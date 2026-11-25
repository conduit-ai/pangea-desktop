// Pangea native shell. One JUCE AudioProcessor, written once by the platform, that wraps:
//   - a Faust sound module (FaustDsp.h, generated from the project's dsp.dsp as class mydsp)
//     as an effect (one mydsp) or as an instrument (Faust's own mydsp_poly, one voice per note)
//   - for an instrument whose code defines `effect`, the stage its voices share, that effect
//     section too (FaustEffect.h, class mydsp_effect), run on the mixed voices (decision 73)
//   - optionally, a C++ logic module (the project's logic.cpp, see logic_api.h)
//   - or, for a MIDI tool (docs/midi-tools.md), a logic module alone: no sound, its notes go
//     out as MIDI. The same source builds both of a MIDI tool's plugins, the instrument with
//     MIDI out (AU aumu and VST3) and the AU MIDI effect (aumi, JucePlugin_IsMidiEffect).
// MIDI notes and logic events are applied between slices of the block so that every change
// lands on its exact sample.
//
// An effect whose code declares a sidechain (`declare sidechain "stereo";` or "mono") has a second
// input bus, Sidechain, off until the host routes something to it, and its Faust inputs after the
// sound's two are fed from that bus, or with silence (docs/sidechain.md). Without that line
// the buses are exactly what they were.
//
// Every plugin has one bypass, and it is Pangea's, not the project's (PangeaBypass.h; decisions 74
// and 75): one parameter, last in the list, returned from getBypassParameter and acted on here in
// processBlock with a 30 ms fade. An effect fades to its input, an instrument to silence with
// its notes ended, and a MIDI tool ends its own notes and passes on the notes it is sent.
//
// Compile-time switches, set by CMake on this file only:
//   PANGEA_NAME          the plugin name shown in the host
//   PANGEA_IS_SYNTH      1 for an instrument (MIDI in, polyphony when the Faust code asks)
//   PANGEA_IS_MIDI_TOOL  1 for a MIDI tool
//   HAS_LOGIC            1 when the target also compiles platform/logic_native.cpp, the logic module
//   HAS_EFFECT           1 when the instrument has an effect section (tools/build-effect.sh wrote FaustEffect.h)
//   PANGEA_RENDER_TOOL   1 in pangea-render only: adds the hooks the offline checks use
// The project's ui.json arrives as data: PangeaUiData.h (from juce_add_binary_data) holds its bytes,
// and the editor reads and checks them when it opens.

// The logic contract first, and the platform maths in it, with no floating-point contraction: clang
// would otherwise fuse a * b + c into one multiply-add on Apple silicon, which rounds differently
// from WebAssembly (no fused instructions), and a velocity that lands on a half can then round the
// other way in the plugin and in the browser. The module itself is not in this file: it is compiled
// on its own, in platform/logic_native.cpp, with the same rounding and the step budget
// (logic_native.flags, PangeaLogicGuard.h; decision 46), and reached through PangeaLogicRun.h.
#if defined (__clang__)
 #pragma clang fp contract(off)
#endif
#include "logic_api.h"
#if defined (__clang__)
 #pragma clang fp contract(on)
#endif
#if HAS_LOGIC
 #include "PangeaLogicRun.h"
#endif

#include <juce_audio_processors/juce_audio_processors.h>

#include <faust/gui/UI.h>
#include <faust/gui/meta.h>
#include <faust/dsp/dsp.h>
#include "FaustDsp.h"
#if HAS_EFFECT
 #include "FaustEffect.h"
#endif
#include <faust/gui/MapUI.h>
#include <faust/dsp/poly-dsp.h>

#include "PangeaModel.h"
#include "PangeaSong.h"
#include "PangeaMeters.h"
#include "PangeaNotes.h"
#include "PangeaBypass.h"
#include "PangeaEditor.h"
#include "PangeaUiData.h"
#if PANGEA_WEB_EDITOR
 #include "PangeaWebEditor.h"
#endif
#if HAS_LOGIC
 #include "PangeaLogicGuard.h"
#endif

#ifndef PANGEA_NAME
 #define PANGEA_NAME "Pangea"
#endif
#ifndef PANGEA_IS_SYNTH
 #define PANGEA_IS_SYNTH 0
#endif
#ifndef PANGEA_IS_MIDI_TOOL
 #define PANGEA_IS_MIDI_TOOL 0
#endif
#ifndef HAS_LOGIC
 #define HAS_LOGIC 0
#endif
#ifndef HAS_EFFECT
 #define HAS_EFFECT 0
#endif
#ifndef PANGEA_RENDER_TOOL
 #define PANGEA_RENDER_TOOL 0
#endif
// JUCE defines this on each plugin target: 1 for a MIDI tool's AU MIDI effect, 0 elsewhere.
// pangea-render is not a plugin target, so it renders a MIDI tool as the instrument with MIDI out.
#if defined (JucePlugin_IsMidiEffect) && JucePlugin_IsMidiEffect
 #define PANGEA_AS_MIDI_EFFECT 1
#else
 #define PANGEA_AS_MIDI_EFFECT 0
#endif
#if PANGEA_IS_MIDI_TOOL && ! HAS_LOGIC
 #error "a MIDI tool is its logic module: build.sh refuses one without logic.cpp"
#endif

// poly-dsp.h brings in Faust's GUI.h, which leaves these two statics to the architecture file.
std::list<GUI*> GUI::fGuiList;
ztimedmap GUI::gTimedZoneMap;

namespace
{

constexpr int kMaxFaustChannels = 8;
constexpr int kMaxSlice = 4096;   // mydsp_poly mixes through a fixed 4096-frame buffer
// Bypassed, an effect gives its input. An instrument has no input to give and is silent.
constexpr bool kBypassGivesInput = PANGEA_IS_SYNTH == 0 && PANGEA_IS_MIDI_TOOL == 0;

// Everything the shell learns from the Faust code: channel counts, polyphony, the control tree.
struct Model
{
    pangea::ControlCollector collector;
    pangea::MetaCollector meta;
    int numInputs = 0, numOutputs = 0, numVoices = 0;
    int sidechainInputs = 0;   // the last of numInputs, fed from the Sidechain bus
    bool polyphonic = false;

    Model()
    {
        mydsp probe;
        probe.metadata (&meta);
        probe.buildUserInterface (&collector);
        numInputs = probe.getNumInputs();
        numOutputs = probe.getNumOutputs();
        sidechainInputs = sidechainOf (probe);
        numVoices = juce::jlimit (0, 64, meta.numVoices());
       #if PANGEA_IS_SYNTH
        polyphonic = numVoices > 1 && collector.hasVoiceControls();
       #endif
       #if HAS_EFFECT
        // The effect section: its controls join the voice's, and what it gives is what the plugin
        // gives. Made on the heap, because an effect holds its delay lines.
        {
            const auto effect = std::make_unique<mydsp_effect>();
            collector.beginEffect();
            effect->buildUserInterface (&collector);
            numOutputs = effect->getNumOutputs();
        }
       #endif
        if (! polyphonic)
            for (auto& c : collector.controls)
                c.voiceDriven = false;   // a mono instrument keeps freq, gain and gate as ordinary parameters
    }

    /** The sound's own inputs: all of them but the sidechain's. */
    int mainInputs() const { return numInputs - sidechainInputs; }

