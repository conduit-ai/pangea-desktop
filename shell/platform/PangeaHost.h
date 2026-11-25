// The Pangea plugin (docs/pangea-plugin.md): one plugin in the DAW that opens any of the member's
// Pangea plugins and changes to a new version while the session plays.
//
// What it opens is a module (PangeaModuleApi.h): one version compiled on its own by the desktop
// app through build-module.sh. The plugin loads it on the message thread, prepares it, and hands it
// to the audio thread, which crossfades from the old version and lets the message thread free it.
// Everything a built plugin does with a version is done here the same way, from the same headers:
// the Faust class (and Faust's polyphony for an instrument, with the effect section its voices
// share when its code defines one), the logic module inside its own guard,
// the notes safety net, host tempo, a MIDI tool's MIDI out. HybridShell.cpp does it for one version
// compiled in; this does it for whichever version is loaded.
//
// The DAW sees a fixed bank of 128 parameters. Each control of the loaded version takes a slot by
// its id and gives it its name, range and units; a control keeps its slot across versions, so
// automation follows it. After the bank comes one more parameter, Pangea's own bypass
// (PangeaBypass.h; decision 75): it is no slot, so no project can take it, rename it or reset it,
// and it is acted on here in process() with the same 30 ms fade a built plugin has. The editor
// shows the member's plugins (library.json, which the desktop app writes) and, once one is loaded,
// the panel its creator designed (pangea::Editor, as in a plugin).
//
// The effect, Pangea, has a sidechain input since 0.3.0: a second input bus, Sidechain, stereo and
// off until the host routes something to it (docs/sidechain.md). A version whose code declares
// a sidechain is fed from it, or with silence while it is off; any other version never reads it.
//
// The kind of plugin is a constructor argument: PangeaHost.cpp passes PANGEA_HOST_KIND for each of
// the three plugins; tools/host-check makes all three in one process.
#pragma once

#if defined (__clang__)
 #pragma clang fp contract(off)
#endif
#include "logic_api.h"
#if defined (__clang__)
 #pragma clang fp contract(on)
#endif
#include "PangeaModuleApi.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_cryptography/juce_cryptography.h>

#include <faust/gui/UI.h>
#include <faust/gui/meta.h>
#include <faust/dsp/dsp.h>
#include <faust/gui/MapUI.h>
#include <faust/dsp/poly-dsp.h>

#include "PangeaModel.h"
#include "PangeaSong.h"
#include "PangeaNotes.h"
#include "PangeaBypass.h"
#include "PangeaEditor.h"
#include "PangeaWebEditor.h"

#include <cerrno>
#include <dlfcn.h>
#include <map>
#include <memory>
#include <pwd.h>
#include <set>
#include <signal.h>
#include <unistd.h>

namespace pangea::host
{

enum class HostKind { effect = 0, instrument = 1, midiFx = 2 };

constexpr int kSlots = 128;
constexpr int kMaxFaustChannels = 8;
constexpr int kMaxSlice = 4096;          // mydsp_poly mixes through a fixed 4096-frame buffer
constexpr double kFadeSeconds = 0.03;    // the crossfade from one version to the next
constexpr double kRequestTimeoutSeconds = 180.0;

inline juce::String pluginName (HostKind k)
{
    switch (k)
    {
        case HostKind::instrument: return "Pangea Instrument";
        case HostKind::midiFx: return "Pangea MIDI FX";
        case HostKind::effect: break;
    }
    return "Pangea";
}

/** Whether this plugin can open a project of `kind` (effect, instrument, midi). */
inline bool canOpen (HostKind host, const juce::String& kind)
{
    switch (host)
    {
        case HostKind::effect: return kind == "effect";
        case HostKind::instrument: return kind == "instrument" || kind == "midi";
        case HostKind::midiFx: return kind == "midi";
    }
    return false;
}

/** Where a project that this plugin cannot open does open. */
inline juce::String opensIn (const juce::String& kind)
{
    if (kind == "effect") return "Opens in Pangea";
    if (kind == "instrument") return "Opens in Pangea Instrument";
    return "Opens in Pangea Instrument or Pangea MIDI FX";
}

inline juce::String kindWord (const juce::String& kind)
{
    if (kind == "instrument") return "Instrument";
    if (kind == "midi") return "MIDI tool";
    return "Effect";
}

// ---- The shared folder ------------------------------------------------------------------------

/** The member's own home, from the user database: a sandboxed DAW (Logic) sets HOME to its container. */
inline juce::File realHome()
{
    if (auto* pw = getpwuid (getuid()); pw != nullptr && pw->pw_dir != nullptr)
        return juce::File (juce::String::fromUTF8 (pw->pw_dir));
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory);
}

/** `~/Library/Application Support/Pangea/host`, or PANGEA_HOST_DIR (tools/host-check uses it). */
inline juce::File hostDir()
{
    if (const char* env = std::getenv ("PANGEA_HOST_DIR"); env != nullptr && *env != 0)
        return juce::File (juce::String::fromUTF8 (env));
    return realHome().getChildFile ("Library/Application Support/Pangea/host");
}

/** Writes `<name>.tmp` and renames it into place, so the desktop app never reads half a file. */
inline bool writeAtomically (const juce::File& target, const juce::String& text)
{
    target.getParentDirectory().createDirectory();
    const auto tmp = target.getSiblingFile (target.getFileName() + ".tmp");
    tmp.deleteFile();
    {
        juce::FileOutputStream out (tmp);
        if (! out.openedOk())
            return false;
        out.writeText (text, false, false, "\n");
        out.flush();
        if (out.getStatus().failed())
            return false;
    }
    return tmp.moveFileTo (target);
}

inline bool processAlive (int pid)
{
    if (pid <= 0)
        return false;
    if (kill (pid, 0) == 0)
        return true;
    return errno == EPERM;   // there, but not ours to signal (a sandboxed DAW may see this)
}

/** One of the member's plugins, as library.json lists it. */
struct Entry
{
    juce::String projectId, name, slug, kind, owner, description;
    /** Where it is filed (decision 85): the category's id and the name people read, and the head
        version's own summary. Empty when it is not filed, and from an app that does not send them. */
    juce::String category, categoryName, summary;
    /** Its tags, and the other words it is found by as its category (the category's other names and
        its own tags that name a kind of the category): both are searched, the second is never shown. */
    juce::StringArray tags, keywords;
    int headVersion = 0;   // 0: no version yet
    bool mine = false;
};

struct Library
{
    bool found = false, appRunning = false, signedIn = false;
    juce::String member, error;
    std::vector<Entry> plugins;

    const Entry* find (const juce::String& projectId) const
    {
        for (const auto& e : plugins)
            if (e.projectId == projectId)
                return &e;
        return nullptr;
    }
};

inline juce::String str (const juce::var& v, const char* key)
{
    const auto x = v.getProperty (key, {});
    return x.isString() ? x.toString() : juce::String();
}

inline int num (const juce::var& v, const char* key)
{
    const auto x = v.getProperty (key, {});
    return (x.isInt() || x.isInt64() || x.isDouble()) ? (int) x : 0;
}

/** The strings of a list; anything else in it, and a field that is no list, is left out. */
inline juce::StringArray strs (const juce::var& v, const char* key)
{
    juce::StringArray out;
    if (const auto* list = v.getProperty (key, {}).getArray())
        for (const auto& x : *list)
            if (x.isString())
                out.add (x.toString());
    return out;
}

inline Library readLibrary (const juce::File& dir)
{
    Library lib;
    const auto file = dir.getChildFile ("library.json");
    if (! file.existsAsFile())
        return lib;
    const auto json = juce::JSON::parse (file.loadFileAsString());
    if (! json.isObject())
        return lib;
    lib.found = true;
    lib.appRunning = processAlive (num (json, "appPid"));
    lib.signedIn = (bool) json.getProperty ("signedIn", false);
    lib.error = str (json, "error");
    if (const auto member = json.getProperty ("member", {}); member.isObject())
        lib.member = str (member, "name").isNotEmpty() ? str (member, "name") : str (member, "email");
    if (const auto* list = json.getProperty ("plugins", {}).getArray())
    {
        for (const auto& p : *list)
        {
            Entry e;
            e.projectId = str (p, "projectId");
            if (e.projectId.isEmpty())
                continue;
            e.name = str (p, "name").isNotEmpty() ? str (p, "name") : e.projectId;
            e.slug = str (p, "slug");
            e.kind = str (p, "kind").isNotEmpty() ? str (p, "kind") : juce::String ("effect");
            e.owner = str (p, "owner");
            e.description = str (p, "description");
            e.category = str (p, "category");
            e.categoryName = str (p, "categoryName");
            e.summary = str (p, "summary");
            e.tags = strs (p, "tags");
            e.keywords = strs (p, "keywords");
            e.headVersion = juce::jmax (0, num (p, "headVersion"));
            e.mine = (bool) p.getProperty ("mine", false);
            lib.plugins.push_back (e);
        }
    }
    return lib;
}

/** A module directory's manifest.json (build-module.sh writes it). */
struct Manifest
{
    int abi = 0, version = 0;
    juce::String projectId, name, slug, kind, sha256, faust;
    bool logic = false;
};

inline bool readManifest (const juce::File& moduleDir, Manifest& m, juce::String& why)
{
    const auto file = moduleDir.getChildFile ("manifest.json");
    const auto json = juce::JSON::parse (file.loadFileAsString());
    if (! json.isObject())
    {
        why = "The module at " + moduleDir.getFullPathName() + " has no readable manifest.";
        return false;
    }
    m.abi = num (json, "abi");
    m.version = num (json, "version");
    m.projectId = str (json, "projectId");
    m.name = str (json, "name");
    m.slug = str (json, "slug");
    m.kind = str (json, "kind");
    m.sha256 = str (json, "sha256");
    m.faust = str (json, "faust");
    m.logic = (bool) json.getProperty ("logic", false);
    if (m.name.isEmpty()) m.name = m.slug.isNotEmpty() ? m.slug : juce::String ("Untitled");
    if (m.kind.isEmpty()) m.kind = "effect";
    return true;
}

inline juce::File moduleDirFor (const juce::String& projectId, int version)
{
    return hostDir().getChildFile ("modules").getChildFile (projectId).getChildFile ("v" + juce::String (version));
}

/** A project id that is safe as a folder name (the desktop app checks the same). */
inline bool safeId (const juce::String& id)
{
    return id.isNotEmpty() && id.length() <= 128
        && id.containsOnly ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_");
}

// ---- A module's code ---------------------------------------------------------------------------

/** The loaded library and its entry points. Closed when the last instance using it goes. */
struct ModuleCode
{
    void* handle = nullptr;
    PangeaCreateDspFn createDsp = nullptr;
    PangeaCreateEffectFn createEffect = nullptr;   // an instrument's effect section; it answers null when there is none
    bool hasLogic = false;
    PangeaLogicNumParamsFn numParams = nullptr;
    PangeaLogicParamsFn params = nullptr;
    PangeaLogicNumControlsFn numControls = nullptr;
    PangeaLogicControlNameFn controlName = nullptr;
    PangeaLogicSetupFn setup = nullptr;
    PangeaLogicGuardNewFn guardNew = nullptr;
    PangeaLogicGuardFreeFn guardFree = nullptr;
    PangeaLogicMakeFn make = nullptr;
    PangeaLogicUnmakeFn unmake = nullptr;
    PangeaLogicProcessFn process = nullptr;
    PangeaLogicStepsUsedFn stepsUsed = nullptr;

    ModuleCode() = default;
    ModuleCode (const ModuleCode&) = delete;
    ModuleCode& operator= (const ModuleCode&) = delete;
    ~ModuleCode() { if (handle != nullptr) dlclose (handle); }

    static std::shared_ptr<ModuleCode> open (const juce::File& dylib, juce::String& why)
    {
        auto code = std::make_shared<ModuleCode>();
        code->handle = dlopen (dylib.getFullPathName().toRawUTF8(), RTLD_NOW | RTLD_LOCAL);
        if (code->handle == nullptr)
        {
            const char* err = dlerror();
            why = "The module could not be loaded: " + juce::String::fromUTF8 (err != nullptr ? err : "unknown reason");
            return nullptr;
        }
        auto sym = [&code] (const char* name) { return dlsym (code->handle, name); };
        auto abi = reinterpret_cast<PangeaModuleAbiFn> (sym ("pangea_module_abi"));
        code->createDsp = reinterpret_cast<PangeaCreateDspFn> (sym ("pangea_create_dsp"));
        auto present = reinterpret_cast<PangeaLogicPresentFn> (sym ("pangea_logic_present"));
        if (abi == nullptr || code->createDsp == nullptr || present == nullptr)
        {
            why = "That file is not a Pangea module.";
            return nullptr;
        }
        if (abi() != PANGEA_MODULE_ABI)
        {
            why = "The module was built for another version of the Pangea plugin.";
            return nullptr;
        }
        code->createEffect = reinterpret_cast<PangeaCreateEffectFn> (sym ("pangea_create_effect"));
        if (code->createEffect == nullptr)
        {
            why = "That file is not a Pangea module.";
            return nullptr;
        }
        code->hasLogic = present() == 1;
        if (code->hasLogic)
        {
            code->numParams = reinterpret_cast<PangeaLogicNumParamsFn> (sym ("pangea_logic_num_params"));
            code->params = reinterpret_cast<PangeaLogicParamsFn> (sym ("pangea_logic_params"));
            code->numControls = reinterpret_cast<PangeaLogicNumControlsFn> (sym ("pangea_logic_num_controls"));
            code->controlName = reinterpret_cast<PangeaLogicControlNameFn> (sym ("pangea_logic_control_name"));
            code->setup = reinterpret_cast<PangeaLogicSetupFn> (sym ("pangea_logic_setup"));
            code->guardNew = reinterpret_cast<PangeaLogicGuardNewFn> (sym ("pangea_logic_guard_new"));
            code->guardFree = reinterpret_cast<PangeaLogicGuardFreeFn> (sym ("pangea_logic_guard_free"));
            code->make = reinterpret_cast<PangeaLogicMakeFn> (sym ("pangea_logic_make"));
            code->unmake = reinterpret_cast<PangeaLogicUnmakeFn> (sym ("pangea_logic_unmake"));
            code->process = reinterpret_cast<PangeaLogicProcessFn> (sym ("pangea_logic_process"));
            code->stepsUsed = reinterpret_cast<PangeaLogicStepsUsedFn> (sym ("pangea_logic_steps_used"));
            if (! code->numParams || ! code->params || ! code->numControls || ! code->controlName || ! code->setup
                || ! code->guardNew || ! code->guardFree || ! code->make || ! code->unmake || ! code->process || ! code->stepsUsed)
            {
                why = "The module's logic entry points are incomplete.";
                return nullptr;
            }
        }
        return code;
    }
};

// ---- One loaded version ------------------------------------------------------------------------

struct Event
{
    enum class Kind { logic, noteOn, noteOff, allNotesOff, sustain };
    int offset = 0;
    int sequence = 0;
    Kind kind = Kind::logic;
    int a = 0;       // logic: control index; notes: pitch; sustain: 1 or 0
    float b = 0;     // logic: value; notes: velocity
};

/** What the host tells a block: read once per block from the play head, as HybridShell reads it. */
struct Transport
{
    double sampleRate = 48000.0, bpm = 120.0, ppq = 0.0, beatsPerBar = 4.0;
    bool playing = true, havePosition = false;
};

/**
 * One version, loaded and prepared: its code, its Faust engine, its logic module and everything a
 * block of it needs. Made and freed on the message thread; between, only the audio thread uses it.
 */
struct Instance
{
    // Declared first, so it is released last: everything below is the module's code or objects.
    std::shared_ptr<ModuleCode> code;

    juce::String projectId, name, slug, kind;
    int version = 0;
    juce::File dir;
    juce::MemoryBlock uiJson;
    Credit credit;   // who built it and Pangea's address, for the web panel's footer

    std::unique_ptr<dsp> engine;
    mydsp_poly* poly = nullptr;          // owned by engine when polyphonic
    MapUI monoMap;
    std::vector<MapUI*> voiceMaps;
    MapUI effectMap;                     // the effect section's zones, when the instrument has one (decision 73)
    std::vector<MapUI*> effectMaps;
    ControlCollector collector;
    MetaCollector meta;
    int numInputs = 0, numOutputs = 0, numVoices = 0;
    int sidechainInputs = 0;   // the last of numInputs, fed from the plugin's Sidechain bus
    bool polyphonic = false;
    std::vector<LogicControl> logic;

    /** The sound's own inputs: all of them but the sidechain's. */
    int mainInputs() const { return numInputs - sidechainInputs; }

    struct Binding { const Control* control; juce::String key; int slot = -1; juce::NormalisableRange<float> range; bool toggle = false; std::vector<FAUSTFLOAT*> zones; };
    struct TempoBinding { std::vector<FAUSTFLOAT*> zones; float min, max; };
    enum class SongValue { playing, beat, fraction };
    struct TransportBinding { std::vector<FAUSTFLOAT*> zones; float min, max; SongValue what = SongValue::playing; };
    struct LogicBinding { juce::String key; int slot = -1; juce::NormalisableRange<float> range; float init = 0; };
    std::vector<Binding> bindings;            // the Faust controls a person turns
    std::vector<TempoBinding> tempo;          // [pangea:bpm]
    std::vector<TransportBinding> transport;  // [pangea:playing], [pangea:beat] (decision 123) and [pangea:beatfrac] (124)
    std::vector<LogicBinding> logicParams;    // in the module's order
    std::vector<std::vector<FAUSTFLOAT*>> logicZones;
    std::map<juce::String, int> slotForId;    // a control's parameter id (as the editor asks) to its slot

    // The logic module, inside its own guard.
    void* guard = nullptr;
    void* logicModule = nullptr;
    int stopped = 0;

    // A block's working state, the audio thread's alone once the instance is live.
    double sampleRate = 48000.0;
    int maxBlock = 512;
    juce::AudioBuffer<float> inScratch, outScratch;
    std::vector<Event> events;
    std::vector<LogicEvent> logicEvents;
    std::vector<notes::Note> noteScratch;
    notes::Tracker logicNotes, passNotes;
    notes::TransportFollower follower;
    notes::Display display;
    notes::Heard heard;   // a MIDI tool's notes as it sends them out, for the web panel (docs/felt-look.md)
    MeterTaps meters;   // its bar graphs as they play, for the web panel's meters and step lights (decision 91)
    juce::int64 freeRunningSamples = 0;
    juce::int64 transportSamples = 0;   // since prepare: the song position for [pangea:beat] when the host has none
    bool sustainDown = false;
    std::array<uint8_t, 128> sustained {};
    bool blockPlaying = false, blockJumped = false, blockStopped = false, wasBypassed = false;
    bool endedForBypass = false;   // its voices were ended for the bypass that is on now
    std::atomic<bool> endAllPending { true };

    bool isMidiTool() const { return kind == "midi"; }

    Instance() = default;
    Instance (const Instance&) = delete;
    Instance& operator= (const Instance&) = delete;

    ~Instance()
    {
        endLogic();
        if (guard != nullptr && code != nullptr)
            code->guardFree (guard);
        guard = nullptr;
        engine.reset();   // before the code goes
    }

    // The zones behind a control: one in each voice, and the effect section's when that has it.
    std::vector<FAUSTFLOAT*> zonesFor (const Control& c) const
    {
        std::vector<FAUSTFLOAT*> zones;
        auto find = [&zones] (const std::vector<MapUI*>& maps, const juce::String& path)
        {
            if (path.isEmpty())
                return;
            for (auto* map : maps)
            {
                auto& paths = map->getFullpathMap();
                if (auto it = paths.find (path.toStdString()); it != paths.end())
                    zones.push_back (it->second);
            }
        };
        find (voiceMaps, c.path);
        find (effectMaps, c.effectPath);
        return zones;
    }

    void endLogic()
    {
        if (logicModule == nullptr || code == nullptr)
            return;
        auto* old = logicModule;
        logicModule = nullptr;
        code->unmake (guard, old);
    }

    /** A fresh logic module at this rate, in place of the old one (HybridShell's freshModule). */
    void freshLogic()
    {
        if (code == nullptr || ! code->hasLogic || guard == nullptr)
            return;
        endLogic();
        stopped = 0;
        display.stopped.store (0, std::memory_order_relaxed);
        void* made = nullptr;
        const int why = code->make (guard, sampleRate, &made);
        if (why != 0)
        {
            stopped = why;
            display.stopped.store (why, std::memory_order_relaxed);
            return;
        }
        logicModule = made;
    }

    /** Ready for this rate and block size; off the audio thread, or with it held off. */
    void prepare (double rate, int blockSize)
    {
        sampleRate = rate > 0 ? rate : 48000.0;
        maxBlock = juce::jmax (16, blockSize);
        engine->init ((int) sampleRate);
        inScratch.setSize (juce::jmax (1, numInputs), maxBlock);
        outScratch.setSize (juce::jmax (1, numOutputs), maxBlock);
        inScratch.clear();
        outScratch.clear();
        freeRunningSamples = 0;
        transportSamples = 0;
        sustainDown = false;
        sustained.fill (0);
        follower.forget();
        endAllPending = true;
        endedForBypass = false;
        freshLogic();
    }
};

/** Loads a version's module and prepares it. Returns null and says why when it cannot. */
inline std::unique_ptr<Instance> makeInstance (const juce::File& dir, const Manifest& m, double rate, int blockSize,
                                               bool instrumentHost, juce::String& why)
{
    const auto dylib = dir.getChildFile ("module.dylib");
    if (! dylib.existsAsFile())
    {
        why = "The module file is missing from " + dir.getFullPathName() + ".";
        return nullptr;
    }
    if (m.sha256.isNotEmpty() && juce::SHA256 (dylib).toHexString() != m.sha256)
    {
        why = "The module file does not match its manifest.";
        return nullptr;
    }
    auto code = ModuleCode::open (dylib, why);
    if (code == nullptr)
        return nullptr;

    auto inst = std::make_unique<Instance>();
    inst->code = code;
    inst->projectId = m.projectId;
    inst->name = m.name;
    inst->slug = m.slug;
    inst->kind = m.kind;
    inst->version = m.version;
    inst->dir = dir;
    dir.getChildFile ("ui.json").loadFileAsData (inst->uiJson);
    if (inst->uiJson.getSize() == 0)
        inst->uiJson.append ("{}", 2);
    // The web panel's footer names who built the plugin and links to Pangea: both are in the
    // module's plugin.json, which the desktop app wrote as the site sent it.
    {
        juce::MemoryBlock pluginJson;
        if (dir.getChildFile ("plugin.json").loadFileAsData (pluginJson))
            inst->credit = Credit::fromPluginJson (pluginJson.getData(), pluginJson.getSize());
    }

    // What the code says about itself, read from an instance of its own, as the shell's Model does.
    // The tree the page finds the meters in (PangeaMeters.h, decision 91) is walked beside it: an
    // instrument's voice from this instance, never tapped, and what plays from the instances that
    // run, below, so that each tap points into the sound code the audio thread runs.
    MeterTree meterTree;
    int voiceOutputs = 0;
    {
        std::unique_ptr<dsp> probe (static_cast<dsp*> (code->createDsp()));
        probe->metadata (&inst->meta);
        probe->buildUserInterface (&inst->collector);
        voiceOutputs = probe->getNumOutputs();
        inst->numInputs = juce::jmin (kMaxFaustChannels, probe->getNumInputs());
        inst->numOutputs = juce::jmin (kMaxFaustChannels, voiceOutputs);
        if (inst->kind == "instrument")
        {
            meterTree.beginStage (false);
            probe->buildUserInterface (&meterTree);
        }
    }
    inst->numVoices = juce::jlimit (0, 64, inst->meta.numVoices());
    // An effect whose code declares a sidechain, with the sound's two inputs and then exactly those
    // (docs/sidechain.md). Anything else has none, and its extra inputs, if any, hear silence.
    if (inst->kind == "effect")
    {
        inst->sidechainInputs = sidechainInputsOf (inst->meta.sidechainChannels(), inst->numInputs);
    }
    inst->polyphonic = instrumentHost && inst->kind == "instrument" && inst->numVoices > 1 && inst->collector.hasVoiceControls();

    // An instrument's effect section, the stage its voices share (decision 73): its controls join
    // the voice's, as the site stores them, and what it gives is what the version gives.
    std::unique_ptr<dsp> effect (static_cast<dsp*> (code->createEffect()));
    if (effect != nullptr)
    {
        // build-effect.sh fits the effect to the voice and leaves out one that gives more channels
        // than the plugin carries; a module that says otherwise is not loaded.
        if (effect->getNumInputs() != voiceOutputs || effect->getNumOutputs() > kMaxFaustChannels)
        {
            why = "The module's effect section does not fit its voice.";
            return nullptr;
        }
        inst->collector.beginEffect();
        effect->buildUserInterface (&inst->collector);
        inst->numOutputs = juce::jmin (kMaxFaustChannels, effect->getNumOutputs());
    }
    if (! inst->polyphonic)
        for (auto& c : inst->collector.controls)
            c.voiceDriven = false;   // a mono instrument keeps freq, gain and gate as ordinary parameters

    auto* d = static_cast<dsp*> (code->createDsp());
    dsp* voices = d;
    if (inst->polyphonic)
    {
        inst->poly = new mydsp_poly (d, inst->numVoices, true, false);
        voices = inst->poly;
        for (auto* voice : inst->poly->fVoiceTable)
            inst->voiceMaps.push_back (voice);
    }
    else
    {
        d->buildUserInterface (&inst->monoMap);
        inst->voiceMaps.push_back (&inst->monoMap);
    }
    if (inst->kind == "effect")
    {
        meterTree.beginStage (true);
        d->buildUserInterface (&meterTree);   // an effect plays as it is: the instance that runs
    }
    if (effect != nullptr)
    {
        // Run on the mixed voices, slice by slice, the way Faust's own polyphonic architecture
        // runs it: a sequencer that owns both, with a buffer of its own one slice long.
        effect->buildUserInterface (&inst->effectMap);
        inst->effectMaps.push_back (&inst->effectMap);
        if (inst->kind != "midi")
        {
            meterTree.beginStage (true);
            effect->buildUserInterface (&meterTree);   // the effect section that runs, after the voices
        }
        inst->engine.reset (new dsp_sequencer (voices, effect.release(), kMaxSlice));
    }
    else
        inst->engine.reset (voices);
    inst->meters.take (meterTree, inst->kind);

    for (const auto& c : inst->collector.controls)
    {
        if (c.isParameter())
            inst->bindings.push_back ({ &c, "f:" + c.id, -1, {}, c.isToggle(), inst->zonesFor (c) });
        else if (c.tempoDriven)
            inst->tempo.push_back ({ inst->zonesFor (c), c.min, c.max });
        else if (c.playDriven || c.beatDriven || c.fractionDriven)
            inst->transport.push_back ({ inst->zonesFor (c), c.min, c.max,
                                         c.beatDriven       ? Instance::SongValue::beat
                                         : c.fractionDriven ? Instance::SongValue::fraction
                                                            : Instance::SongValue::playing });
    }

    if (code->hasLogic)
    {
        if (code->setup() != 1)
        {
            why = "The step budget for the logic module could not be set up.";
            return nullptr;
        }
        inst->logic = describeLogic (code->params(), code->numParams());
        for (const auto& lc : inst->logic)
            inst->logicParams.push_back ({ "l:" + lc.key(), -1, {}, lc.init });
        for (int i = 0; i < code->numControls(); ++i)
        {
            // A control of that name that only the effect section has is driven too, beside the voice's.
            std::vector<FAUSTFLOAT*> zones, effectZones;
            const auto wanted = juce::String::fromUTF8 (code->controlName (i));
            for (const auto& c : inst->collector.controls)
                if (c.logicDriven && c.label == wanted)
                    (c.path.isNotEmpty() ? zones : effectZones) = inst->zonesFor (c);
            zones.insert (zones.end(), effectZones.begin(), effectZones.end());
            inst->logicZones.push_back (zones);
        }
        inst->guard = code->guardNew();
    }
    else if (inst->kind == "midi")
    {
        why = "This MIDI tool has no logic module.";
        return nullptr;
    }

    inst->events.reserve (2048);
    inst->logicEvents.resize ((size_t) notes::maxEvents);
    inst->noteScratch.resize ((size_t) (2 * notes::maxEvents + 16 * 128));
    inst->prepare (rate, blockSize);
    return inst;
}