    /**
     * How many of the code's inputs are a sidechain: what it declares, for an effect whose inputs
     * are the sound's two and then exactly those. Anything else has none, and its buses are as
     * they always were. Read from a fresh instance, so makeBuses can ask before the model exists.
     */
    static int sidechainOf (mydsp& probe)
    {
        if (PANGEA_IS_SYNTH || PANGEA_IS_MIDI_TOOL)
            return 0;
        pangea::MetaCollector m;
        probe.metadata (&m);
        return pangea::sidechainInputsOf (m.sidechainChannels(), probe.getNumInputs());
    }
};

struct Event
{
    enum class Kind { logic, noteOn, noteOff, allNotesOff, sustain };
    int offset = 0;
    int sequence = 0;
    Kind kind = Kind::logic;
    int a = 0;       // logic: control index; notes: pitch; sustain: 1 or 0
    float b = 0;     // logic: value; notes: velocity
};

// The logic module's controls, read once (the hints, the keys, the bars counted per group).
std::vector<pangea::LogicControl> logicControls()
{
   #if HAS_LOGIC
    return pangea::describeLogic (pangea::logic::params(), pangea::logic::numParams());
   #else
    return {};
   #endif
}

} // namespace

class PangeaProcessor : public juce::AudioProcessor
{
public:
    PangeaProcessor()
        : AudioProcessor (makeBuses()),
          logic (logicControls()),
          apvts (*this, nullptr, "PARAMETERS", buildLayout (model, logic))
    {
        bypassParameter = apvts.getParameter (pangea::bypass::parameterId);
        bypassValue = apvts.getRawParameterValue (pangea::bypass::parameterId);
        jassert (bypassParameter != nullptr && bypassValue != nullptr);

        dsp* voices = nullptr;
        if (model.polyphonic)
        {
            poly = new mydsp_poly (new mydsp(), model.numVoices, true, false);
            voices = poly;
            for (auto* voice : poly->fVoiceTable)
                voiceMaps.push_back (voice);   // each dsp_voice is a MapUI over its own zones
        }
        else
        {
            voices = new mydsp();
            voices->buildUserInterface (&monoMap);
            voiceMaps.push_back (&monoMap);
        }
       #if ! PANGEA_IS_MIDI_TOOL
        // The tree the web panel finds the meters in, and the bar graphs it taps (PangeaMeters.h,
        // decision 91). An instrument's voice is in the tree and never tapped; what plays is walked
        // from the instances that run, so that each tap points into the sound code the audio thread
        // runs. A MIDI tool has no meters.
        pangea::MeterTree meterTree;
        #if PANGEA_IS_SYNTH
        {
            const auto voiceProbe = std::make_unique<mydsp>();
            meterTree.beginStage (false);
            voiceProbe->buildUserInterface (&meterTree);
        }
        #else
        meterTree.beginStage (true);
        voices->buildUserInterface (&meterTree);
        #endif
       #endif
       #if HAS_EFFECT
        // The effect section runs on the mixed voices, slice by slice, the way Faust's own
        // polyphonic architecture runs it (dsp_poly_factory in poly-dsp.h): a sequencer that owns
        // both and holds the voices' mix in a buffer of its own, one slice long.
        auto* effect = new mydsp_effect();
        // build-effect.sh fits its inputs to the voices and leaves out one with more outputs than this.
        jassert (effect->getNumInputs() == voices->getNumOutputs() && effect->getNumOutputs() <= kMaxFaustChannels);
        effect->buildUserInterface (&effectMap);
        effectMaps.push_back (&effectMap);
        meterTree.beginStage (true);
        effect->buildUserInterface (&meterTree);
        engine.reset (new dsp_sequencer (voices, effect, kMaxSlice));
       #else
        engine.reset (voices);
       #endif
       #if ! PANGEA_IS_MIDI_TOOL
        meters.take (meterTree, PANGEA_IS_SYNTH ? "instrument" : "effect");
       #endif

        for (auto& c : model.collector.controls)
        {
            if (c.isParameter())
                bindings.push_back ({ apvts.getRawParameterValue (c.id), zonesFor (c) });
            else if (c.hostDriven())
                tempoBindings.push_back ({ zonesFor (c), c.min, c.max, c.label, hostValueOf (c) });
        }

       #if HAS_LOGIC
        for (const auto& c : logic)
            logicParamValues.push_back (apvts.getRawParameterValue (c.id));

        // Every control the logic drives must exist in the Faust code and be tagged [logic:1]. A
        // control of that name that only the effect section has is driven too, beside the voice's.
        for (int i = 0; i < pangea::logic::numControls(); ++i)
        {
            std::vector<FAUSTFLOAT*> zones, effectZones;
            for (auto& c : model.collector.controls)
                if (c.logicDriven && c.label == pangea::logic::controlName (i))
                    (c.path.isNotEmpty() ? zones : effectZones) = zonesFor (c);
            zones.insert (zones.end(), effectZones.begin(), effectZones.end());
            jassert (! zones.empty());
            logicZones.push_back (zones);
        }
       #endif

        events.reserve (2048);
        logicEvents.resize ((size_t) pangea::notes::maxEvents);
        midiOut.ensureSize (8192);
       #if HAS_LOGIC
        pangea::logic::Guard::setUp();
       #endif
    }

   #if HAS_LOGIC
    ~PangeaProcessor() override { endModule(); }
   #endif

    void prepareToPlay (double sampleRate, int samplesPerBlock) override
    {
        engine->init ((int) sampleRate);
        inScratch.setSize (juce::jmax (1, model.numInputs), juce::jmax (16, samplesPerBlock));
        outScratch.setSize (juce::jmax (1, model.numOutputs), juce::jmax (16, samplesPerBlock));
        inScratch.clear();
        outScratch.clear();
        // The bypass: the length of its fade at this rate, and room for an effect's input.
        bypassFader.prepare (sampleRate);
        bypassEnded = false;
        if (kBypassGivesInput)
        {
            dryScratch.setSize (2, juce::jmax (16, samplesPerBlock));
            dryScratch.clear();
        }
        freeRunningSamples = 0;
        hostSamples = 0;
        sustainDown = false;
        sustained.fill (0);
        pushParameters();
        follower.forget();
        // A fresh module knows nothing of the notes the last one left: the next block ends them.
        endAllPending = true;
       #if HAS_LOGIC
        freshModule (sampleRate);
       #endif
    }