// ---- One of the 128 parameters the DAW sees ------------------------------------------------------

class SlotParameter : public juce::RangedAudioParameter
{
public:
    enum class Kind { unused, number, toggle, menu };

    struct Spec
    {
        juce::String key, name, unit;
        juce::NormalisableRange<float> range { 0.0f, 1.0f };
        float defaultReal = 0.0f;
        Kind kind = Kind::number;
        juce::StringArray labels;   // a menu's, for min, min + 1, ...
        int decimals = 2;
    };

    explicit SlotParameter (int slotIndex)
        : RangedAudioParameter (juce::ParameterID { "p" + juce::String (slotIndex + 1).paddedLeft ('0', 3), 1 }, unusedName (slotIndex)),
          index (slotIndex)
    {
        spec.name = unusedName (slotIndex);
        spec.kind = Kind::unused;
    }

    static juce::String unusedName (int i) { return "Slot " + juce::String (i + 1); }

    // The audio thread reads only this.
    float getValue() const override { return value.load (std::memory_order_relaxed); }
    void setValue (float v) override { value.store (juce::jlimit (0.0f, 1.0f, v), std::memory_order_relaxed); }
    float getDefaultValue() const override { return defaultValue.load (std::memory_order_relaxed); }

    juce::String getName (int maxLength) const override
    {
        const juce::SpinLock::ScopedLockType l (lock);
        return spec.name.substring (0, maxLength);
    }
    juce::String getLabel() const override
    {
        const juce::SpinLock::ScopedLockType l (lock);
        return spec.unit;
    }
    juce::String getText (float v, int maxLength) const override
    {
        const juce::SpinLock::ScopedLockType l (lock);
        juce::String text;
        switch (spec.kind)
        {
            case Kind::unused: text = "Unused"; break;
            case Kind::toggle: text = v >= 0.5f ? "On" : "Off"; break;
            case Kind::menu:
            {
                const float real = spec.range.snapToLegalValue (spec.range.convertFrom0to1 (v));
                const int i = juce::roundToInt (real - spec.range.start);
                text = (i >= 0 && i < spec.labels.size()) ? spec.labels[i] : juce::String (juce::roundToInt (real));
                break;
            }
            case Kind::number: text = juce::String (spec.range.convertFrom0to1 (v), spec.decimals); break;
        }
        return maxLength > 0 ? text.substring (0, maxLength) : text;
    }
    float getValueForText (const juce::String& text) const override
    {
        const juce::SpinLock::ScopedLockType l (lock);
        const auto t = text.trim();
        switch (spec.kind)
        {
            case Kind::unused: return 0.0f;
            case Kind::toggle: return (t.equalsIgnoreCase ("on") || t.getFloatValue() >= 0.5f) ? 1.0f : 0.0f;
            case Kind::menu:
                for (int i = 0; i < spec.labels.size(); ++i)
                    if (spec.labels[i].equalsIgnoreCase (t))
                        return spec.range.convertTo0to1 (spec.range.start + (float) i);
                return spec.range.convertTo0to1 (spec.range.snapToLegalValue (t.getFloatValue()));
            case Kind::number: return spec.range.convertTo0to1 (spec.range.snapToLegalValue (t.getFloatValue()));
        }
        return 0.0f;
    }
    const juce::NormalisableRange<float>& getNormalisableRange() const override { return spec.range; }

    /** The control this slot now stands for. Message thread. */
    void assign (const Spec& s)
    {
        {
            const juce::SpinLock::ScopedLockType l (lock);
            spec = s;
        }
        defaultValue.store (s.kind == Kind::unused ? 0.0f : s.range.convertTo0to1 (s.range.snapToLegalValue (s.defaultReal)));
    }
    void clear()
    {
        Spec s;
        s.name = unusedName (index);
        s.kind = Kind::unused;
        assign (s);
    }
    Spec getSpec() const
    {
        const juce::SpinLock::ScopedLockType l (lock);
        return spec;
    }
    juce::String key() const
    {
        const juce::SpinLock::ScopedLockType l (lock);
        return spec.key;
    }

    const int index;

private:
    std::atomic<float> value { 0.0f }, defaultValue { 0.0f };
    mutable juce::SpinLock lock;
    Spec spec;
};

/** A Faust control's range as the shell builds it (HybridShell's buildLayout). */
inline SlotParameter::Spec specFor (const Control& c)
{
    SlotParameter::Spec s;
    s.name = c.label;
    s.unit = c.unit;
    if (c.isToggle())
    {
        s.kind = SlotParameter::Kind::toggle;
        s.range = juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f);
        s.defaultReal = c.init >= 0.5f ? 1.0f : 0.0f;
        s.decimals = 0;
        return s;
    }
    float mn = c.min, mx = c.max;
    if (mx <= mn) mx = mn + 1.0f;
    const bool stepped = c.step >= 1.0f;
    s.decimals = stepped ? 0 : (c.step > 0 ? juce::jlimit (1, 4, (int) std::ceil (-std::log10 (c.step))) : 2);
    s.range = juce::NormalisableRange<float> (mn, mx, stepped ? c.step : 0.0f);
    if (c.logScale && mn > 0)
        s.range.setSkewForCentre (std::sqrt (mn * mx));
    s.defaultReal = juce::jlimit (mn, mx, c.init);
    s.kind = SlotParameter::Kind::number;
    return s;
}

/** A logic control's (HybridShell's buildLayout, the logic part). */
inline SlotParameter::Spec specFor (const LogicControl& c)
{
    SlotParameter::Spec s;
    s.name = c.hostName();
    s.unit = c.unit;
    const bool menu = c.style == LogicControl::Style::menu && ! c.labels.isEmpty();
    const bool whole = menu || c.step >= 1.0f;
    s.range = juce::NormalisableRange<float> (c.min, c.max > c.min ? c.max : c.min + 1.0f, whole ? juce::jmax (1.0f, c.step) : 0.0f);
    s.defaultReal = c.init;
    s.kind = menu ? SlotParameter::Kind::menu : SlotParameter::Kind::number;
    s.labels = c.labels;
    s.decimals = whole ? 0 : (c.step > 0 ? juce::jlimit (1, 4, (int) std::ceil (-std::log10 (c.step))) : 2);
    return s;
}

class HostEditor;

// ---- The plugin --------------------------------------------------------------------------------

class Processor : public juce::AudioProcessor,
                  public juce::ChangeBroadcaster,
                  private juce::Timer,
                  private juce::AsyncUpdater
{
public:
    /** How a version comes in: chosen from the list, a newer version of the one loaded, or a session. */
    enum class Mode { fresh, swap, restore };

    explicit Processor (HostKind k)
        : AudioProcessor (busesFor (k)), hostKind (k)
    {
        for (int i = 0; i < kSlots; ++i)
        {
            auto* p = new SlotParameter (i);
            slots[(size_t) i] = p;
            addParameter (p);
        }
        // Pangea's own bypass, the same parameter every built plugin has (PangeaBypass.h; decision
        // 75). It comes after the slots and is not one of them: slots are given out among the 128
        // only, so no project can take it, rename it or reset it, and its automation holds
        // through every version and every plugin opened here. Its hint is above the slots', so in
        // an Audio Unit's list it is last and the slots keep their places.
        bypassParam = new juce::AudioParameterBool (juce::ParameterID { pangea::bypass::parameterId, pangea::bypass::versionHint },
                                                    pangea::bypass::parameterName, false);
        addParameter (bypassParam);
        fadeMidi.ensureSize (8192);
        midiOut.ensureSize (8192);
        library = readLibrary (hostDir());
        jassert (hostKind != HostKind::effect || getBusCount (true) == 2);
        status = library.found ? juce::String() : juce::String ("Open the Pangea app and sign in to see your plugins here.");
        startTimerHz (5);
    }

    ~Processor() override
    {
        stopTimer();
        cancelPendingUpdate();
        const juce::SpinLock::ScopedLockType l (swapLock);
        current = fading = nullptr;
    }

    // ---- What the DAW calls --------------------------------------------------------------------

    void prepareToPlay (double rate, int blockSize) override
    {
        sampleRate.store (rate);
        maxBlock.store (blockSize);
        fadeScratch.setSize (2, juce::jmax (16, blockSize));
        // The bypass: the length of its fade at this rate, and room for an effect's input.
        bypassFader.prepare (rate);
        bypassEnded = false;
        dryScratch.setSize (2, juce::jmax (16, blockSize));
        dryScratch.clear();
        sideScratch.setSize (2, juce::jmax (16, blockSize));
        sideScratch.clear();
        const juce::SpinLock::ScopedLockType l (swapLock);
        fading = nullptr;   // a fade cannot carry over a change of rate
        if (current != nullptr)
            current->prepare (rate, blockSize);
    }

    void releaseResources() override { endAllNotes(); }
    void reset() override { endAllNotes(); }

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        if (hostKind == HostKind::midiFx)
            return true;
        const auto out = layouts.getMainOutputChannelSet();
        if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
            return false;
        if (hostKind == HostKind::instrument)
            return true;
        const auto in = layouts.getMainInputChannelSet();
        if (! (in.isDisabled() || in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono()))
            return false;
        // The sidechain: off, mono or stereo, whatever the main bus is.
        if (layouts.inputBuses.size() < 2)
            return true;
        const auto side = layouts.getChannelSet (true, 1);
        return side.isDisabled() || side == juce::AudioChannelSet::stereo() || side == juce::AudioChannelSet::mono();
    }

    // The bypass is a parameter the plugin returns (getBypassParameter), so JUCE's wrappers never
    // call processBlockBypassed: a host's bypass arrives as the parameter, and process() reads it.
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        process (buffer, midi, false);
    }

    // For a host that calls this all the same: the same path, bypassed whatever the parameter says.
    void processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        process (buffer, midi, true);
    }

    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return pluginName (hostKind); }
    bool acceptsMidi() const override { return hostKind != HostKind::effect; }
    bool producesMidi() const override { return hostKind != HostKind::effect; }
    bool isMidiEffect() const override { return hostKind == HostKind::midiFx; }
    double getTailLengthSeconds() const override { return hostKind == HostKind::midiFx ? 0.0 : 4.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& dest) override
    {
        juce::XmlElement xml ("PANGEA_HOST");
        xml.setAttribute ("format", 1);
        xml.setAttribute ("follow", followLatest ? 1 : 0);
        // The panel is the component library's unless the person asked for the shell's own
        // (decision 77). A session saved before that carries "web", which was 0 wherever nobody
        // had touched the switch: it is written no more and never read.
        if (! webPanel)
            xml.setAttribute ("panel", "native");
        xml.setAttribute ("bypass", bypassParam->get() ? 1 : 0);   // no slot's value: it is saved by name
        if (live != nullptr)
        {
            xml.setAttribute ("projectId", live->projectId);
            xml.setAttribute ("version", live->version);
            xml.setAttribute ("name", live->name);
        }
        else if (restoring.projectId.isNotEmpty())
        {
            // A session saved before its version arrived keeps asking for it.
            xml.setAttribute ("projectId", restoring.projectId);
            xml.setAttribute ("version", restoring.version);
            xml.setAttribute ("name", restoring.name);
        }
        juce::StringArray values;
        for (auto* s : slots)
            values.add (juce::String (s->getValue(), 6));
        xml.createNewChildElement ("Values")->addTextElement (values.joinIntoString (" "));
        for (const auto& [projectId, map] : slotMaps)
        {
            auto* p = xml.createNewChildElement ("Project");
            p->setAttribute ("id", projectId);
            for (const auto& [key, slot] : map)
            {
                auto* k = p->createNewChildElement ("Slot");
                k->setAttribute ("key", key);
                k->setAttribute ("index", slot);
            }
        }
        copyXmlToBinary (xml, dest);
    }

    void setStateInformation (const void* data, int size) override
    {
        auto xml = getXmlFromBinary (data, size);
        if (xml == nullptr || ! xml->hasTagName ("PANGEA_HOST"))
            return;
        followLatest = xml->getIntAttribute ("follow", 0) != 0;
        // A song opens with the version it was saved with, Follow or not (decision 83). What was made
        // while it was closed is held back until the app's next library says what that is.
        followHeld = followLatest;
        heldSince = juce::Time::getCurrentTime();
        followAbove = 0;
        webPanel = xml->getStringAttribute ("panel") != "native";
        // A session saved before the plugin had its bypass says nothing of it: it opens with the plugin in.
        bypassParam->setValueNotifyingHost (xml->getIntAttribute ("bypass", 0) != 0 ? 1.0f : 0.0f);
        if (auto* values = xml->getChildByName ("Values"))
        {
            const auto parts = juce::StringArray::fromTokens (values->getAllSubText(), " ", "");
            for (int i = 0; i < kSlots && i < parts.size(); ++i)
                slots[(size_t) i]->setValue (parts[i].getFloatValue());
        }
        slotMaps.clear();
        for (auto* p : xml->getChildWithTagNameIterator ("Project"))
        {
            auto& map = slotMaps[p->getStringAttribute ("id")];
            for (auto* k : p->getChildWithTagNameIterator ("Slot"))
            {
                const int index = k->getIntAttribute ("index", -1);
                if (index >= 0 && index < kSlots)
                    map[k->getStringAttribute ("key")] = index;
            }
        }
        restoring = { xml->getStringAttribute ("projectId"), xml->getIntAttribute ("version", 0), xml->getStringAttribute ("name") };
        if (restoring.projectId.isEmpty() || ! safeId (restoring.projectId))
        {
            restoring = {};
            return;
        }
        if (juce::MessageManager::existsAndIsCurrentThread())
            handleAsyncUpdate();
        else
            triggerAsyncUpdate();
    }

    // ---- What the editor calls (message thread) -------------------------------------------------

    HostKind kind() const noexcept { return hostKind; }
    const Library& getLibrary() const noexcept { return library; }
    std::shared_ptr<Instance> loaded() const { return live; }
    juce::String getStatus() const { return status; }
    bool isBusy() const noexcept { return pending.requestId.isNotEmpty(); }
    juce::String busyProject() const { return pending.projectId; }
    bool followsLatest() const noexcept { return followLatest; }
    /** The panel is drawn by the component library in a web view (PangeaWebEditor.h), not natively. */
    bool usesWebPanel() const noexcept { return webPanel; }
    void setWebPanel (bool on) { webPanel = on; sendChangeMessage(); }
    /** Ticked by the member: the newest version now, and each one after it (decision 83). */
    void setFollowLatest (bool on)
    {
        followLatest = on;
        releaseFollow();
        checkForNewer();
        sendChangeMessage();
    }
    SlotParameter* slot (int i) const { return (i >= 0 && i < kSlots) ? slots[(size_t) i] : nullptr; }
    /** Pangea's bypass: the one parameter after the slots (PangeaBypass.h). */
    juce::AudioParameterBool* bypassParameter() const noexcept { return bypassParam; }

    /** Opens a project from the list: its newest version, built if it must be. */
    void choose (const juce::String& projectId)
    {
        const auto* e = library.find (projectId);
        if (e == nullptr || ! safeId (projectId))
            return;
        if (! canOpen (hostKind, e->kind))
            return setStatus (e->name + " is " + (e->kind == "instrument" ? "an instrument" : (e->kind == "midi" ? "a MIDI tool" : "an effect")) + ". " + opensIn (e->kind) + ".");
        if (! e->mine)
            return setStatus ("For now the Pangea plugin opens your own plugins. Install " + e->name + " from the Pangea app.");
        releaseFollow();   // the member chose the newest version
        const auto mode = (live != nullptr && live->projectId == projectId) ? Mode::swap : Mode::fresh;
        if (live != nullptr && live->projectId == projectId && live->version >= e->headVersion && e->headVersion > 0)
            return setStatus (e->name + " v" + juce::String (live->version) + " is open.");
        if (e->headVersion > 0)
        {
            // Already fetched: open it now. A module in an older format is built again (load asks for that).
            const auto dir = moduleDirFor (projectId, e->headVersion);
            if (dir.getChildFile ("manifest.json").existsAsFile())
            {
                load (dir, mode);
                return;
            }
        }
        request (projectId, 0, mode, false, e->name);
    }

    /** Asks for the newest version of what is loaded. */
    void refresh()
    {
        if (live == nullptr || isBusy())
            return;
        releaseFollow();   // the member asked for the newest version
        request (live->projectId, 0, Mode::swap, false, live->name);
    }

    /** Asks the desktop app to write library.json again. */
    void refreshLibrary()
    {
        writeRequest ("library", {}, 0, false);
        setStatus (library.appRunning ? "Asking the Pangea app for your plugins." : "Open the Pangea app to see your plugins.");
    }

    /** Loads a module directory now (also for tools/host-check). True when it is live. */
    bool load (const juce::File& dir, Mode mode)
    {
        Manifest m;
        juce::String why;
        if (! readManifest (dir, m, why))
            return setStatus (why), false;
        if (m.abi != PANGEA_MODULE_ABI)
        {
            request (m.projectId, m.version, mode, true, m.name);
            return false;
        }
        if (! canOpen (hostKind, m.kind))
            return setStatus (m.name + ": " + opensIn (m.kind) + "."), false;
        auto inst = makeInstance (dir, m, sampleRate.load(), maxBlock.load(), hostKind == HostKind::instrument, why);
        if (inst == nullptr)
            return setStatus (m.name + ": " + why), false;
        adopt (std::shared_ptr<Instance> (std::move (inst)), mode);
        return true;
    }

    // For tools/host-check: answer the request the app would see, and run the timer's work now.
    juce::String pendingRequestId() const { return pending.requestId; }
    void poll() { timerCallback(); }
    bool fadeRunning() const { const juce::SpinLock::ScopedLockType l (swapLock); return fading != nullptr; }