    // Nothing can be sent after a release or during a reset, so the next block ends what sounds.
    void releaseResources() override { endAllPending = true; }
    void reset() override { endAllPending = true; }

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
       #if PANGEA_AS_MIDI_EFFECT
        juce::ignoreUnused (layouts);
        return true;   // a MIDI effect has no audio; whatever Logic offers is fine
       #else
        const auto out = layouts.getMainOutputChannelSet();
        if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
            return false;
        if (model.numInputs == 0 || PANGEA_IS_MIDI_TOOL)
            return true;
        const auto in = layouts.getMainInputChannelSet();
        if (! (in.isDisabled() || in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono()))
            return false;
        // The sidechain, when the code has one: off, mono or stereo, whatever the main bus is.
        if (layouts.inputBuses.size() < 2)
            return true;
        const auto side = layouts.getChannelSet (true, 1);
        return side.isDisabled() || side == juce::AudioChannelSet::stereo() || side == juce::AudioChannelSet::mono();
       #endif
    }

    // The bypass is a parameter the plugin returns (getBypassParameter), so JUCE's wrappers never
    // call processBlockBypassed: a host's bypass arrives as the parameter, and is acted on here.
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        juce::ScopedNoDenormals noDenormals;
        const bool bypassAsked = bypassValue->load (std::memory_order_relaxed) >= 0.5f;
       #if PANGEA_IS_MIDI_TOOL
        processMidiTool (buffer, midi, bypassAsked);
       #else
        processSound (buffer, midi, bypassAsked);
       #endif
    }

    // For a host that calls this all the same: the same paths, bypassed whatever the parameter says.
    void processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        juce::ScopedNoDenormals noDenormals;
       #if PANGEA_IS_MIDI_TOOL
        processMidiTool (buffer, midi, true);
       #else
        processSound (buffer, midi, true);
       #endif
    }

    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParameter; }

    juce::AudioProcessorEditor* createEditor() override
    {
       #if PANGEA_WEB_EDITOR
        // The test: the component library in a web view (PangeaWebEditor.h), a logic module's
        // controls and playheads included. Its footer names who built the plugin and links to
        // Pangea: build.sh compiled in that much of plugin.json (credit.json).
        return new pangea::WebEditor (*this, apvts, model.collector.controls, PANGEA_NAME,
                                      PangeaUiData::ui_json, (size_t) PangeaUiData::ui_jsonSize, logic, &display,
                                      pangea::Credit::fromPluginJson (PangeaUiData::credit_json,
                                                                      (size_t) PangeaUiData::credit_jsonSize),
                                      &meters,
                                     #if PANGEA_IS_MIDI_TOOL
                                      &heard   // its notes, for the Felt look's chord map and notes
                                     #else
                                      nullptr
                                     #endif
                                      );
       #else
        return createEditorWithUi (PangeaUiData::ui_json, (size_t) PangeaUiData::ui_jsonSize);
       #endif
    }

    // The editor drawn from the given ui.json bytes rather than the embedded ones: the render tool
    // uses it to draw one build in every style. The bytes are checked exactly as the embedded ones are.
    pangea::Editor* createEditorWithUi (const void* uiData, size_t uiSize)
    {
        return new pangea::Editor (*this, apvts, model.collector.root, model.collector.controls, PANGEA_NAME,
                                   uiData, uiSize, logic, &display);
    }
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return juce::String (PANGEA_NAME) + (PANGEA_AS_MIDI_EFFECT ? " FX" : ""); }
    bool acceptsMidi() const override { return PANGEA_IS_SYNTH != 0 || PANGEA_IS_MIDI_TOOL != 0; }
    bool producesMidi() const override { return PANGEA_IS_MIDI_TOOL != 0; }
    bool isMidiEffect() const override { return PANGEA_AS_MIDI_EFFECT != 0; }
    double getTailLengthSeconds() const override { return PANGEA_IS_MIDI_TOOL ? 0.0 : 4.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& dest) override
    {
        if (auto xml = apvts.copyState().createXml())
            copyXmlToBinary (*xml, dest);
    }

    void setStateInformation (const void* data, int size) override
    {
        // A state saved before the plugin had its bypass says nothing of it. A parameter that a
        // state leaves out goes back to where a new plugin has it, so such a state opens with the
        // plugin in (pangea-render bypass-check holds that).
        if (auto xml = getXmlFromBinary (data, size))
            if (xml->hasTagName (apvts.state.getType()))
                apvts.replaceState (juce::ValueTree::fromXml (*xml));
    }

    // For the offline render tool.
    bool isPolyphonic() const { return model.polyphonic; }
    int numVoices() const { return model.numVoices; }
    const std::vector<pangea::Control>& controls() const { return model.collector.controls; }
    const std::vector<pangea::LogicControl>& logicControlList() const { return logic; }
    const pangea::notes::Display& getDisplay() const { return display; }
    /** The sound code's bar graphs as they play, for the web panel's meters (decision 91). */
    const pangea::MeterTaps& getMeterTaps() const noexcept { return meters; }

    // What each control the host sets holds now ([pangea:bpm], [pangea:playing], [pangea:beat], [pangea:beatfrac]), for
    // pangea-render's tempo and transport checks.
    struct TempoReading { juce::String label; float value, min, max; juce::String kind; };
    std::vector<TempoReading> readTempo() const
    {
        std::vector<TempoReading> out;
        for (const auto& t : tempoBindings)
            if (! t.zones.empty())
                out.push_back ({ t.label, (float) *t.zones.front(), t.min, t.max,
                                 t.what == HostValue::bpm ? "bpm" : t.what == HostValue::playing ? "playing"
                                 : t.what == HostValue::fraction ? "beatfrac" : "beat" });
        return out;
    }
   #if PANGEA_RENDER_TOOL
    /** How many zones a control is bound to: in the voices, and in the effect section. */
    std::pair<int, int> homesOf (const pangea::Control& c) const
    {
        auto inVoices = c, inEffect = c;
        inVoices.effectPath = {};
        inEffect.path = {};
        return { (int) zonesFor (inVoices).size(), (int) zonesFor (inEffect).size() };
    }
    /** For the bypass check: the voices that hold a note, and those that have not gone quiet yet. */
    std::pair<int, int> voicesInUse() const
    {
        int holding = 0, busy = 0;
        if (poly != nullptr)
            for (auto* voice : poly->fVoiceTable)
            {
                holding += (voice->fCurNote >= 0 || voice->fCurNote == kLegatoVoice) ? 1 : 0;
                busy += voice->fCurNote != kFreeVoice ? 1 : 0;
            }
        return { holding, busy };
    }
    /** For the bypass check: how many notes the logic module has sounding. */
    int logicNotesSounding() const { return logicNotes.count(); }
    /** The most steps one block of the module has taken since it was made (the last prepare). */
    long long mostStepsInABlock() const { return mostSteps; }
    /** Shows the stopped message, for pictures of it. */
    void showStopped (int why) { display.stopped.store (why, std::memory_order_relaxed); }
    /** For the sidechain check: the code's sidechain inputs, and the channels the last block had on the bus. */
    int sidechainInputs() const { return model.sidechainInputs; }
    int sidechainHeardChannels() const { return sidechainHeard; }
   #endif