private:
    friend class HostEditor;

    static BusesProperties busesFor (HostKind k)
    {
        switch (k)
        {
            case HostKind::midiFx: return BusesProperties();   // a MIDI effect: no audio, as Logic's MIDI FX slot expects
            case HostKind::instrument: return BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true);
            case HostKind::effect: break;
        }
        // The sidechain comes after the main buses and is off by default, so a host that never
        // routes one has the plugin as it was before 0.3.0 (docs/sidechain.md).
        return BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                                .withInput (pangea::sidechainBusName, juce::AudioChannelSet::stereo(), false);
    }

    // ---- Versions in and out ---------------------------------------------------------------------

    /** Gives each control of `inst` its slot and sets the slot up; then the audio thread takes it. */
    void adopt (std::shared_ptr<Instance> inst, Mode mode)
    {
        assignSlots (*inst, mode);
        std::vector<std::shared_ptr<Instance>> toFree;
        {
            const juce::SpinLock::ScopedLockType l (swapLock);
            const double rate = sampleRate.load();
            if (rate > 0 && std::abs (inst->sampleRate - rate) > 0.5)
                inst->prepare (rate, maxBlock.load());
            // A fade still running from the last swap stops here: the audio thread holds nothing
            // outside this lock, so the version it was fading can go.
            fading = nullptr;
            toFree.push_back (std::move (old));
            const bool crossfade = current != nullptr && ! current->isMidiTool() && ! inst->isMidiTool() && mode != Mode::restore;
            if (current != nullptr && (crossfade || current->isMidiTool()))
            {
                fading = current;          // a sound fades out; a MIDI tool's notes are ended
                fadePos = 0;
                fadeLength = juce::jmax (1, (int) (kFadeSeconds * juce::jmax (1.0, rate)));
                old = live;
            }
            else
            {
                toFree.push_back (live);    // nothing to fade: the old one goes now
            }
            current = inst.get();
            replayHeld = true;
            live = inst;
        }
        toFree.clear();
        restoring = {};
        updateHostDisplay (ChangeDetails().withParameterInfoChanged (true));
        if (inst->isMidiTool() && wrapperType == wrapperType_AudioUnit && juce::PluginHostType().isAbletonLive())
            setStatus (inst->name + " is open, but Ableton takes no MIDI from an Audio Unit. Use Pangea Instrument from the VST3 list.");
        else
            setStatus (inst->name + " v" + juce::String (inst->version) + (mode == Mode::swap ? " is live." : " is open."));
    }

    void assignSlots (Instance& inst, Mode mode)
    {
        auto& map = slotMaps[inst.projectId];
        struct Want { juce::String key; SlotParameter::Spec spec; int* slotOut; juce::NormalisableRange<float>* rangeOut; juce::String id; };
        std::vector<Want> wants;
        for (auto& b : inst.bindings)
            wants.push_back ({ b.key, specFor (*b.control), &b.slot, &b.range, b.control->id });
        for (size_t i = 0; i < inst.logicParams.size(); ++i)
            wants.push_back ({ inst.logicParams[i].key, specFor (inst.logic[i]), &inst.logicParams[i].slot, &inst.logicParams[i].range, inst.logic[i].id });

        std::set<int> taken, reserved;
        for (const auto& kv : map)
            reserved.insert (kv.second);
        std::vector<int> slotOf (wants.size(), -1);
        for (size_t i = 0; i < wants.size(); ++i)
            if (auto it = map.find (wants[i].key); it != map.end() && taken.count (it->second) == 0)
                taken.insert (slotOf[i] = it->second);
        for (size_t i = 0; i < wants.size(); ++i)
        {
            if (slotOf[i] >= 0)
                continue;
            int s = -1;
            for (int k = 0; k < kSlots && s < 0; ++k)
                if (reserved.count (k) == 0 && taken.count (k) == 0)
                    s = k;
            for (int k = 0; k < kSlots && s < 0; ++k)   // every slot is held: take one this version does not use
                if (taken.count (k) == 0)
                {
                    s = k;
                    for (auto it = map.begin(); it != map.end();)
                        it = (it->second == k) ? map.erase (it) : std::next (it);
                }
            if (s < 0)
                continue;   // more than 128 controls: the rest are shown but not automatable
            map[wants[i].key] = s;
            reserved.insert (s);
            taken.insert (slotOf[i] = s);
        }

        for (size_t i = 0; i < wants.size(); ++i)
        {
            const int s = slotOf[i];
            if (s < 0)
                continue;
            auto spec = wants[i].spec;
            spec.key = wants[i].key;
            auto* p = slots[(size_t) s];
            const auto before = p->getSpec();
            const float value01 = p->getValue();
            float next01 = spec.range.convertTo0to1 (spec.range.snapToLegalValue (spec.defaultReal));
            if (mode == Mode::restore)
                next01 = value01;   // the session's value, for this same version
            else if (mode == Mode::swap && before.key == spec.key && before.kind != SlotParameter::Kind::unused)
            {
                // Moved by the person: keep where it is. Still at the old default: take the new default,
                // so an edit that changes a default is heard.
                const float oldDefault01 = before.range.convertTo0to1 (before.range.snapToLegalValue (before.defaultReal));
                if (std::abs (value01 - oldDefault01) > 1.0e-4f)
                {
                    const float real = before.range.convertFrom0to1 (value01);
                    next01 = spec.range.convertTo0to1 (spec.range.snapToLegalValue (juce::jlimit (spec.range.start, spec.range.end, real)));
                }
            }
            p->assign (spec);
            if (std::abs (next01 - p->getValue()) > 1.0e-6f)
                p->setValueNotifyingHost (next01);
            *wants[i].slotOut = s;
            *wants[i].rangeOut = spec.range;
            inst.slotForId[wants[i].id] = s;
        }

        // Slots this version does not use stand for nothing now.
        std::set<int> used (taken.begin(), taken.end());
        for (int k = 0; k < kSlots; ++k)
            if (used.count (k) == 0 && slots[(size_t) k]->getSpec().kind != SlotParameter::Kind::unused)
                slots[(size_t) k]->clear();
    }

    // ---- Requests to the desktop app ---------------------------------------------------------------

    struct Pending { juce::String requestId, projectId, name; int version = 0; Mode mode = Mode::fresh; double sentAt = 0; };
    struct Restoring { juce::String projectId; int version = 0; juce::String name; };

    void request (const juce::String& projectId, int version, Mode mode, bool rebuild, const juce::String& name)
    {
        if (! safeId (projectId))
            return;
        const auto id = writeRequest ("module", projectId, version, rebuild);
        if (id.isEmpty())
            return setStatus ("Could not write to " + hostDir().getFullPathName() + ".");
        pending = { id, projectId, name, version, mode, juce::Time::getMillisecondCounterHiRes() / 1000.0 };
        const auto what = name + (version > 0 ? " v" + juce::String (version) : juce::String());
        setStatus (library.appRunning ? "Getting " + what + " from the Pangea app."
                                      : "Waiting for the Pangea app to get " + what + ". Is it open?");
    }

    juce::String writeRequest (const juce::String& type, const juce::String& projectId, int version, bool rebuild)
    {
        const auto id = juce::Uuid().toDashedString();
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("format", 1);
        obj->setProperty ("requestId", id);
        obj->setProperty ("type", type);
        if (type == "module")
        {
            obj->setProperty ("projectId", projectId);
            obj->setProperty ("version", version > 0 ? juce::var (version) : juce::var());
            obj->setProperty ("rebuild", rebuild);
        }
        obj->setProperty ("createdAt", juce::Time::getCurrentTime().toISO8601 (true));
        const auto file = hostDir().getChildFile ("requests").getChildFile (id + ".json");
        return writeAtomically (file, juce::JSON::toString (juce::var (obj))) ? id : juce::String();
    }

    void pollResponse()
    {
        if (pending.requestId.isEmpty())
            return;
        const auto file = hostDir().getChildFile ("responses").getChildFile (pending.requestId + ".json");
        if (! file.existsAsFile())
        {
            const double waited = juce::Time::getMillisecondCounterHiRes() / 1000.0 - pending.sentAt;
            if (waited > kRequestTimeoutSeconds)
            {
                hostDir().getChildFile ("requests").getChildFile (pending.requestId + ".json").deleteFile();
                const auto name = pending.name;
                pending = {};
                setStatus ("The Pangea app did not answer about " + name + ". Is it open and signed in?");
            }
            return;
        }
        const auto json = juce::JSON::parse (file.loadFileAsString());
        file.deleteFile();
        const auto was = pending;
        pending = {};
        if (! json.isObject())
            return setStatus ("The Pangea app's answer could not be read.");
        const bool ok = (bool) json.getProperty ("ok", false);
        const auto message = str (json, "message");
        if (! ok)
            return setStatus (message.isNotEmpty() ? message : "The Pangea app could not build " + was.name + ".");
        const int version = num (json, "version");
        const auto dir = juce::File (str (json, "moduleDir"));
        if (! dir.isAChildOf (hostDir().getChildFile ("modules")))
            return setStatus ("The Pangea app answered with a module outside its folder.");
        if (was.mode == Mode::swap && live != nullptr && live->projectId == was.projectId && live->version == version)
            return setStatus (live->name + " v" + juce::String (version) + " is the newest version.");
        load (dir, was.mode);
    }

    void handleAsyncUpdate() override
    {
        // A session's version: from disk when it is there, from the desktop app when it is not.
        if (restoring.projectId.isEmpty())
            return;
        const auto r = restoring;
        if (r.version > 0)
        {
            const auto dir = moduleDirFor (r.projectId, r.version);
            if (dir.getChildFile ("manifest.json").existsAsFile())
            {
                load (dir, Mode::restore);   // or, for an older format, a request to build it again
                return;
            }
        }
        request (r.projectId, r.version, Mode::restore, false, r.name.isNotEmpty() ? r.name : juce::String ("this session's plugin"));
    }

    void checkForNewer()
    {
        if (! followLatest || followHeld || live == nullptr || isBusy())
            return;
        if (const auto* e = library.find (live->projectId);
            e != nullptr && e->headVersion > live->version && e->headVersion > followAbove)
            request (live->projectId, e->headVersion, Mode::swap, false, live->name);
    }

    /** Follow from now on, with nothing held back: the member chose, refreshed or ticked Follow. */
    void releaseFollow() noexcept
    {
        followHeld = false;
        followAbove = 0;
    }

    /**
     * A song opened again with Follow on: the first library the app writes after that lists, as its
     * head, the newest version made while the song was closed. That one and older are not followed;
     * a version newer than it, made while the song is open, is. With the app closed nothing is
     * written, and the song stays on the version it was saved with.
     */
    void armFollow (juce::Time libraryWritten)
    {
        if (! followHeld || libraryWritten <= heldSince)
            return;
        const auto id = live != nullptr ? live->projectId : restoring.projectId;
        if (const auto* e = library.find (id))
        {
            followAbove = e->headVersion;
            followHeld = false;
        }
    }

    void timerCallback() override
    {
        // The library, when the desktop app wrote it again.
        const auto libFile = hostDir().getChildFile ("library.json");
        const auto modified = libFile.getLastModificationTime();
        const double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
        if (modified != libraryModified || now - libraryReadAt > 5.0)
        {
            libraryModified = modified;
            libraryReadAt = now;
            const bool hadNone = ! library.found;
            library = readLibrary (hostDir());
            if (hadNone && library.found && status.startsWith ("Open the Pangea app"))
                status = {};
            armFollow (modified);
            checkForNewer();
            sendChangeMessage();
        }
        pollResponse();

        // The old version, once the audio thread has faded it out (or has stopped calling).
        std::shared_ptr<Instance> toFree;
        {
            const juce::SpinLock::ScopedLockType l (swapLock);
            const bool audioIdle = juce::Time::getMillisecondCounterHiRes() - lastBlockMs.load() > 500.0;
            if (fading != nullptr && audioIdle)
                fading = nullptr;
            if (fading == nullptr)
                toFree = std::move (old);
        }
        toFree.reset();
    }

    void setStatus (const juce::String& s)
    {
        status = s;
        sendChangeMessage();
    }

    // ---- The audio thread ------------------------------------------------------------------------

    void endAllNotes()
    {
        const juce::SpinLock::ScopedLockType l (swapLock);
        if (current != nullptr)
            current->endAllPending = true;
    }

    Transport readTransport (int n, Instance& in)
    {
        Transport t;
        t.sampleRate = getSampleRate() > 0.0 ? getSampleRate() : in.sampleRate;
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
            {
                t.bpm = pos->getBpm().orFallback (120.0);
                t.playing = pos->getIsPlaying();
                if (auto p = pos->getPpqPosition())
                {
                    t.ppq = *p;
                    t.havePosition = true;
                }
                if (auto ts = pos->getTimeSignature(); ts && ts->numerator > 0 && ts->denominator > 0)
                    t.beatsPerBar = ts->numerator * 4.0 / ts->denominator;
            }
        if (! std::isfinite (t.bpm) || t.bpm < 1.0) t.bpm = 120.0;
        if (t.bpm > 999.0) t.bpm = 999.0;
        if (! std::isfinite (t.beatsPerBar) || t.beatsPerBar <= 0.0 || t.beatsPerBar > 64.0) t.beatsPerBar = 4.0;
        juce::ignoreUnused (n);
        return t;
    }

    // `bypassedCall` is true for a host that calls processBlockBypassed. Either way the bypass is
    // Pangea's own (PangeaBypass.h; decisions 74 and 75), as in a built plugin: an effect fades to
    // its input and an instrument to silence over 30 ms, and back the same way, after the
    // crossfade between versions. The version runs on unheard, so an effect comes back with its
    // tails as they are. An instrument takes no new notes while bypassed and its voices are ended
    // once the fade is over. A MIDI tool ends its own notes and passes on what it is sent.
    void process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, bool bypassedCall)
    {
        juce::ScopedNoDenormals noDenormals;
        lastBlockMs.store (juce::Time::getMillisecondCounterHiRes());
        const int n = buffer.getNumSamples();

        // The fade moves across every block, a version loaded or not, so that a version that comes
        // in while the plugin is bypassed is bypassed from its first sample.
        const bool bypassAsked = bypassedCall || bypassParam->get();
        bypassFader.setTarget (bypassAsked);
        const bool blending = ! bypassFader.fullyIn();
        const bool bypassOver = bypassFader.fullyBypassed();   // asked for, and the fade there before this block
        const auto ramp = bypassFader.advance (n);
        // Bypassed, the plugin takes no new notes, so none is counted as held; and once the fade is
        // over nothing is held any more, so a version that comes in now is played no old notes.
        trackHeld (midi, ! bypassAsked);
        if (! bypassAsked)
            bypassEnded = false;
        else if (bypassOver && ! bypassEnded)
        {
            bypassEnded = true;
            held.fill (0);
        }

        const juce::SpinLock::ScopedTryLockType lock (swapLock);
        if (! lock.isLocked() || current == nullptr)
        {
            // Nothing loaded (or a swap this instant): an effect lets the sound through, the others are quiet.
            if (hostKind != HostKind::effect)
                buffer.clear();
            if (hostKind == HostKind::instrument)
                midi.clear();
            return;
        }
        auto& in = *current;

        if (in.isMidiTool())
        {
            midiOut.clear();
            if (fading != nullptr)
            {
                // The old version's notes end where the new one starts.
                if (fading->isMidiTool())
                    endNotes (fading->logicNotes);
                fading = nullptr;
            }
            processMidiTool (in, buffer, midi, bypassAsked);
            replayHeld = false;
            return;
        }

        // The sidechain, kept before the version's output overwrites the buffer: both versions of a
        // crossfade read it from here (keepSidechain).
        keepSidechain (buffer, n);

        // An effect's input, kept before the version's output overwrites it: what it gives bypassed.
        const int outs = juce::jmin (buffer.getNumChannels(), getTotalNumOutputChannels());
        if (blending && hostKind == HostKind::effect)
        {
            if (dryScratch.getNumSamples() < n)
                dryScratch.setSize (dryScratch.getNumChannels(), n, false, false, true);
            const int ins = juce::jmin (buffer.getNumChannels(), getMainBusNumInputChannels());
            for (int ch = 0; ch < juce::jmin (outs, dryScratch.getNumChannels()); ++ch)
                pangea::bypass::keepDry (dryScratch.getWritePointer (ch), ch, outs, buffer.getArrayOfReadPointers(), ins, n);
        }

        const bool fadingSound = fading != nullptr && ! fading->isMidiTool();
        bool endedTool = false;
        if (fading != nullptr && fading->isMidiTool())
        {
            // A MIDI tool gave way to an instrument: its notes still end, on the MIDI out.
            midiOut.clear();
            endNotes (fading->logicNotes);
            fading = nullptr;
            endedTool = true;
        }
        if (fadingSound)
        {
            if (fadeScratch.getNumSamples() < n)
                fadeScratch.setSize (2, n, false, false, true);
            for (int ch = 0; ch < 2; ++ch)
            {
                if (ch < buffer.getNumChannels())
                    fadeScratch.copyFrom (ch, 0, buffer, ch, 0, n);
                else
                    fadeScratch.clear (ch, 0, n);
            }
            fadeMidi.clear();
            fadeMidi.addEvents (midi, 0, n, 0);
        }

        if (replayHeld && in.polyphonic)
            for (int note = 0; note < 128; ++note)
                if (held[(size_t) note] > 0)
                    in.poly->keyOn (0, note, held[(size_t) note]);
        replayHeld = false;

        // Bypassed, and the fade over: this version's voices are ended, once, by the path a MIDI all
        // notes off takes. Ended any sooner, the notes would stop inside the fade.
        if (! bypassAsked)
            in.endedForBypass = false;
        else if (bypassOver && ! in.endedForBypass)
        {
            in.endedForBypass = true;
            if (in.polyphonic)
                apply (in, { 0, 0, Event::Kind::allNotesOff, 0, 0.0f });
        }

        const auto t = readTransport (n, in);
        processSound (in, buffer, midi, t, buffer.getNumChannels(), bypassAsked);
        if (endedTool)
            midi.swapWith (midiOut);

        if (fadingSound)
        {
            juce::AudioBuffer<float> view (fadeScratch.getArrayOfWritePointers(), 2, n);
            processSound (*fading, view, fadeMidi, t, 2, bypassAsked);
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            {
                auto* outp = buffer.getWritePointer (ch);
                const auto* oldp = fadeScratch.getReadPointer (juce::jmin (ch, 1));
                for (int i = 0; i < n; ++i)
                {
                    const float g = juce::jlimit (0.0f, 1.0f, (float) (fadePos + i) / (float) fadeLength);
                    outp[i] = outp[i] * g + oldp[i] * (1.0f - g);
                }
            }
            fadePos += n;
            if (fadePos >= fadeLength)
                fading = nullptr;   // the message thread frees it
        }

        // The bypass's fade, after the crossfade between versions: sample by sample across the
        // block, between what the versions made and what a bypassed plugin gives.
        if (blending)
            for (int ch = 0; ch < outs; ++ch)
                pangea::bypass::blend (ramp, buffer.getWritePointer (ch),
                                       hostKind == HostKind::effect && ch < dryScratch.getNumChannels() ? dryScratch.getReadPointer (ch) : nullptr, n);
    }

    // The notes the host holds, for a version that comes in while they are down. `newNotes` is false
    // while the plugin is bypassed: it takes no new notes then, so none is counted as held.
    void trackHeld (const juce::MidiBuffer& midi, bool newNotes)
    {
        for (const auto metadata : midi)
        {
            const auto m = metadata.getMessage();
            if (m.isNoteOn())
            {
                if (newNotes)
                    held[(size_t) m.getNoteNumber()] = (uint8_t) juce::jmax (1, (int) m.getVelocity());
            }
            else if (m.isNoteOff())
                held[(size_t) m.getNoteNumber()] = 0;
            else if (m.isAllNotesOff() || m.isAllSoundOff())
                held.fill (0);
        }
    }

    void pushParameters (Instance& in)
    {
        for (auto& b : in.bindings)
        {
            if (b.slot < 0)
                continue;
            const float v01 = slots[(size_t) b.slot]->getValue();
            float v = b.toggle ? (v01 >= 0.5f ? 1.0f : 0.0f) : b.range.convertFrom0to1 (v01);
            if (! b.toggle && b.range.interval > 0.0f)
                v = b.range.snapToLegalValue (v);
            for (auto* z : b.zones)
                *z = v;
        }
    }

    void pushTempo (Instance& in, double bpm)
    {
        for (auto& tb : in.tempo)
        {
            const auto v = (FAUSTFLOAT) juce::jlimit ((double) tb.min, (double) tb.max, bpm);
            for (auto* z : tb.zones)
                *z = v;
        }
    }

    // [pangea:playing] holds 1 while the host's transport plays and 0 while it is stopped, and
    // [pangea:beat] the song position in beats at the start of the block (decision 123), and
    // [pangea:beatfrac] its fraction of a beat (decision 124), worked out by PangeaSong.h exactly as
    // HybridShell's pushHost does. With no host position, the beats are counted from the version's
    // first block; a host with no transport at all counts as playing (readTransport).
    void pushTransport (Instance& in, const Transport& t, int n)
    {
        const auto song = songValues (t.playing, t.havePosition, t.ppq, in.transportSamples, t.bpm, t.sampleRate);
        in.transportSamples += n;
        for (auto& tb : in.transport)
        {
            const double value = tb.what == Instance::SongValue::beat       ? song.beat
                               : tb.what == Instance::SongValue::fraction   ? song.fraction
                                                                            : song.playing;
            const auto v = (FAUSTFLOAT) juce::jlimit ((double) tb.min, (double) tb.max, value);
            for (auto* z : tb.zones)
                *z = v;
        }
    }

    // An effect or an instrument: HybridShell's processSound for the given version. `bypassAsked`
    // is Pangea's bypass: the version runs on, but its logic module is run the way it is run while
    // the transport is stopped, and it takes no new notes. The fade itself is process()'s.
    void processSound (Instance& in, juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, const Transport& t, int bufferChannels,
                       bool bypassAsked)
    {
        const int n = buffer.getNumSamples();
        // The main bus only: the sidechain's channels are fed on their own, from sideScratch.
        const int bufferIns = hostKind == HostKind::effect ? juce::jmin (bufferChannels, getMainBusNumInputChannels()) : 0;
        const int bufferOuts = juce::jmin (bufferChannels, getTotalNumOutputChannels());
        if (in.inScratch.getNumSamples() < n)
        {
            in.inScratch.setSize (in.inScratch.getNumChannels(), n, false, false, true);
            in.outScratch.setSize (in.outScratch.getNumChannels(), n, false, false, true);
        }

        pushParameters (in);
        pushTempo (in, t.bpm);
        pushTransport (in, t, n);
        gatherInputs (in, buffer, bufferIns, n);
        gatherSidechain (in, n);

        in.events.clear();
        int sequence = 0;
        if (in.code->hasLogic)
        {
            const int numLogic = runLogic (in, n, t, bypassAsked);
            if (bypassAsked)
                in.follower.forget();   // as a bypassed MIDI tool does: coming back is a start, not a jump
            for (int i = 0; i < numLogic; ++i)
            {
                const auto& e = in.logicEvents[(size_t) i];
                if (e.control >= 0 && std::isfinite (e.value))
                    in.events.push_back ({ juce::jlimit (0, n, e.offset), sequence++, Event::Kind::logic, e.control, e.value });
            }
            in.display.take (in.logicEvents.data(), numLogic);
            in.display.playing.store (in.blockPlaying, std::memory_order_relaxed);
            if (in.polyphonic)
            {
                const bool endAll = in.endAllPending.exchange (false) || in.blockJumped || ! in.blockPlaying || in.blockStopped;
                const int numNotes = notes::route (in.logicEvents.data(), numLogic, n, in.blockPlaying, endAll, in.logicNotes,
                                                   in.noteScratch.data(), (int) in.noteScratch.size());
                for (int k = 0; k < numNotes; ++k)
                {
                    const auto& note = in.noteScratch[(size_t) k];
                    in.events.push_back ({ note.offset, sequence++, note.on ? Event::Kind::noteOn : Event::Kind::noteOff,
                                           note.note, (float) note.velocity });
                }
            }
        }
        if (in.polyphonic)
            for (const auto metadata : midi)
                addMidiEvent (in, metadata.getMessage(), juce::jlimit (0, n, metadata.samplePosition), sequence++, ! bypassAsked);

        std::sort (in.events.begin(), in.events.end(), [] (const Event& x, const Event& y)
        {
            return x.offset != y.offset ? x.offset < y.offset : x.sequence < y.sequence;
        });

        int pos = 0;
        for (size_t e = 0; e <= in.events.size(); ++e)
        {
            const int sliceEnd = e < in.events.size() ? in.events[e].offset : n;
            for (int start = pos; start < sliceEnd; start += kMaxSlice)
                renderSlice (in, start, juce::jmin (kMaxSlice, sliceEnd - start));
            pos = juce::jmax (pos, sliceEnd);
            if (e < in.events.size())
                apply (in, in.events[e]);
        }

        deliverOutputs (in, buffer, bufferOuts, n);
        in.meters.capture();   // the bar graphs' values for this block: a store each, no lock (decision 91)
        midi.clear();
    }

    // A MIDI tool: HybridShell's processMidiTool for the given version.
    void processMidiTool (Instance& in, juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, bool bypassed)
    {
        const int n = buffer.getNumSamples();
        buffer.clear();
        const bool endAll = in.endAllPending.exchange (false);

        if (bypassed)
        {
            if (! in.wasBypassed || endAll)
                endNotes (in.logicNotes);
            for (const auto metadata : midi)
            {
                const auto m = metadata.getMessage();
                if (m.isNoteOn())
                    in.passNotes.set (m.getChannel(), m.getNoteNumber(), true);
                else if (m.isNoteOff())
                    in.passNotes.set (m.getChannel(), m.getNoteNumber(), false);
                else if (m.isAllNotesOff() || m.isAllSoundOff())
                    in.passNotes.clear();
                midiOut.addEvent (m, juce::jlimit (0, juce::jmax (0, n - 1), metadata.samplePosition));
            }
            in.wasBypassed = true;
            in.follower.forget();
            in.display.playing.store (false, std::memory_order_relaxed);
        }
        else
        {
            if (in.wasBypassed)
                endNotes (in.passNotes);
            in.wasBypassed = false;
            const auto t = readTransport (n, in);
            const int numLogic = runLogic (in, n, t);
            in.display.take (in.logicEvents.data(), numLogic);
            in.display.playing.store (in.blockPlaying, std::memory_order_relaxed);
            const int numNotes = notes::route (in.logicEvents.data(), numLogic, n, in.blockPlaying,
                                               endAll || in.blockJumped || ! in.blockPlaying || in.blockStopped, in.logicNotes,
                                               in.noteScratch.data(), (int) in.noteScratch.size());
            for (int k = 0; k < numNotes; ++k)
            {
                addNote (in.noteScratch[(size_t) k]);
                in.heard.push (in.noteScratch[(size_t) k]);   // for the web panel's notes, while one listens
            }
        }
        midi.swapWith (midiOut);
    }

    void addNote (const notes::Note& note)
    {
        midiOut.addEvent (note.on ? juce::MidiMessage::noteOn (note.channel, note.note, (juce::uint8) note.velocity)
                                  : juce::MidiMessage::noteOff (note.channel, note.note),
                          note.offset);
    }

    void endNotes (notes::Tracker& tracker)
    {
        tracker.forEachOn ([this] (int channel, int note) { addNote ({ 0, false, channel, note, 0 }); });
        tracker.clear();
    }

    // `asStopped` is the bypass of an effect or an instrument: the module is run the way it is run
    // while the transport is stopped, with the song carrying on underneath, so it ends its notes,
    // starts none and comes back in time (as the builder runs a bypassed module, decision 74).
    int runLogic (Instance& in, int n, const Transport& t, bool asStopped = false)
    {
        double ppq = t.ppq;
        if (! t.havePosition || ! std::isfinite (ppq))
            ppq = (double) in.freeRunningSamples * t.bpm / (60.0 * t.sampleRate);
        in.freeRunningSamples += n;
        const bool playing = t.playing && ! asStopped;
        in.blockPlaying = playing;
        in.blockJumped = in.follower.jumped (ppq, t.bpm, n, t.sampleRate, playing);

        float params[64] = {};
        for (size_t i = 0; i < in.logicParams.size() && i < 64; ++i)
        {
            const auto& lp = in.logicParams[i];
            params[i] = lp.slot >= 0 ? lp.range.snapToLegalValue (lp.range.convertFrom0to1 (slots[(size_t) lp.slot]->getValue()))
                                     : lp.init;
        }

        in.blockStopped = false;
        if (in.logicModule == nullptr || in.stopped != 0)
            return 0;
        LogicBlock block { t.sampleRate, t.bpm, ppq, n, playing, params, t.beatsPerBar };
        int count = 0;
        const int why = in.code->process (in.guard, in.logicModule, &block, in.logicEvents.data(), (int) in.logicEvents.size(), &count);
        if (why != 0)
        {
            // Dropped, and never called again until the next prepare (decision 46).
            in.stopped = why;
            in.display.stopped.store (why, std::memory_order_relaxed);
            in.blockStopped = true;
            return 0;
        }
        return juce::jlimit (0, (int) in.logicEvents.size(), count);
    }

    void gatherInputs (Instance& in, const juce::AudioBuffer<float>& buffer, int bufferIns, int n)
    {
        for (int ch = 0; ch < in.mainInputs(); ++ch)
        {
            if (bufferIns == 0)
                in.inScratch.clear (ch, 0, n);
            else if (in.mainInputs() == 1 && bufferIns >= 2)
            {
                in.inScratch.copyFrom (0, 0, buffer, 0, 0, n);          // a mono effect hears both sides
                in.inScratch.addFrom (0, 0, buffer, 1, 0, n);
                in.inScratch.applyGain (0, 0, n, 0.5f);
            }
            else if (ch < bufferIns)
                in.inScratch.copyFrom (ch, 0, buffer, ch, 0, n);
            else
                in.inScratch.clear (ch, 0, n);
        }
    }

    // The Sidechain bus's channels for this block, copied while the host has it on; none while it
    // is off, or in the instrument and the MIDI effect, which have no such bus.
    void keepSidechain (juce::AudioBuffer<float>& buffer, int n)
    {
        sideChannels = 0;
        if (hostKind != HostKind::effect)
            return;
        // Only the channels the host really passed: never past the buffer's own.
        const int passed = sidechainChannelsPassed (*this, buffer);
        if (passed == 0)
            return;
        const int first = getChannelIndexInProcessBlockBuffer (true, 1, 0);
        if (sideScratch.getNumSamples() < n)
            sideScratch.setSize (2, n, false, false, true);
        sideChannels = passed;
        for (int ch = 0; ch < sideChannels; ++ch)
            sideScratch.copyFrom (ch, 0, buffer, first + ch, 0, n);
    }

    // A version's sidechain inputs, from what keepSidechain kept, or silence. A version that
    // declares none has nothing to feed, and what is on the bus is never read.
    void gatherSidechain (Instance& in, int n)
    {
        if (in.sidechainInputs == 0)
            return;
        float* faustInputs[2] = {};
        for (int ch = 0; ch < in.sidechainInputs; ++ch)
            faustInputs[ch] = in.inScratch.getWritePointer (in.mainInputs() + ch);
        const float* bus[2] = {};
        for (int ch = 0; ch < sideChannels; ++ch)
            bus[ch] = sideScratch.getReadPointer (ch);
        pangea::fillSidechain (faustInputs, in.sidechainInputs, sideChannels > 0 ? bus : nullptr, sideChannels, n);
    }

    void deliverOutputs (Instance& in, juce::AudioBuffer<float>& buffer, int bufferOuts, int n) const
    {
        for (int ch = 0; ch < bufferOuts; ++ch)
        {
            if (in.numOutputs == 0)
                buffer.clear (ch, 0, n);
            else if (bufferOuts == 1 && in.numOutputs >= 2)
            {
                buffer.copyFrom (0, 0, in.outScratch, 0, 0, n);         // a stereo result on a mono track
                buffer.addFrom (0, 0, in.outScratch, 1, 0, n);
                buffer.applyGain (0, 0, n, 0.5f);
            }
            else
                buffer.copyFrom (ch, 0, in.outScratch, juce::jmin (ch, in.numOutputs - 1), 0, n);
        }
        for (int ch = bufferOuts; ch < buffer.getNumChannels(); ++ch)
            buffer.clear (ch, 0, n);
    }

    void renderSlice (Instance& in, int start, int count)
    {
        float* ins[kMaxFaustChannels] = {};
        float* outs[kMaxFaustChannels] = {};
        for (int ch = 0; ch < in.numInputs; ++ch)
            ins[ch] = in.inScratch.getWritePointer (ch, start);
        for (int ch = 0; ch < in.numOutputs; ++ch)
            outs[ch] = in.outScratch.getWritePointer (ch, start);
        in.engine->compute (count, ins, outs);
    }

    // `newNotes` is false while the plugin is bypassed: it takes no new notes then. Note offs, the
    // pedal and all notes off still pass, so that a note begun before the bypass can end.
    void addMidiEvent (Instance& in, const juce::MidiMessage& m, int offset, int sequence, bool newNotes)
    {
        if (m.isNoteOn())
        {
            if (newNotes)
                in.events.push_back ({ offset, sequence, Event::Kind::noteOn, m.getNoteNumber(), (float) m.getVelocity() });
        }
        else if (m.isNoteOff())
            in.events.push_back ({ offset, sequence, Event::Kind::noteOff, m.getNoteNumber(), 0.0f });
        else if (m.isAllNotesOff() || m.isAllSoundOff())
            in.events.push_back ({ offset, sequence, Event::Kind::allNotesOff, 0, 0.0f });
        else if (m.isSustainPedalOn())
            in.events.push_back ({ offset, sequence, Event::Kind::sustain, 1, 0.0f });
        else if (m.isSustainPedalOff())
            in.events.push_back ({ offset, sequence, Event::Kind::sustain, 0, 0.0f });
    }

    void apply (Instance& in, const Event& ev)
    {
        switch (ev.kind)
        {
            case Event::Kind::logic:
                if (ev.a >= 0 && ev.a < (int) in.logicZones.size())
                    for (auto* z : in.logicZones[(size_t) ev.a])
                        *z = ev.b;
                break;
            case Event::Kind::noteOn:
                if (in.sustained[(size_t) ev.a] > 0)
                    in.sustained[(size_t) ev.a] = 0;
                in.poly->keyOn (0, ev.a, (int) ev.b);
                break;
            case Event::Kind::noteOff:
                if (in.sustainDown)
                    in.sustained[(size_t) ev.a] = (uint8_t) juce::jmin (255, in.sustained[(size_t) ev.a] + 1);
                else
                    endVoice (in, ev.a);
                break;
            case Event::Kind::sustain:
                in.sustainDown = ev.a != 0;
                if (! in.sustainDown)
                    for (int pitch = 0; pitch < 128; ++pitch)
                        while (in.sustained[(size_t) pitch] > 0)
                        {
                            endVoice (in, pitch);
                            --in.sustained[(size_t) pitch];
                        }
                break;
            case Event::Kind::allNotesOff:
                in.sustained.fill (0);
                in.poly->allNotesOff (false);
                break;
        }
    }

    // A note off for the voice that plays a pitch. Faust's own keyOff prints a line when no voice
    // does, and nothing may be printed on this thread. With a bypass that is an everyday case: the
    // note off of a note the bypass ended, or of one that was never taken. So Faust is asked only
    // when a voice holds the pitch, by the test its own keyOff makes.
    void endVoice (Instance& in, int pitch)
    {
        for (auto* voice : in.poly->fVoiceTable)
            if (voice->fCurNote == pitch || (voice->fCurNote == kLegatoVoice && voice->fNextNote == pitch))
            {
                in.poly->keyOff (0, pitch, 0);
                return;
            }
    }

    // ---- State ---------------------------------------------------------------------------------

    const HostKind hostKind;
    std::array<SlotParameter*, kSlots> slots {};
    std::map<juce::String, std::map<juce::String, int>> slotMaps;   // per project: control key to slot

    // The versions. `live` is what the message thread holds for the audio thread's `current`;
    // `old` holds the one fading out until the audio thread lets it go.
    std::shared_ptr<Instance> live, old;
    mutable juce::SpinLock swapLock;
    Instance* current = nullptr;
    Instance* fading = nullptr;
    int fadePos = 0, fadeLength = 1;
    bool replayHeld = false;

    std::atomic<double> sampleRate { 48000.0 };
    std::atomic<int> maxBlock { 512 };
    std::atomic<double> lastBlockMs { 0.0 };
    juce::AudioBuffer<float> fadeScratch;   // the input, copied for the version fading out
    juce::AudioBuffer<float> sideScratch;   // the Sidechain bus, copied for the block (keepSidechain)
    int sideChannels = 0;                   // its channels this block: 0 while the bus is off
    juce::MidiBuffer fadeMidi, midiOut;
    std::array<uint8_t, 128> held {};

    // Pangea's bypass (PangeaBypass.h): the parameter after the slots, its fader, an effect's input
    // kept for the block, and whether what was held has been let go for the bypass that is on now.
    juce::AudioParameterBool* bypassParam = nullptr;
    pangea::bypass::Fader bypassFader;
    juce::AudioBuffer<float> dryScratch;
    bool bypassEnded = false;

    Library library;
    juce::Time libraryModified;
    double libraryReadAt = 0;
    Pending pending;
    Restoring restoring;
    juce::String status;
    bool followLatest = false;
    // Decision 83: a song opened again keeps its version. Held until the app's next library; then
    // only versions above followAbove are followed.
    bool followHeld = false;
    juce::Time heldSince;
    int followAbove = 0;
    bool webPanel = true;   // the component library's panel; the top bar's switch gives the shell's own
};