private:
    static BusesProperties makeBuses()
    {
       #if PANGEA_AS_MIDI_EFFECT
        return BusesProperties();   // a MIDI effect: no audio at all, as Logic's MIDI FX slot expects
       #else
        mydsp probe;   // an instrument has no audio inputs
        auto buses = BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true);
        if (probe.getNumInputs() > 0 && ! PANGEA_IS_MIDI_TOOL)
            buses = buses.withInput ("Input", juce::AudioChannelSet::stereo(), true);
        // A sidechain the code declares: a second input bus, stereo, and off by default, so a host
        // that never routes one sees the plugin as it would without it (docs/sidechain.md).
        if (Model::sidechainOf (probe) > 0)
            buses = buses.withInput (pangea::sidechainBusName, juce::AudioChannelSet::stereo(), false);
        return buses;
       #endif
    }

    static juce::AudioProcessorValueTreeState::ParameterLayout buildLayout (const Model& m, const std::vector<pangea::LogicControl>& logic)
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;
        auto addFloat = [&] (const juce::ParameterID& id, const juce::String& name, float mn, float mx, float step,
                             float init, bool logScale, const juce::String& unit)
        {
            if (mx <= mn) mx = mn + 1.0f;
            // Whole-number steps become a real interval (Transpose, Rotate). Finer steps only set how many
            // decimals are shown: a 0.01 interval on a 0.97 or 0.6 maximum fails auval's round trip by one bit.
            const bool stepped = step >= 1.0f;
            const int decimals = stepped ? 0 : (step > 0 ? juce::jlimit (1, 4, (int) std::ceil (-std::log10 (step))) : 2);
            juce::NormalisableRange<float> range (mn, mx, stepped ? step : 0.0f);
            if (logScale && mn > 0)
                range.setSkewForCentre (std::sqrt (mn * mx));
            auto attributes = juce::AudioParameterFloatAttributes()
                .withLabel (unit)
                .withStringFromValueFunction ([decimals] (float v, int) { return juce::String (v, decimals); });
            layout.add (std::make_unique<juce::AudioParameterFloat> (id, name, range,
                                                                     juce::jlimit (mn, mx, init), attributes));
        };

        for (const auto& c : m.collector.controls)
        {
            if (! c.isParameter())
                continue;
            // An Audio Unit lists its parameters by this hint and then by a hash of the id, and Logic
            // keeps automation by place in that list. A control only the effect section has takes
            // the later hint, so it comes after the voice's and leaves their places as they were in
            // a plugin built before the effect section was (decision 73).
            const juce::ParameterID id { c.id, c.path.isEmpty() ? 2 : 1 };
            if (c.isToggle())
                layout.add (std::make_unique<juce::AudioParameterBool> (id, c.label, c.init >= 0.5f));
            else
                addFloat (id, c.label, c.min, c.max, c.step, c.init, c.logScale, c.unit);
        }

        // The logic module's own controls. A menu shows its labels to the host too, and takes them
        // back when typed; its value stays the number the module reads (min, min + 1, ...).
        for (const auto& c : logic)
        {
            if (c.style != pangea::LogicControl::Style::menu || c.labels.isEmpty())
            {
                addFloat (juce::ParameterID { c.id, 1 }, c.hostName(), c.min, c.max, c.step, c.init, false, c.unit);
                continue;
            }
            const auto control = c;
            auto attributes = juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction ([control] (float v, int)
                {
                    const auto label = control.labelFor (v);
                    return label.isNotEmpty() ? label : juce::String (juce::roundToInt (v));
                })
                .withValueFromStringFunction ([control] (const juce::String& text)
                {
                    for (int i = 0; i < control.labels.size(); ++i)
                        if (control.labels[i].equalsIgnoreCase (text.trim()))
                            return control.min + (float) i;
                    return text.getFloatValue();
                });
            layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { c.id, 1 }, c.hostName(),
                                                                     juce::NormalisableRange<float> (c.min, c.max, 1.0f),
                                                                     c.init, attributes));
        }

        // Pangea's own bypass, the same parameter in every plugin (PangeaBypass.h; decision 75). It
        // is the last parameter, and its hint is above every control's, so that in an Audio Unit's
        // list it comes after them all and a plugin built again keeps the places it had. Its id
        // has an underscore, which no id made from a label or for a logic control has.
        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { pangea::bypass::parameterId, pangea::bypass::versionHint }, pangea::bypass::parameterName, false));
        return layout;
    }

    // The zone (or, for a polyphonic instrument, one zone per voice) behind a control, and the
    // effect section's zone when that has the control: a control both use is set in both.
    std::vector<FAUSTFLOAT*> zonesFor (const pangea::Control& c) const
    {
        std::vector<FAUSTFLOAT*> zones;
        auto find = [&zones] (const std::vector<MapUI*>& maps, const juce::String& path)
        {
            if (path.isEmpty())
                return;
            for (auto* map : maps)
            {
                auto& paths = map->getFullpathMap();
                auto it = paths.find (path.toStdString());
                if (it != paths.end())
                    zones.push_back (it->second);
            }
        };
        find (voiceMaps, c.path);
        find (effectMaps, c.effectPath);
        return zones;
    }

    void pushParameters()
    {
        for (auto& b : bindings)
        {
            const float v = b.value->load();
            for (auto* z : b.zones)
                *z = v;
        }
    }

    // The controls the host sets before every block, read on the audio thread inside processBlock,
    // where the play head is valid. [pangea:bpm] holds the host's tempo, 120 BPM when it has none
    // (decision 55). [pangea:playing] holds 1 while the transport plays and 0 while it is stopped,
    // and [pangea:beat] the song position in beats (quarter notes) at the start of the block
    // (decision 123), and [pangea:beatfrac] its fraction of a beat (decision 124; PangeaSong.h).
    // A host with no transport counts as playing, from beat 0 at the first block, as a logic
    // module runs freely without one. Each is kept inside its control's range. Written once, before
    // the block: a block computed in slices keeps them, and the sound code counts its samples on.
    void pushHost (int n)
    {
        if (tempoBindings.empty())
            return;
        const double sampleRate = getSampleRate() > 0.0 ? getSampleRate() : 48000.0;
        double bpm = 120.0, ppq = 0.0;
        bool playing = true, havePosition = false;
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
            {
                bpm = pos->getBpm().orFallback (120.0);
                playing = pos->getIsPlaying();
                if (auto p = pos->getPpqPosition())
                {
                    ppq = *p;
                    havePosition = true;
                }
            }
        if (! std::isfinite (bpm) || bpm < 1.0)
            bpm = 120.0;
        const auto song = pangea::songValues (playing, havePosition, ppq, hostSamples, bpm, sampleRate);
        hostSamples += n;
        for (auto& t : tempoBindings)
        {
            const double value = t.what == HostValue::bpm       ? bpm
                               : t.what == HostValue::playing   ? song.playing
                               : t.what == HostValue::fraction  ? song.fraction
                                                                : song.beat;
            const auto v = (FAUSTFLOAT) juce::jlimit ((double) t.min, (double) t.max, value);
            for (auto* z : t.zones)
                *z = v;
        }
    }

    // An effect or an instrument: the Faust code makes the sound; the logic module, if any, sets its
    // controls and (in a polyphonic instrument) may play its voices.
    //
    // `bypassAsked` is Pangea's bypass (PangeaBypass.h; decisions 74 and 75). The sound fades over
    // 30 ms to what a bypassed plugin gives, an effect's input or an instrument's silence, and back
    // the same way. The Faust code runs on unheard, as it does in the builder, so an effect comes
    // back with its tails as they are. An instrument takes no new notes while bypassed, and once
    // the fade is over its voices are ended. A logic module is run the way it is run while the
    // transport is stopped.
    void processSound (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, bool bypassAsked)
    {
        const int n = buffer.getNumSamples();
        // The main bus only: a sidechain's channels come after it in the buffer and are fed to the
        // code on their own (gatherSidechain). Without a sidechain bus the two counts are the same.
        const int bufferIns = getMainBusNumInputChannels();
        const int bufferOuts = getTotalNumOutputChannels();

        if (inScratch.getNumSamples() < n)
        {
            inScratch.setSize (inScratch.getNumChannels(), n, false, false, true);
            outScratch.setSize (outScratch.getNumChannels(), n, false, false, true);
            if (kBypassGivesInput)
                dryScratch.setSize (dryScratch.getNumChannels(), n, false, false, true);
        }

        // With the plugin in and no fade running, nothing below is done for the bypass: the block
        // is the plugin's own, sample for sample. Otherwise an effect's input is kept before the
        // outputs overwrite it.
        bypassFader.setTarget (bypassAsked);
        const bool blending = ! bypassFader.fullyIn();
        if (blending && kBypassGivesInput)
            for (int ch = 0; ch < juce::jmin (bufferOuts, dryScratch.getNumChannels()); ++ch)
                pangea::bypass::keepDry (dryScratch.getWritePointer (ch), ch, bufferOuts, buffer.getArrayOfReadPointers(), bufferIns, n);

        pushParameters();
        pushHost (n);
        gatherInputs (buffer, bufferIns, n);
        gatherSidechain (buffer, n);

        // Collect this block's events (logic first, then MIDI), ordered by sample.
        events.clear();
        int sequence = 0;
        // Bypassed, and the fade over: every voice is ended, once, by the path a MIDI all notes off
        // takes. Ended any sooner, the notes would stop inside the fade.
        if (! bypassAsked)
            bypassEnded = false;
        else if (bypassFader.fullyBypassed() && ! bypassEnded)
        {
            bypassEnded = true;
            if (model.polyphonic)
                events.push_back ({ 0, sequence++, Event::Kind::allNotesOff, 0, 0.0f });
        }
       #if HAS_LOGIC
        const int numLogic = runLogic (n, bypassAsked);
        if (bypassAsked)
            follower.forget();   // as a bypassed MIDI tool does: coming back is a start, not a jump
        for (int i = 0; i < numLogic; ++i)
        {
            const auto& e = logicEvents[(size_t) i];
            if (e.control >= 0 && std::isfinite (e.value))
                events.push_back ({ juce::jlimit (0, n, e.offset), sequence++, Event::Kind::logic, e.control, e.value });
        }
        display.take (logicEvents.data(), numLogic);
        display.playing.store (blockPlaying, std::memory_order_relaxed);
        if (model.polyphonic)
        {
            // Notes from the module play the Faust voices, with the same safety net as a MIDI tool's.
            const bool endAll = endAllPending.exchange (false) || blockJumped || ! blockPlaying || blockStopped;
            const int numNotes = pangea::notes::route (logicEvents.data(), numLogic, n, blockPlaying, endAll, logicNotes,
                                                        noteScratch.data(), (int) noteScratch.size());
            for (int k = 0; k < numNotes; ++k)
            {
                const auto& note = noteScratch[(size_t) k];
                events.push_back ({ note.offset, sequence++, note.on ? Event::Kind::noteOn : Event::Kind::noteOff,
                                    note.note, (float) note.velocity });
            }
        }
       #endif
        if (model.polyphonic)
            for (const auto metadata : midi)
                addMidiEvent (metadata.getMessage(), juce::jlimit (0, n, metadata.samplePosition), sequence++, ! bypassAsked);

        std::sort (events.begin(), events.end(), [] (const Event& x, const Event& y)
        {
            return x.offset != y.offset ? x.offset < y.offset : x.sequence < y.sequence;
        });

        // Render in slices so each event lands on its exact sample.
        int pos = 0;
        for (size_t e = 0; e <= events.size(); ++e)
        {
            const int sliceEnd = e < events.size() ? events[e].offset : n;
            for (int start = pos; start < sliceEnd; start += kMaxSlice)
                renderSlice (start, juce::jmin (kMaxSlice, sliceEnd - start));
            pos = juce::jmax (pos, sliceEnd);
            if (e < events.size())
                apply (events[e]);
        }

        deliverOutputs (buffer, bufferOuts, n);
        meters.capture();   // the bar graphs' values for this block: a store each, no lock (decision 91)

        // The bypass's fade, sample by sample across the block, between what the plugin made and
        // what a bypassed plugin gives.
        const auto ramp = bypassFader.advance (n);
        if (blending)
            for (int ch = 0; ch < bufferOuts; ++ch)
                pangea::bypass::blend (ramp, buffer.getWritePointer (ch),
                                       kBypassGivesInput && ch < dryScratch.getNumChannels() ? dryScratch.getReadPointer (ch) : nullptr, n);
        midi.clear();
    }

   #if PANGEA_IS_MIDI_TOOL
    // A MIDI tool: no sound; the module's notes go out as MIDI on their samples. Incoming MIDI is
    // dropped (a MIDI tool plays its own notes; Pattern Player's held-chord harmony is left out).
    // Bypassed, the tool is transparent: what comes in goes out, and its own notes end.
    void processMidiTool (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, bool bypassed)
    {
        const int n = buffer.getNumSamples();
        buffer.clear();
        midiOut.clear();
        const bool endAll = endAllPending.exchange (false);

        if (bypassed)
        {
            if (! wasBypassed || endAll)
                endAllNotes (logicNotes);
            for (const auto metadata : midi)
            {
                const auto m = metadata.getMessage();
                if (m.isNoteOn())
                    passNotes.set (m.getChannel(), m.getNoteNumber(), true);
                else if (m.isNoteOff())
                    passNotes.set (m.getChannel(), m.getNoteNumber(), false);
                else if (m.isAllNotesOff() || m.isAllSoundOff())
                    passNotes.clear();
                midiOut.addEvent (m, juce::jlimit (0, juce::jmax (0, n - 1), metadata.samplePosition));
            }
            wasBypassed = true;
            follower.forget();
            display.playing.store (false, std::memory_order_relaxed);
        }
        else
        {
            if (wasBypassed)
                endAllNotes (passNotes);   // notes that came through while bypassed would now never end
            wasBypassed = false;
            const int numLogic = runLogic (n);
            display.take (logicEvents.data(), numLogic);
            display.playing.store (blockPlaying, std::memory_order_relaxed);
            const int numNotes = pangea::notes::route (logicEvents.data(), numLogic, n, blockPlaying,
                                                        endAll || blockJumped || ! blockPlaying || blockStopped, logicNotes,
                                                        noteScratch.data(), (int) noteScratch.size());
            for (int k = 0; k < numNotes; ++k)
                addNote (noteScratch[(size_t) k]);
        }
        midi.swapWith (midiOut);
    }

    void addNote (const pangea::notes::Note& note)
    {
        midiOut.addEvent (note.on ? juce::MidiMessage::noteOn (note.channel, note.note, (juce::uint8) note.velocity)
                                  : juce::MidiMessage::noteOff (note.channel, note.note),
                          note.offset);
        heard.push (note);   // for the panel's notes, while one listens (docs/felt-look.md)
    }

    // Offs at the start of this block for everything `tracker` says is sounding.
    void endAllNotes (pangea::notes::Tracker& tracker)
    {
        tracker.forEachOn ([this] (int channel, int note) { addNote ({ 0, false, channel, note, 0 }); });
        tracker.clear();
    }
   #endif

    void gatherInputs (const juce::AudioBuffer<float>& buffer, int bufferIns, int n)
    {
        for (int ch = 0; ch < model.mainInputs(); ++ch)
        {
            if (bufferIns == 0)
                inScratch.clear (ch, 0, n);
            else if (model.mainInputs() == 1 && bufferIns >= 2)
            {
                inScratch.copyFrom (0, 0, buffer, 0, 0, n);          // a mono effect hears both sides
                inScratch.addFrom (0, 0, buffer, 1, 0, n);
                inScratch.applyGain (0, 0, n, 0.5f);
            }
            else if (ch < bufferIns)
                inScratch.copyFrom (ch, 0, buffer, ch, 0, n);
            else
                inScratch.clear (ch, 0, n);
        }
    }

    // The code's sidechain inputs, from the Sidechain bus while the host has it on, and silence
    // while it is off: an effect hears silence there with nothing routed, as the site plays it.
    void gatherSidechain (juce::AudioBuffer<float>& buffer, int n)
    {
        if (model.sidechainInputs == 0)
            return;
        float* faustInputs[2] = {};
        for (int ch = 0; ch < model.sidechainInputs; ++ch)
            faustInputs[ch] = inScratch.getWritePointer (model.mainInputs() + ch);
        const float* bus[2] = {};
        const int busChannels = pangea::sidechainChannelsPassed (*this, buffer);
        const int first = busChannels > 0 ? getChannelIndexInProcessBlockBuffer (true, 1, 0) : 0;
        for (int ch = 0; ch < busChannels; ++ch)
            bus[ch] = buffer.getReadPointer (first + ch);
        pangea::fillSidechain (faustInputs, model.sidechainInputs, bus, busChannels, n);
       #if PANGEA_RENDER_TOOL
        sidechainHeard = busChannels;
       #endif
    }

    void deliverOutputs (juce::AudioBuffer<float>& buffer, int bufferOuts, int n) const
    {
        for (int ch = 0; ch < bufferOuts; ++ch)
        {
            if (model.numOutputs == 0)
                buffer.clear (ch, 0, n);
            else if (bufferOuts == 1 && model.numOutputs >= 2)
            {
                buffer.copyFrom (0, 0, outScratch, 0, 0, n);         // a stereo result on a mono track
                buffer.addFrom (0, 0, outScratch, 1, 0, n);
                buffer.applyGain (0, 0, n, 0.5f);
            }
            else
                buffer.copyFrom (ch, 0, outScratch, juce::jmin (ch, model.numOutputs - 1), 0, n);
        }
        for (int ch = bufferOuts; ch < buffer.getNumChannels(); ++ch)
            buffer.clear (ch, 0, n);
    }

    void renderSlice (int start, int count)
    {
        float* ins[kMaxFaustChannels] = {};
        float* outs[kMaxFaustChannels] = {};
        for (int ch = 0; ch < model.numInputs && ch < kMaxFaustChannels; ++ch)
            ins[ch] = inScratch.getWritePointer (ch, start);
        for (int ch = 0; ch < model.numOutputs && ch < kMaxFaustChannels; ++ch)
            outs[ch] = outScratch.getWritePointer (ch, start);
        engine->compute (count, ins, outs);
    }

    // `newNotes` is false while the plugin is bypassed: it takes no new notes then. Note offs, the
    // pedal and all notes off still pass, so that a note begun before the bypass can end.
    void addMidiEvent (const juce::MidiMessage& m, int offset, int sequence, bool newNotes)
    {
        if (m.isNoteOn())
        {
            if (newNotes)
                events.push_back ({ offset, sequence, Event::Kind::noteOn, m.getNoteNumber(), (float) m.getVelocity() });
        }
        else if (m.isNoteOff())
            events.push_back ({ offset, sequence, Event::Kind::noteOff, m.getNoteNumber(), 0.0f });
        else if (m.isAllNotesOff() || m.isAllSoundOff())
            events.push_back ({ offset, sequence, Event::Kind::allNotesOff, 0, 0.0f });
        else if (m.isSustainPedalOn())
            events.push_back ({ offset, sequence, Event::Kind::sustain, 1, 0.0f });
        else if (m.isSustainPedalOff())
            events.push_back ({ offset, sequence, Event::Kind::sustain, 0, 0.0f });
    }

    void apply (const Event& ev)
    {
        switch (ev.kind)
        {
            case Event::Kind::logic:
               #if HAS_LOGIC
                if (ev.a >= 0 && ev.a < (int) logicZones.size())
                    for (auto* z : logicZones[(size_t) ev.a])
                        *z = ev.b;
               #endif
                break;

            case Event::Kind::noteOn:
                if (sustained[(size_t) ev.a] > 0)   // the key is pressed again while the pedal holds it
                    sustained[(size_t) ev.a] = 0;
                poly->keyOn (0, ev.a, (int) ev.b);
                break;

            case Event::Kind::noteOff:
                if (sustainDown)
                    sustained[(size_t) ev.a] = (uint8_t) juce::jmin (255, sustained[(size_t) ev.a] + 1);
                else
                    endVoice (ev.a);
                break;

            case Event::Kind::sustain:
                sustainDown = ev.a != 0;
                if (! sustainDown)
                {
                    for (int pitch = 0; pitch < 128; ++pitch)
                    {
                        while (sustained[(size_t) pitch] > 0)
                        {
                            endVoice (pitch);
                            --sustained[(size_t) pitch];
                        }
                    }
                }
                break;

            case Event::Kind::allNotesOff:
                sustained.fill (0);
                poly->allNotesOff (false);
                break;
        }
    }

    // A note off for the voice that plays a pitch. Faust's own keyOff prints a line when no voice
    // does, and nothing may be printed on this thread. With a bypass that is an everyday case: the
    // note off of a note the bypass ended, or of one that was never taken. So Faust is asked only
    // when a voice holds the pitch, by the test its own keyOff makes.
    void endVoice (int pitch)
    {
        for (auto* voice : poly->fVoiceTable)
            if (voice->fCurNote == pitch || (voice->fCurNote == kLegatoVoice && voice->fNextNote == pitch))
            {
                poly->keyOff (0, pitch, 0);
                return;
            }
    }

   #if HAS_LOGIC
    // A new module, prepared, in place of the old one. A module that was stopped starts again here.
    void freshModule (double sampleRate)
    {
        endModule();
        setStopped (pangea::logic::Stop::none);
       #if PANGEA_RENDER_TOOL
        mostSteps = 0;
       #endif
        const auto why = guard.run (pangea::logic::setupLimits, [this, sampleRate]
        {
            module = pangea::logic::make();
            pangea::logic::prepare (*module, sampleRate);
        });
        if (why != pangea::logic::Stop::none)
        {
            module = nullptr;   // stopped while it was made: left as it is, never touched again
            setStopped (why);
        }
    }

    // Ends the module, within the budget too: its destructor is the module's code.
    void endModule()
    {
        if (module == nullptr)
            return;
        auto* old = module;
        module = nullptr;
        guard.run (pangea::logic::setupLimits, [old] { pangea::logic::unmake (old); });
    }

    void setStopped (pangea::logic::Stop why)
    {
        stopped = why;
        display.stopped.store ((int) why, std::memory_order_relaxed);
    }

    // Runs the module for this block with the host's transport; sets blockPlaying, blockJumped and
    // blockStopped. A stopped module is not run again: its blocks are empty. `asStopped` is the
    // bypass of an effect or an instrument: the module is run the way it is run while the
    // transport is stopped, with the song carrying on underneath, so it ends its notes, starts
    // none and comes back in time (as the builder runs a bypassed module, decision 74).
    int runLogic (int n, bool asStopped = false)
    {
        const double sampleRate = getSampleRate() > 0.0 ? getSampleRate() : 48000.0;
        double bpm = 120.0, ppq = 0.0, beatsPerBar = 4.0;
        bool playing = true, havePosition = false;
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
            {
                bpm = pos->getBpm().orFallback (120.0);
                playing = pos->getIsPlaying();
                if (auto p = pos->getPpqPosition())
                {
                    ppq = *p;
                    havePosition = true;
                }
                // Quarter notes per bar: 4 in 4/4, 3 in 6/8.
                if (auto ts = pos->getTimeSignature(); ts && ts->numerator > 0 && ts->denominator > 0)
                    beatsPerBar = ts->numerator * 4.0 / ts->denominator;
            }
        if (! std::isfinite (bpm) || bpm < 1.0) bpm = 120.0;
        if (bpm > 999.0) bpm = 999.0;
        if (! std::isfinite (beatsPerBar) || beatsPerBar <= 0.0 || beatsPerBar > 64.0) beatsPerBar = 4.0;
        if (! havePosition || ! std::isfinite (ppq))   // no host position: run freely from the first block
            ppq = (double) freeRunningSamples * bpm / (60.0 * sampleRate);
        freeRunningSamples += n;
        if (asStopped)
            playing = false;

        blockPlaying = playing;
        blockJumped = follower.jumped (ppq, bpm, n, sampleRate, playing);

        float params[64] = {};
        for (size_t i = 0; i < logicParamValues.size() && i < 64; ++i)
            params[i] = logicParamValues[i]->load();

        blockStopped = false;
        if (module == nullptr || stopped != pangea::logic::Stop::none)
            return 0;
        LogicBlock block { sampleRate, bpm, ppq, n, playing, params, beatsPerBar };
        logicCount = 0;
        const auto why = guard.run (pangea::logic::processLimits, [this, &block]
        {
            logicCount = pangea::logic::process (*module, block, logicEvents.data(), (int) logicEvents.size());
        });
       #if PANGEA_RENDER_TOOL
        mostSteps = juce::jmax (mostSteps, guard.stepsUsed());
       #endif
        if (why != pangea::logic::Stop::none)
        {
            // What it wrote in this block is dropped, and what it left sounding ends now. The module
            // stays as it is until the host prepares the plugin again (freshModule), off this thread.
            setStopped (why);
            blockStopped = true;
            return 0;
        }
        return juce::jlimit (0, (int) logicEvents.size(), logicCount);
    }
   #endif

    struct Binding { std::atomic<float>* value; std::vector<FAUSTFLOAT*> zones; };
    // A control the host sets before every block: what it holds, and where (decisions 55, 123 and 124).
    enum class HostValue { bpm, playing, beat, fraction };
    static HostValue hostValueOf (const pangea::Control& c)
    {
        return c.playDriven       ? HostValue::playing
             : c.beatDriven       ? HostValue::beat
             : c.fractionDriven   ? HostValue::fraction
                                  : HostValue::bpm;
    }
    struct TempoBinding { std::vector<FAUSTFLOAT*> zones; float min, max; juce::String label; HostValue what = HostValue::bpm; };
    std::vector<TempoBinding> tempoBindings;   // every control marked [pangea:bpm], [pangea:playing], [pangea:beat] or [pangea:beatfrac]
    juce::int64 hostSamples = 0;               // samples since prepareToPlay: the song position when the host has none

    Model model;
    std::vector<pangea::LogicControl> logic;
    std::unique_ptr<dsp> engine;
    mydsp_poly* poly = nullptr;            // owned by engine when polyphonic
    MapUI monoMap;
    std::vector<MapUI*> voiceMaps;
    MapUI effectMap;                       // the effect section's zones, when the instrument has one
    std::vector<MapUI*> effectMaps;
    juce::AudioProcessorValueTreeState apvts;
    std::vector<Binding> bindings;
    juce::AudioBuffer<float> inScratch, outScratch;

    // Pangea's bypass (PangeaBypass.h): the parameter, its fader, an effect's input kept for the
    // block, and whether the voices have been ended for this bypass.
    juce::RangedAudioParameter* bypassParameter = nullptr;
    std::atomic<float>* bypassValue = nullptr;
    pangea::bypass::Fader bypassFader;
    juce::AudioBuffer<float> dryScratch;
    bool bypassEnded = false;

    std::vector<Event> events;
    std::vector<LogicEvent> logicEvents;
    juce::int64 freeRunningSamples = 0;
    bool sustainDown = false;
    std::array<uint8_t, 128> sustained {};

    // The notes the module has sounding, and the safety net around them (PangeaNotes.h).
    pangea::notes::Tracker logicNotes, passNotes;
    pangea::notes::TransportFollower follower;
    pangea::notes::Display display;
    pangea::notes::Heard heard;   // the notes a MIDI tool sends out, for its panel (docs/felt-look.md)
    // The sound code's bar graphs as they play, for the web panel's meters and step lights
    // (PangeaMeters.h, decision 91). Empty for a MIDI tool.
    pangea::MeterTaps meters;
    std::array<pangea::notes::Note, 2 * pangea::notes::maxEvents + 16 * 128> noteScratch {};
    juce::MidiBuffer midiOut;
    std::atomic<bool> endAllPending { true };
    bool blockPlaying = false, blockJumped = false, wasBypassed = false;
   #if PANGEA_RENDER_TOOL
    long long mostSteps = 0;
    int sidechainHeard = 0;   // the Sidechain bus's channels in the last block (the sidechain check)
   #endif

   #if HAS_LOGIC
    pangea::logic::Module* module = nullptr;
    pangea::logic::Guard guard;
    pangea::logic::Stop stopped = pangea::logic::Stop::none;
    int logicCount = 0;
    bool blockStopped = false;
    std::vector<std::atomic<float>*> logicParamValues;
    std::vector<std::vector<FAUSTFLOAT*>> logicZones;
   #endif
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PangeaProcessor();
}

// For the render tool (tools/render/Main.cpp declares these too).
juce::AudioProcessorEditor* pangeaCreateEditorWithUi (juce::AudioProcessor&, const void* uiData, size_t uiSize);
juce::StringArray pangeaDescribeEditor (juce::AudioProcessorEditor&);

// An editor for this processor drawn from other ui.json bytes.
juce::AudioProcessorEditor* pangeaCreateEditorWithUi (juce::AudioProcessor& processor, const void* uiData, size_t uiSize)
{
    if (auto* p = dynamic_cast<PangeaProcessor*> (&processor))
        return p->createEditorWithUi (uiData, uiSize);
    return nullptr;
}

// For the render tool: what an editor drew and what in its ui.json fell back, one line each.
juce::StringArray pangeaDescribeEditor (juce::AudioProcessorEditor& editor)
{
    juce::StringArray lines;
    if (auto* e = dynamic_cast<pangea::Editor*> (&editor))
    {
        const auto& spec = e->getSpec();
        lines.add (juce::String ("style ") + pangea::ui::styleName (spec.style) + ", "
                   + (spec.hasLayout ? "the person's layout (" + juce::String ((int) spec.layout.items.size()) + " items)"
                                     : juce::String ("the automatic arrangement"))
                   + ", " + juce::String (e->getNumCells()) + " controls shown, panel "
                   + juce::String (e->getNaturalWidth()) + " x " + juce::String (e->getNaturalHeight()) + " units"
                   + (e->getNumLit() > 0 ? ", " + juce::String (e->getNumLit()) + " playhead bar lit" : juce::String()));
        for (const auto& note : spec.notes)
            lines.add ("fell back: " + note);
        if (auto* bypass = e->getBypassSwitch())
        {
            // Where it sits, from its own bounds: the title row is the top of the panel, above the controls.
            const int rowBottom = pangea::Faceplate::stageMargin + pangea::Look::from (spec).headerHeight();
            const bool inRow = bypass->getY() >= pangea::Faceplate::stageMargin && bypass->getBottom() <= rowBottom;
            lines.add (juce::String ("bypass switch ") + (bypass->getToggleState() ? "on" : "off")
                       + (inRow ? ", in the title row" : ", outside the title row") + ", its right edge "
                       + juce::String (e->getNaturalWidth() - bypass->getRight()) + " units from the window's, the controls drawn "
                       + (e->showsBypassed() ? "at " + juce::String (juce::roundToInt (e->getControlsAlpha() * 100.0f)) + " per cent"
                                             : juce::String ("in full")));
        }
        else
            lines.add ("no bypass switch");
        if (e->getStoppedText().isNotEmpty())
            lines.add ("stopped message: " + e->getStoppedText());
    }
    return lines;
}