// ---- The window --------------------------------------------------------------------------------

/** Pangea's own colours around the panels it shows. */
struct Chrome
{
    static juce::Colour stage() { return juce::Colour (0xff1e2324); }
    static juce::Colour bar() { return juce::Colour (0xff2a3132); }
    static juce::Colour row() { return juce::Colour (0xff262c2d); }
    static juce::Colour rowOver() { return juce::Colour (0xff313a3b); }
    static juce::Colour ink() { return juce::Colour (0xffece6da); }
    static juce::Colour dim() { return juce::Colour (0xff9aa3a0); }
    static juce::Colour accent() { return juce::Colour (0xffd4a95e); }
    static juce::Font font (float size, bool bold = false)
    {
        return juce::Font (juce::FontOptions ("Helvetica Neue", size, bold ? juce::Font::bold : juce::Font::plain));
    }
};

// ---- The list's search (decision 85) ----
//
// The plugin's copy of the rule the site's library page (src/lib/catalogue/search.ts) and the
// desktop app (desktop/src/lib/search.ts) use; docs/pangea-plugin.md states it once. A change
// here is a change in three places. tools/host-check holds this copy to the cases the site's
// tests hold theirs to.
//
// The rule. A plugin's text is in three parts, the most telling first:
//   1. its category: the name, the category's other names, and those of its tags that name
//      a kind of the category (a compressor, filed under Dynamics): categoryName and keywords
//   2. its own name, its tags and its kind
//   3. its summary, its description and its owner's name
// Every word of what the person typed must start a word of that text. Each word counts as the
// number of the first part it is found in, and the plugin's rank is the sum: the lower, the better.
//
// Words: lower case; "iz" reads as "is" on both sides; anything that is not a letter, a digit or a
// hyphen parts two words. A hyphenated word in the text counts as each of its parts and as the
// parts joined (lo-fi: lo, fi, lofi); typed, it is only the parts joined. A typed word of more
// than three letters loses a last "s" (delays).

/** True where a character parts two words. Past ASCII everything is a letter but the spaces and
    the punctuation of Latin-1, General Punctuation and the CJK symbols. */
inline bool searchSeparator (juce::juce_wchar c) noexcept
{
    if (c == '-')
        return false;
    if (c < 128)
        return ! juce::CharacterFunctions::isLetterOrDigit (c);
    return (c >= 0x00a0 && c <= 0x00bf) || c == 0x00d7 || c == 0x00f7 || (c >= 0x2000 && c <= 0x206f) || (c >= 0x3000 && c <= 0x303f);
}

/** A text as its pieces, each piece as the parts between its hyphens. */
inline std::vector<juce::StringArray> searchChunks (const juce::String& text)
{
    std::vector<juce::StringArray> out;
    juce::String piece;
    const auto flush = [&out, &piece]
    {
        juce::StringArray parts;
        parts.addTokens (piece, "-", "");
        parts.removeEmptyStrings();
        if (! parts.isEmpty())
            out.push_back (parts);
        piece.clear();
    };
    const auto lower = text.toLowerCase().replace ("iz", "is");
    for (auto t = lower.getCharPointer(); ! t.isEmpty(); ++t)
    {
        if (searchSeparator (*t))
            flush();
        else
            piece += *t;
    }
    flush();
    return out;
}