#if PANGEA_RENDER_TOOL
// ---- Hooks for pangea-render's offline checks (tools/render) ----------------------------------

#include "RenderHooks.h"

bool pangeaIsMidiTool() { return PANGEA_IS_MIDI_TOOL != 0; }
bool pangeaIsInstrument() { return PANGEA_IS_SYNTH != 0; }
bool pangeaHasLogicModule() { return HAS_LOGIC != 0; }
bool pangeaHasWebPanel() { return PANGEA_WEB_EDITOR != 0; }

pangea::render::PanelBypass pangeaPanelBypass (juce::AudioProcessorEditor& editor)
{
    pangea::render::PanelBypass b;
    if (auto* e = dynamic_cast<pangea::Editor*> (&editor))
    {
        b.button = e->getBypassSwitch();
        b.controlsFainter = e->showsBypassed();
    }
    return b;
}

pangea::render::Sounding pangeaSounding (juce::AudioProcessor& processor)
{
    pangea::render::Sounding s;
    if (auto* p = dynamic_cast<PangeaProcessor*> (&processor))
    {
        s.polyphonic = p->isPolyphonic();
        s.numVoices = p->isPolyphonic() ? p->numVoices() : 0;
        const auto voices = p->voicesInUse();
        s.voicesHolding = voices.first;
        s.voicesBusy = voices.second;
        s.logicNotes = p->logicNotesSounding();
        s.modulePlaying = p->getDisplay().playing.load();
    }
    return s;
}