/** The words of a plugin's text. */
inline juce::StringArray searchTextWords (const juce::String& text)
{
    juce::StringArray words;
    for (const auto& parts : searchChunks (text))
    {
        words.addArray (parts);
        if (parts.size() > 1)
            words.add (parts.joinIntoString (""));
    }
    return words;
}

/** The words of what a person typed. */
inline juce::StringArray searchQueryWords (const juce::String& query)
{
    juce::StringArray words;
    for (const auto& parts : searchChunks (query))
    {
        auto word = parts.joinIntoString ("");
        if (word.length() > 3 && word.endsWithChar ('s'))
            word = word.dropLastCharacters (1);
        words.add (word);
    }
    return words;
}

/** The rank of a plugin for the typed words, the lower the better; -1 when a word is nowhere. */
inline int searchRank (const juce::StringArray& words, const std::array<juce::String, 3>& parts)
{
    if (words.isEmpty())
        return 0;
    const std::array<juce::StringArray, 3> text { searchTextWords (parts[0]), searchTextWords (parts[1]), searchTextWords (parts[2]) };
    int rank = 0;
    for (const auto& word : words)
    {
        int found = -1;
        for (int part = 0; part < 3 && found < 0; ++part)
            for (const auto& has : text[(size_t) part])
                if (has.startsWith (word))
                {
                    found = part;
                    break;
                }
        if (found < 0)
            return -1;
        rank += found + 1;
    }
    return rank;
}

/** An entry's text in the three parts of the rule. */
inline std::array<juce::String, 3> searchParts (const Entry& e)
{
    return { e.categoryName + " " + e.keywords.joinIntoString (" "),
             e.name + " " + e.tags.joinIntoString (" ") + " " + kindWord (e.kind),
             e.summary + " " + e.description + " " + e.owner };
}

/** What the list shows for what was typed and the category chosen ("" for every category): what
    this plugin opens first, then the best match first, then the app's order. */
inline std::vector<Entry> listedEntries (const Library& lib, HostKind host, const juce::String& query, const juce::String& category)
{
    struct Ranked
    {
        const Entry* entry;
        int rank;
    };
    const auto words = searchQueryWords (query);
    std::vector<Ranked> ranked;
    for (const auto& e : lib.plugins)
    {
        if (category.isNotEmpty() && e.category != category)
            continue;
        if (const int rank = searchRank (words, searchParts (e)); rank >= 0)
            ranked.push_back ({ &e, rank });
    }
    std::stable_sort (ranked.begin(), ranked.end(), [host] (const Ranked& a, const Ranked& b)
    {
        const bool openA = canOpen (host, a.entry->kind), openB = canOpen (host, b.entry->kind);
        return openA != openB ? openA : a.rank < b.rank;
    });
    std::vector<Entry> out;
    for (const auto& r : ranked)
        out.push_back (*r.entry);
    return out;
}

/** A category in the list's menu: its id, the name people read, and how many plugins it holds. */
struct CategoryCount
{
    juce::String id, name;
    int count = 0;
    bool operator== (const CategoryCount& other) const { return id == other.id && name == other.name && count == other.count; }
};

/** The categories of the plugins that match what was typed, by name, with how many each holds, so
    that each number is what choosing that category would show. The category chosen stays in the
    menu when it holds nothing, so that it can be left. */
inline std::vector<CategoryCount> listedCategories (const Library& lib, const juce::String& query, const juce::String& chosen)
{
    const auto words = searchQueryWords (query);
    std::vector<CategoryCount> out;
    for (const auto& e : lib.plugins)
    {
        if (e.category.isEmpty() || searchRank (words, searchParts (e)) < 0)
            continue;
        auto held = std::find_if (out.begin(), out.end(), [&e] (const CategoryCount& c) { return c.id == e.category; });
        if (held != out.end())
            ++held->count;
        else
            out.push_back ({ e.category, e.categoryName.isNotEmpty() ? e.categoryName : e.category, 1 });
    }
    if (chosen.isNotEmpty() && std::none_of (out.begin(), out.end(), [&chosen] (const CategoryCount& c) { return c.id == chosen; }))
    {
        juce::String name = chosen;
        for (const auto& e : lib.plugins)
            if (e.category == chosen && e.categoryName.isNotEmpty())
                name = e.categoryName;
        out.push_back ({ chosen, name, 0 });
    }
    std::sort (out.begin(), out.end(), [] (const CategoryCount& a, const CategoryCount& b) { return a.name.compareIgnoreCase (b.name) < 0; });
    return out;
}

/** The line under a plugin's name in the list: where it is filed, or its kind when it is not, its
    version, then its summary, or its description when it has no summary. */
inline juce::String listDetail (const Entry& e)
{
    auto detail = (e.categoryName.isNotEmpty() ? e.categoryName : kindWord (e.kind))
                  + (e.headVersion > 0 ? ", v" + juce::String (e.headVersion) : juce::String());
    const auto& words = e.summary.isNotEmpty() ? e.summary : e.description;
    if (words.isNotEmpty())
        detail += ". " + words;
    return detail;
}

class HostEditor : public juce::AudioProcessorEditor,
                   private juce::ChangeListener,
                   private juce::ComponentListener,
                   private juce::ListBoxModel
{
public:
    static constexpr int barHeight = 44, footHeight = 26;
    /** The strip under the bar while the list shows: the search box and the category menu (decision 85). */
    static constexpr int findHeight = 38;

    explicit HostEditor (Processor& p) : AudioProcessorEditor (p), proc (p)
    {
        setOpaque (true);
        for (auto* b : { &backButton, &refreshButton, &listButton })
        {
            b->setColour (juce::TextButton::buttonColourId, Chrome::row());
            b->setColour (juce::TextButton::textColourOffId, Chrome::ink());
            addAndMakeVisible (*b);
        }
        backButton.onClick = [this] { showList (true); };
        refreshButton.onClick = [this] { proc.refresh(); };
        listButton.onClick = [this] { proc.refreshLibrary(); };
        follow.setColour (juce::ToggleButton::textColourId, Chrome::ink());
        follow.setColour (juce::ToggleButton::tickColourId, Chrome::accent());
        follow.setToggleState (proc.followsLatest(), juce::dontSendNotification);
        follow.onClick = [this] { proc.setFollowLatest (follow.getToggleState()); };
        addAndMakeVisible (follow);
        webToggle.setColour (juce::ToggleButton::textColourId, Chrome::ink());
        webToggle.setColour (juce::ToggleButton::tickColourId, Chrome::accent());
        webToggle.setToggleState (proc.usesWebPanel(), juce::dontSendNotification);
        webToggle.onClick = [this] { proc.setWebPanel (webToggle.getToggleState()); };
        addAndMakeVisible (webToggle);

        list.setModel (this);
        list.setRowHeight (54);
        list.setColour (juce::ListBox::backgroundColourId, Chrome::stage());
        list.setOutlineThickness (0);
        addAndMakeVisible (list);

        // The search box and the category menu over the list (decision 85). The box never takes the
        // keyboard by itself: in a DAW the keys are the DAW's until the person presses in the box.
        search.setFont (Chrome::font (14.0f));
        search.setTextToShowWhenEmpty ("Search by name, category or tag", Chrome::dim());
        search.setColour (juce::TextEditor::backgroundColourId, Chrome::row());
        search.setColour (juce::TextEditor::textColourId, Chrome::ink());
        search.setColour (juce::TextEditor::outlineColourId, Chrome::rowOver());
        search.setColour (juce::TextEditor::focusedOutlineColourId, Chrome::accent());
        search.setColour (juce::CaretComponent::caretColourId, Chrome::ink());
        search.setIndents (8, 6);
        search.onTextChange = [this] { refreshList(); };
        // Return hands the keys back to the DAW with the search kept; Escape also empties the box.
        search.onReturnKey = [this] { juce::Component::unfocusAllComponents(); };
        search.onEscapeKey = [this]
        {
            search.setText ({}, juce::dontSendNotification);
            juce::Component::unfocusAllComponents();
            refreshList();
        };
        addAndMakeVisible (search);
        categoryBox.setLookAndFeel (&menuLook);
        categoryBox.setColour (juce::ComboBox::backgroundColourId, Chrome::row());
        categoryBox.setColour (juce::ComboBox::textColourId, Chrome::ink());
        categoryBox.setColour (juce::ComboBox::outlineColourId, Chrome::rowOver());
        categoryBox.setColour (juce::ComboBox::arrowColourId, Chrome::dim());
        categoryBox.onChange = [this]
        {
            const int index = categoryBox.getSelectedId() - 2;   // 1 is every category
            chooseCategory (index >= 0 && index < (int) categories.size() ? categories[(size_t) index].id : juce::String());
        };
        addAndMakeVisible (categoryBox);

        proc.addChangeListener (this);
        setResizable (true, false);
        setResizeLimits (420, 200, 2000, 1600);   // low enough for a short panel: a web panel can be under 200 high
        showList (proc.loaded() == nullptr);
        if (proc.loaded() != nullptr)
            showPanel();
    }

    ~HostEditor() override
    {
        proc.removeChangeListener (this);
        categoryBox.setLookAndFeel (nullptr);
        panel.reset();
    }

    /** The web panel, when that is what shows (tools/host-check talks to its page). */
    pangea::WebEditor* webPanel() const noexcept { return panelIsWeb ? static_cast<pangea::WebEditor*> (panel.get()) : nullptr; }
    /** The natively drawn panel, when that is what shows (tools/host-check presses its bypass switch). */
    pangea::Editor* nativePanel() const noexcept { return panelIsWeb ? nullptr : dynamic_cast<pangea::Editor*> (panel.get()); }

    // What the search box and the category menu do, for tools/host-check to press as a person would.
    /** Types into the search box, as its keys do. */
    void typeInSearch (const juce::String& text)
    {
        // The box tells of a change through the message loop; here the list is read again at once.
        search.setText (text, juce::dontSendNotification);
        refreshList();
    }
    /** Chooses a category in the menu by its id; "" is every category. */
    void chooseCategory (const juce::String& id)
    {
        chosenCategory = id;
        refreshList();
    }
    /** The names in the list, in the order shown. */
    juce::StringArray listedNames() const
    {
        juce::StringArray names;
        for (const auto& e : shown)
            names.add (e.name);
        return names;
    }
    /** The category menu's lines, in order, the first being every category. */
    juce::StringArray categoryMenu() const
    {
        juce::StringArray lines;
        for (int i = 0; i < categoryBox.getNumItems(); ++i)
            lines.add (categoryBox.getItemText (i));
        return lines;
    }
    /** The menu line that shows as chosen. */
    juce::String categoryShown() const { return categoryBox.getText(); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Chrome::stage());
        auto bar = getLocalBounds().removeFromTop (barHeight);
        g.setColour (Chrome::bar());
        g.fillRect (bar);
        g.setColour (Chrome::ink());
        auto text = bar.reduced (14, 0);
        if (listShown)
        {
            g.setFont (Chrome::font (18.0f, true));
            g.drawText (pluginName (proc.kind()), text, juce::Justification::centredLeft, false);
        }
        else if (auto inst = proc.loaded())
        {
            text.removeFromLeft (backButton.getRight());
            g.setFont (Chrome::font (16.0f, true));
            g.drawText (inst->name, text.removeFromLeft (juce::jmin (260, text.getWidth())), juce::Justification::centredLeft, true);
        }
        auto foot = getLocalBounds().removeFromBottom (footHeight);
        g.setColour (Chrome::bar());
        g.fillRect (foot);
        g.setColour (Chrome::dim());
        g.setFont (Chrome::font (12.5f));
        g.drawText (footText(), foot.reduced (12, 0), juce::Justification::centredLeft, true);

    }

    /** The words for a list with nothing in it. They are drawn over the list, which fills its own
        background: drawn under it, as they were before decision 85, they could not be seen. */
    void paintOverChildren (juce::Graphics& g) override
    {
        if (! listShown)
            return;
        juce::String words;
        if (proc.getLibrary().plugins.empty())
            words = emptyText();
        else if (shown.empty())
            words = "Nothing matches.";   // there are plugins, and none is what was typed or in the category chosen
        if (words.isEmpty())
            return;
        g.setColour (Chrome::dim());
        g.setFont (Chrome::font (15.0f));
        g.drawFittedText (words, list.getBounds().reduced (40, 20), juce::Justification::centred, 4);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto bar = area.removeFromTop (barHeight).reduced (8, 7);
        area.removeFromBottom (footHeight);
        if (listShown)
        {
            listButton.setBounds (bar.removeFromRight (110));
            auto find = area.removeFromTop (findHeight).reduced (10, 5);
            categoryBox.setBounds (find.removeFromRight (juce::jmin (200, find.getWidth() / 2)));
            find.removeFromRight (8);
            search.setBounds (find);
            list.setBounds (area);
        }
        else
        {
            backButton.setBounds (bar.removeFromLeft (84));
            refreshButton.setBounds (bar.removeFromRight (84));
            bar.removeFromRight (8);
            follow.setBounds (bar.removeFromRight (176));
            bar.removeFromRight (4);
            webToggle.setBounds (bar.removeFromRight (112));
            if (panel != nullptr && panelIsWeb)
            {
                // A web view cannot be scaled as a picture is: it keeps its own size and the page scales itself.
                panel->setBounds (area.withSizeKeepingCentre (panelW, panelH));
            }
            else if (panel != nullptr)
            {
                // The panel keeps its shape and fills the space it has.
                const double scale = juce::jmin ((double) area.getWidth() / panelW, (double) area.getHeight() / panelH);
                const int w = juce::roundToInt (panelW * scale), h = juce::roundToInt (panelH * scale);
                panel->setBounds (area.withSizeKeepingCentre (w, h));
            }
        }
    }

private:
    Processor& proc;
    juce::TextButton backButton { "Plugins" }, refreshButton { "Refresh" }, listButton { "Refresh list" };
    juce::ToggleButton follow { "Follow new versions" }, webToggle { "Web panel" };
    juce::ListBox list;
    juce::TextEditor search;
    /** The category menu and its list in Pangea's own type and colours. Declared before the menu, which holds it. */
    struct MenuLook : juce::LookAndFeel_V4
    {
        MenuLook()
        {
            setColour (juce::PopupMenu::backgroundColourId, Chrome::bar());
            setColour (juce::PopupMenu::textColourId, Chrome::ink());
            setColour (juce::PopupMenu::highlightedBackgroundColourId, Chrome::rowOver());
            setColour (juce::PopupMenu::highlightedTextColourId, Chrome::ink());
        }
        juce::Font getComboBoxFont (juce::ComboBox&) override { return Chrome::font (14.0f); }
        juce::Font getPopupMenuFont() override { return Chrome::font (14.0f); }
    };
    MenuLook menuLook;
    juce::ComboBox categoryBox;
    /** What the list shows now, as copies: the library can change under a row that is being drawn. */
    std::vector<Entry> shown;
    /** The category menu's lines after the first, and the category chosen ("" for every category). */
    std::vector<CategoryCount> categories;
    juce::String chosenCategory;
    std::unique_ptr<juce::AudioProcessorEditor> panel;   // pangea::Editor, or pangea::WebEditor
    std::shared_ptr<Instance> panelInstance;   // keeps what the panel points into alive
    int panelW = 720, panelH = 400;
    bool listShown = true, panelIsWeb = false;

    /** The web panel draws every kind now: a logic module's controls and its playheads too. */
    bool wantsWeb (const Instance&) const { return proc.usesWebPanel(); }

    /** Reads the list again from the library, the search box and the category chosen, and the
        category menu with it. Called when any of the three changes. */
    void refreshList()
    {
        const auto& lib = proc.getLibrary();
        const auto query = search.getText();
        shown = listedEntries (lib, proc.kind(), query, chosenCategory);
        auto menu = listedCategories (lib, query, chosenCategory);
        int matching = 0;
        {
            const auto words = searchQueryWords (query);
            for (const auto& e : lib.plugins)
                if (searchRank (words, searchParts (e)) >= 0)
                    ++matching;
        }
        // The menu is built again only when its lines changed, so that one open on screen is left alone.
        const auto all = "All categories (" + juce::String (matching) + ")";
        if (menu != categories || categoryBox.getNumItems() != (int) menu.size() + 1 || categoryBox.getItemText (0) != all)
        {
            categories = std::move (menu);
            categoryBox.clear (juce::dontSendNotification);
            categoryBox.addItem (all, 1);
            for (size_t i = 0; i < categories.size(); ++i)
                categoryBox.addItem (categories[i].name + " (" + juce::String (categories[i].count) + ")", (int) i + 2);
        }
        int chosenId = 1;
        for (size_t i = 0; i < categories.size(); ++i)
            if (categories[i].id == chosenCategory)
                chosenId = (int) i + 2;
        categoryBox.setSelectedId (chosenId, juce::dontSendNotification);
        // A menu with one line has nothing to choose from: a library where nothing is filed yet.
        categoryBox.setVisible (listShown && ! categories.empty());
        list.updateContent();
        list.repaint();
        repaint();
    }

    juce::String emptyText() const
    {
        const auto& lib = proc.getLibrary();
        if (! lib.found)
            return "Open the Pangea app and sign in. Your plugins will show here.";
        if (! lib.signedIn)
            return "Sign in to the Pangea app to see your plugins here.";
        if (lib.error.isNotEmpty())
            return lib.error;
        return "You have no plugins yet. Make one on Pangea and it will show here.";
    }

    juce::String footText() const
    {
        const auto status = proc.getStatus();
        if (status.isNotEmpty())
            return status;
        const auto& lib = proc.getLibrary();
        if (! lib.found || ! lib.appRunning)
            return "The Pangea app is not running. Plugins already fetched still open.";
        if (auto inst = proc.loaded())
        {
            if (const auto* e = lib.find (inst->projectId); e != nullptr && e->headVersion > inst->version)
                return "v" + juce::String (e->headVersion) + " is on Pangea. Refresh to hear it.";
            return inst->name + " v" + juce::String (inst->version);
        }
        return lib.member.isNotEmpty() ? "Signed in as " + lib.member : juce::String();
    }

    void showList (bool shouldShow)
    {
        listShown = shouldShow;
        list.setVisible (shouldShow);
        search.setVisible (shouldShow);
        categoryBox.setVisible (shouldShow && ! categories.empty());
        listButton.setVisible (shouldShow);
        backButton.setVisible (! shouldShow);
        refreshButton.setVisible (! shouldShow);
        follow.setVisible (! shouldShow);
        webToggle.setVisible (! shouldShow);
        if (panel != nullptr)
            panel->setVisible (! shouldShow);
        if (shouldShow)
        {
            refreshList();
            if (getWidth() < 520)
                setSize (600, 480);
        }
        resized();
        repaint();
    }

    void showPanel()
    {
        auto inst = proc.loaded();
        if (inst == nullptr)
            return;
        const bool web = wantsWeb (*inst);
        if (inst != panelInstance || web != panelIsWeb || panel == nullptr)
        {
            panel.reset();
            panelInstance = inst;
            panelIsWeb = web;
            auto* procPtr = &proc;
            Instance* raw = inst.get();
            ParamSource source ([procPtr, raw] (const juce::String& id) -> juce::RangedAudioParameter*
            {
                if (auto it = raw->slotForId.find (id); it != raw->slotForId.end())
                    return procPtr->slot (it->second);
                return nullptr;
            });
            if (web)
            {
                // The web view panel: the component library draws the plugin; it says how tall it is.
                auto webEditor = std::make_unique<pangea::WebEditor> (proc, source, inst->collector.controls, inst->name,
                                                                      inst->uiJson.getData(), inst->uiJson.getSize(),
                                                                      inst->logic, &inst->display, inst->credit, &inst->meters,
                                                                      inst->isMidiTool() ? &inst->heard : nullptr);
                webEditor->setResizable (false, false);
                webEditor->addComponentListener (this);
                panelW = webEditor->getWidth();
                panelH = webEditor->getHeight();
                panel = std::move (webEditor);
            }
            else
            {
                auto native = std::make_unique<pangea::Editor> (proc, source, inst->collector.root, inst->collector.controls,
                                                                inst->name, inst->uiJson.getData(), inst->uiJson.getSize(),
                                                                inst->logic, &inst->display);
                native->setResizable (false, false);   // the window resizes; the panel follows it
                panelW = juce::jmax (1, native->getNaturalWidth());
                panelH = juce::jmax (1, native->getNaturalHeight());
                panel = std::move (native);
            }
            addAndMakeVisible (*panel);
        }
        fitWindow();
        showList (false);
    }

    void fitWindow()
    {
        const double scale = panelIsWeb ? 1.0 : juce::jmin (1.0, 820.0 / panelH);
        setSize (juce::jmax (560, juce::roundToInt (panelW * scale)), juce::roundToInt (panelH * scale) + barHeight + footHeight);
    }

    // The web panel sizes itself once its page has drawn: the window follows.
    void componentMovedOrResized (juce::Component& c, bool, bool wasResized) override
    {
        if (! wasResized || &c != panel.get() || ! panelIsWeb)
            return;
        if (c.getWidth() == panelW && c.getHeight() == panelH)
            return;
        panelW = c.getWidth();
        panelH = c.getHeight();
        if (! listShown)
            fitWindow();
        resized();
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        follow.setToggleState (proc.followsLatest(), juce::dontSendNotification);
        webToggle.setToggleState (proc.usesWebPanel(), juce::dontSendNotification);
        const auto inst = proc.loaded();
        if (inst != nullptr && (inst != panelInstance || (! listShown && wantsWeb (*inst) != panelIsWeb)))
            showPanel();   // a version was chosen or came in, or the panel changed: show it
        refreshList();   // the library may have changed: the list, and the category menu with it
    }

    // ---- The list ----

    int getNumRows() override { return (int) shown.size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (row < 0 || row >= (int) shown.size())
            return;
        const auto& e = shown[(size_t) row];
        const bool openable = canOpen (proc.kind(), e.kind) && e.mine && e.headVersion > 0;
        const auto inst = proc.loaded();
        const bool isLoaded = inst != nullptr && inst->projectId == e.projectId;
        auto box = juce::Rectangle<int> (0, 0, width, height).reduced (10, 4);
        g.setColour (selected || isLoaded ? Chrome::rowOver() : Chrome::row());
        g.fillRoundedRectangle (box.toFloat(), 6.0f);
        if (isLoaded)
        {
            g.setColour (Chrome::accent());
            g.fillRoundedRectangle (box.removeFromLeft (4).toFloat(), 2.0f);
        }
        box.reduce (14, 6);
        const float alpha = openable ? 1.0f : 0.45f;

        juce::String right;
        if (proc.isBusy() && proc.busyProject() == e.projectId)
            right = "Getting it";
        else if (isLoaded)
            right = "Open, v" + juce::String (inst->version);
        else if (! canOpen (proc.kind(), e.kind))
            right = opensIn (e.kind);
        else if (! e.mine)
            right = "Shared by " + (e.owner.isNotEmpty() ? e.owner : juce::String ("another member"));
        else if (e.headVersion == 0)
            right = "No version yet";
        auto rightArea = box.removeFromRight (juce::jmin (220, box.getWidth() / 2));
        g.setColour (Chrome::dim().withMultipliedAlpha (isLoaded ? 1.0f : alpha));
        g.setFont (Chrome::font (12.5f));
        g.drawText (right, rightArea, juce::Justification::centredRight, true);

        g.setColour (Chrome::ink().withMultipliedAlpha (alpha));
        g.setFont (Chrome::font (15.5f, true));
        g.drawText (e.name, box.removeFromTop (box.getHeight() / 2), juce::Justification::bottomLeft, true);
        g.setColour (Chrome::dim().withMultipliedAlpha (alpha));
        g.setFont (Chrome::font (12.5f));
        g.drawText (listDetail (e), box, juce::Justification::topLeft, true);
    }

    void listBoxItemClicked (int row, const juce::MouseEvent&) override
    {
        if (row < 0 || row >= (int) shown.size())
            return;
        const auto id = shown[(size_t) row].projectId;
        if (auto inst = proc.loaded(); inst != nullptr && inst->projectId == id)
        {
            showPanel();
            proc.choose (id);   // and a newer version, if there is one
            return;
        }
        proc.choose (id);
    }

    void selectedRowsChanged (int) override {}
};

inline juce::AudioProcessorEditor* Processor::createEditor() { return new HostEditor (*this); }

} // namespace pangea::host