std::vector<pangea::LogicControl> pangeaLogicControls() { return logicControls(); }

std::vector<pangea::render::TempoValue> pangeaTempoValues (juce::AudioProcessor& processor)
{
    std::vector<pangea::render::TempoValue> out;
    if (auto* p = dynamic_cast<PangeaProcessor*> (&processor))
        for (const auto& r : p->readTempo())
            out.push_back ({ r.label, r.value, r.min, r.max, r.kind });
    return out;
}

juce::StringArray pangeaParameterKeys (juce::AudioProcessor& processor)
{
    juce::StringArray keys;
    if (auto* p = dynamic_cast<PangeaProcessor*> (&processor))
    {
        for (const auto& c : p->controls())
            if (c.isParameter())
                keys.add (c.key());
        for (const auto& c : p->logicControlList())
            keys.add (c.key());
    }
    return keys;
}

bool pangeaHasEffectSection() { return HAS_EFFECT != 0; }

/** The sidechain inputs the code declares (docs/sidechain.md): 0, 1 or 2. */
int pangeaSidechainInputs (juce::AudioProcessor& processor)
{
    if (auto* p = dynamic_cast<PangeaProcessor*> (&processor))
        return p->sidechainInputs();
    return 0;
}

/** The Sidechain bus's channels in the last block the processor played: 0 while it is off. */
int pangeaSidechainHeard (juce::AudioProcessor& processor)
{
    if (auto* p = dynamic_cast<PangeaProcessor*> (&processor))
        return p->sidechainHeardChannels();
    return 0;
}

std::vector<pangea::render::EffectControl> pangeaEffectControls (juce::AudioProcessor& processor)
{
    std::vector<pangea::render::EffectControl> out;
    if (auto* p = dynamic_cast<PangeaProcessor*> (&processor))
        for (const auto& c : p->controls())
            if (c.effectPath.isNotEmpty())
            {
                const auto homes = p->homesOf (c);
                out.push_back ({ c.id, c.key(), c.label, homes.first, homes.second, c.isParameter(), c.min, c.max });
            }
    return out;
}

int pangeaDisplayPosition (juce::AudioProcessor& processor, int slot)
{
    if (auto* p = dynamic_cast<PangeaProcessor*> (&processor))
        return p->getDisplay().position (slot);
    return -1;
}

// The module on its own: a fresh module, driven block by block through `plan`, with nothing of the
// platform around it but the step budget. This is what the plugin's MIDI out is compared with.
bool pangeaRunModule (double sampleRate, const std::vector<float>& params, const std::vector<pangea::render::Block>& plan,
                      std::vector<pangea::render::ModuleEvent>& out, int* stoppedAt)
{
    out.clear();
    if (stoppedAt != nullptr)
        *stoppedAt = -1;
   #if HAS_LOGIC
    pangea::logic::Guard::setUp();
    pangea::logic::Guard guard;
    pangea::logic::Module* module = nullptr;
    if (guard.run (pangea::logic::setupLimits, [&] { module = pangea::logic::make(); pangea::logic::prepare (*module, sampleRate); })
        != pangea::logic::Stop::none)
    {
        if (stoppedAt != nullptr)
            *stoppedAt = 0;
        return true;
    }
    std::vector<float> values (64, 0.0f);
    for (int i = 0; i < pangea::logic::numParams() && i < 64; ++i)
        values[(size_t) i] = i < (int) params.size() ? params[(size_t) i] : pangea::logic::params()[i].init;
    std::vector<LogicEvent> events ((size_t) pangea::notes::maxEvents);
    for (size_t b = 0; b < plan.size(); ++b)
    {
        const auto& block = plan[b];
        LogicBlock lb { sampleRate, block.bpm, block.ppq, block.numSamples, block.playing, values.data(), block.beatsPerBar };
        int count = 0;
        if (guard.run (pangea::logic::processLimits, [&] { count = pangea::logic::process (*module, lb, events.data(), (int) events.size()); })
            != pangea::logic::Stop::none)
        {
            if (stoppedAt != nullptr)
                *stoppedAt = (int) b;
            return true;   // left as it is, as the plugin leaves a stopped module
        }
        const int n = juce::jlimit (0, pangea::notes::maxEvents, count);
        for (int i = 0; i < n; ++i)
        {
            const auto& e = events[(size_t) i];
            out.push_back ({ (int) b, e.offset, e.control, e.value, e.note, e.channel });
        }
    }
    guard.run (pangea::logic::setupLimits, [&] { pangea::logic::unmake (module); });
    return true;
   #else
    juce::ignoreUnused (sampleRate, params, plan);
    return false;
   #endif
}

// How the processor's module stands: 0 running, 1 stopped for running too long, 2 for going too
// deep, 3 not run because the budget could not be set up.
int pangeaLogicStopped (juce::AudioProcessor& processor)
{
    if (auto* p = dynamic_cast<PangeaProcessor*> (&processor))
        return p->getDisplay().stopped.load();
    return -1;
}

long long pangeaMostLogicSteps (juce::AudioProcessor& processor)
{
    if (auto* p = dynamic_cast<PangeaProcessor*> (&processor))
        return p->mostStepsInABlock();
    return -1;
}

void pangeaShowStopped (juce::AudioProcessor& processor, int why)
{
    if (auto* p = dynamic_cast<PangeaProcessor*> (&processor))
        p->showStopped (why);
}

const pangea::MeterTaps* pangeaMeterTaps (juce::AudioProcessor& processor)
{
    if (auto* p = dynamic_cast<PangeaProcessor*> (&processor))
        return &p->getMeterTaps();
    return nullptr;
}
#endif
