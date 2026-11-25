// pangea-host-check: the Pangea plugin's offline checks (docs/pangea-plugin.md). tools/test-host.sh
// builds real modules into a scratch host folder, writes its library.json, and runs this with
// PANGEA_HOST_DIR pointing there. It drives the plugin's own processor (PangeaHost.h) for each of
// the three kinds, plays the desktop app's part in the request and response files, and then loads
// the built VST3 as a DAW does, to see the parameter names reach the host. Pangea's bypass, the
// parameter after the 128 slots (decision 75), is checked in all three kinds and in the VST3, and
// in the web panel: its header bar, its button and the parameter both ways. The list's search and
// its category menu (decision 85) are checked on the rule's own cases, on the library, and in the window.
// Pangea's sidechain input (docs/sidechain.md) is checked on its buses, on Kick Duck and Tape
// Drift in the processor, and on Kick Duck in the VST3.
//
//   pangea-host-check <Pangea.vst3> <Pangea Instrument.vst3>
#include "PangeaHost.h"
#include "../BypassHold.h"

#include <iostream>

std::list<GUI*> GUI::fGuiList;
ztimedmap GUI::gTimedZoneMap;

using namespace pangea::host;

namespace
{

int failures = 0;

void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  ok    " : "  FAIL  ") << what << std::endl;
    if (! ok)
        ++failures;
}

constexpr double kRate = 48000.0;
constexpr int kBlock = 512;

struct TestPlayHead : juce::AudioPlayHead
{
    double bpm = 120.0, ppq = 0.0;
    bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm (bpm);
        info.setPpqPosition (ppq);
        info.setIsPlaying (playing);
        info.setTimeSignature (TimeSignature { 4, 4 });
        return info;
    }
    void advance (int n) { if (playing) ppq += n * bpm / (60.0 * kRate); }
};

/** What blocks of a processor did: the left channel, the notes it sent out. */
struct Run
{
    std::vector<float> left;
    int ons = 0, offs = 0;
    std::set<int> sounding;
    float largestStep (size_t from, size_t to) const
    {
        float worst = 0;
        for (size_t i = juce::jmax<size_t> (1, from); i < juce::jmin (to, left.size()); ++i)
            worst = juce::jmax (worst, std::abs (left[i] - left[i - 1]));
        return worst;
    }
    double rms (size_t from, size_t to) const
    {
        double s = 0;
        size_t count = 0;
        for (size_t i = from; i < juce::jmin (to, left.size()); ++i, ++count)
            s += (double) left[i] * left[i];
        return count > 0 ? std::sqrt (s / (double) count) : 0.0;
    }
    bool finite() const
    {
        for (float x : left)
            if (! std::isfinite (x))
                return false;
        return true;
    }
};

long signalPos = 0;

/** One block: a steady tone, louder in bursts, for an effect; MIDI as given; notes out counted. */
void block (juce::AudioProcessor& p, Run& run, TestPlayHead& head, juce::MidiBuffer midi = {}, bool withSignal = true)
{
    const int channels = juce::jmax (2, juce::jmax (p.getTotalNumInputChannels(), p.getTotalNumOutputChannels()));
    juce::AudioBuffer<float> buffer (channels, kBlock);
    buffer.clear();
    if (withSignal && p.getTotalNumInputChannels() > 0)
        for (int i = 0; i < kBlock; ++i)
        {
            const long n = signalPos + i;
            const float env = (n % (long) (kRate / 2)) < (long) (kRate / 20) ? 0.5f : 0.2f;
            const float v = env * std::sin (2.0f * juce::MathConstants<float>::pi * 220.0f * (float) n / (float) kRate);
            buffer.setSample (0, i, v);
            buffer.setSample (1, i, v);
        }
    signalPos += kBlock;
    p.processBlock (buffer, midi);
    run.left.insert (run.left.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + kBlock);
    for (const auto m : midi)
    {
        const auto msg = m.getMessage();
        if (msg.isNoteOn()) { ++run.ons; run.sounding.insert (msg.getNoteNumber()); }
        else if (msg.isNoteOff()) { ++run.offs; run.sounding.erase (msg.getNoteNumber()); }
    }
    head.advance (kBlock);
}

SlotParameter* slotNamed (juce::AudioProcessor& p, const juce::String& name)
{
    for (auto* param : p.getParameters())
        if (auto* s = dynamic_cast<SlotParameter*> (param); s != nullptr && s->getName (100) == name)
            return s;
    return nullptr;
}

float realValue (SlotParameter* s)
{
    if (s == nullptr) return std::numeric_limits<float>::quiet_NaN();
    const auto spec = s->getSpec();
    return spec.range.convertFrom0to1 (s->getValue());
}

void setReal (SlotParameter* s, float v)
{
    if (s != nullptr)
        s->setValueNotifyingHost (s->getSpec().range.convertTo0to1 (v));
}

juce::File host() { return hostDir(); }

/** Plays the desktop app: answers the one request in the folder with the given module directory. */
juce::var answerRequest (const juce::File& moduleDir, int version, const juce::String& projectId)
{
    const auto files = host().getChildFile ("requests").findChildFiles (juce::File::findFiles, false, "*.json");
    if (files.size() != 1)
        return {};
    const auto json = juce::JSON::parse (files[0].loadFileAsString());
    const auto id = files[0].getFileNameWithoutExtension();
    files[0].deleteFile();
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("format", 1);
    obj->setProperty ("requestId", id);
    obj->setProperty ("ok", true);
    obj->setProperty ("outcome", "ready");
    obj->setProperty ("projectId", projectId);
    obj->setProperty ("version", version);
    obj->setProperty ("moduleDir", moduleDir.getFullPathName());
    obj->setProperty ("message", "ready");
    writeAtomically (host().getChildFile ("responses").getChildFile (id + ".json"), juce::JSON::toString (juce::var (obj)));
    return json;
}

void setHeadVersion (const juce::String& projectId, int version)
{
    const auto file = host().getChildFile ("library.json");
    auto json = juce::JSON::parse (file.loadFileAsString());
    if (auto* list = json.getProperty ("plugins", {}).getArray())
        for (auto& p : *list)
            if (p.getProperty ("projectId", {}).toString() == projectId)
                p.getDynamicObject()->setProperty ("headVersion", version);
    writeAtomically (file, juce::JSON::toString (json));
    juce::Thread::sleep (20);   // a new modification time, as a real rewrite has
}

juce::MemoryBlock effectChecks()
{
    std::cout << "Pangea (effect): Tape Drift v1, then v2 through Refresh" << std::endl;
    Processor p (HostKind::effect);
    TestPlayHead head;
    p.setPlayHead (&head);
    p.prepareToPlay (kRate, kBlock);
    p.poll();
    check (p.getLibrary().found && p.getLibrary().plugins.size() >= 6, "the library lists the test plugins ("
           + juce::String ((int) p.getLibrary().plugins.size()) + ")");
    check (p.getLibrary().appRunning, "the library's app is seen as running");

    p.choose ("test-poly-saw");
    check (p.loaded() == nullptr && p.getStatus().contains ("Opens in Pangea Instrument"), "an instrument is refused here: \"" + p.getStatus() + "\"");
    p.choose ("test-not-mine");
    check (p.loaded() == nullptr && p.getStatus().contains ("your own plugins"), "someone else's plugin is refused: \"" + p.getStatus() + "\"");

    Run run;
    block (p, run, head);   // nothing loaded: the sound passes through
    check (run.rms (0, kBlock) > 0.05, "with nothing loaded the sound passes through");

    p.choose ("test-tape-drift");
    auto inst = p.loaded();
    check (inst != nullptr && inst->name == "Tape Drift" && inst->version == 1, "Tape Drift v1 opens from the module on disk");
    auto* time = slotNamed (p, "Time");
    auto* mix = slotNamed (p, "Mix");
    // The bank is the first 128 parameters, slots every one. What follows it is no slot.
    int slotsFirst = 0;
    while (slotsFirst < p.getParameters().size() && dynamic_cast<SlotParameter*> (p.getParameters()[slotsFirst]) != nullptr)
        ++slotsFirst;
    check (time != nullptr && mix != nullptr && slotsFirst == kSlots, "slots named after its controls (Time is "
           + (time ? time->paramID : juce::String ("missing")) + ", 128 slots)");
    check (slotNamed (p, "Slot 128") != nullptr, "unused slots are named Slot N");
    // After the bank, one more parameter and no other: Pangea's bypass, which no project can take.
    {
        auto* bypass = dynamic_cast<juce::RangedAudioParameter*> (p.getBypassParameter());
        int highestSlotHint = 0;
        for (int i = 0; i < slotsFirst; ++i)
            highestSlotHint = juce::jmax (highestSlotHint, p.getParameters()[i]->getVersionHint());
        check (p.getParameters().size() == kSlots + 1 && bypass != nullptr && p.getParameters().getLast() == bypass
               && dynamic_cast<SlotParameter*> (bypass) == nullptr && bypass == p.bypassParameter()
               && bypass->getParameterID() == pangea::bypass::parameterId && bypass->getName (100) == pangea::bypass::parameterName
               && bypass->getVersionHint() > highestSlotHint && ! p.bypassParameter()->get(),
               "after the 128 slots comes Pangea's bypass and nothing else: "
               + (bypass != nullptr ? bypass->getParameterID() + ", named " + bypass->getName (100) + ", off, version hint "
                                          + juce::String (bypass->getVersionHint()) + " where the slots have " + juce::String (highestSlotHint)
                                    : juce::String ("missing"))
               + ", " + juce::String (p.getParameters().size()) + " parameters in all");
    }
    const juce::String timeId = time != nullptr ? time->paramID : juce::String();
    setReal (time, 250.0f);
    check (std::abs (realValue (time) - 250.0f) < 0.6f && time->getText (time->getValue(), 20) == "250", "Time set to 250 ms reads back as \"250\"");

    run = {};
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    for (int b = 0; b < (int) (kRate / kBlock); ++b)
        block (p, run, head);
    const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
    check (run.finite() && run.rms (0, run.left.size()) > 0.02, "one second of audio through v1: level " + juce::String (run.rms (0, run.left.size()), 4)
           + ", " + juce::String (ms, 1) + " ms of work");
    const float normalStep = run.largestStep (0, run.left.size());

    // v2 is on Pangea: Refresh asks the app; the app answers; the plugin swaps.
    setHeadVersion ("test-tape-drift", 2);
    p.poll();
    check (p.getStatus().isEmpty() || ! p.getStatus().contains ("v2"), "v2 does not load by itself while Follow is off");
    p.refresh();
    check (p.isBusy() && p.pendingRequestId().isNotEmpty(), "Refresh writes a request");
    const auto req = answerRequest (moduleDirFor ("test-tape-drift", 2), 2, "test-tape-drift");
    check (req.getProperty ("type", {}).toString() == "module" && req.getProperty ("projectId", {}).toString() == "test-tape-drift"
           && req.getProperty ("version", juce::var (1)).isVoid() && ! (bool) req.getProperty ("rebuild", true),
           "the request asks for the newest version of the project: " + juce::JSON::toString (req, true));
    p.poll();
    inst = p.loaded();
    check (inst != nullptr && inst->version == 2, "the answer is read and v2 goes live");
    check (p.fadeRunning(), "v1 fades out while v2 fades in");
    const size_t swapAt = run.left.size();
    for (int b = 0; b < (int) (kRate / kBlock); ++b)
        block (p, run, head);
    check (! p.fadeRunning(), "the fade is over after 30 ms");
    p.poll();
    check (run.finite(), "every sample through the swap is a number");
    const float fadeStep = run.largestStep (swapAt, swapAt + (size_t) (0.04 * kRate));
    check (fadeStep <= normalStep * 1.5f, "no click at the swap: largest step " + juce::String (fadeStep, 4) + " (playing normally "
           + juce::String (normalStep, 4) + ")");
    check (run.rms (swapAt + 4096, run.left.size()) > 0.02, "v2 makes sound: level " + juce::String (run.rms (swapAt + 4096, run.left.size()), 4));

    auto* time2 = slotNamed (p, "Time");
    auto* mix2 = slotNamed (p, "Mix");
    auto* drive = slotNamed (p, "Drive");
    check (time2 != nullptr && time2->paramID == timeId, "Time keeps its slot (" + timeId + "), so its automation follows it");
    check (std::abs (realValue (time2) - 250.0f) < 0.6f, "Time keeps the 250 ms the person set: " + juce::String (realValue (time2), 1));
    check (std::abs (realValue (mix2) - 0.6f) < 0.006f, "Mix, never moved, takes v2's new default: " + juce::String (realValue (mix2), 3));
    check (drive != nullptr && std::abs (realValue (drive) - 2.0f) < 0.01f, "the new Drive control takes a free slot at its default");

    // Refresh again: nothing newer.
    p.refresh();
    answerRequest (moduleDirFor ("test-tape-drift", 2), 2, "test-tape-drift");
    p.poll();
    check (p.loaded() == inst && p.getStatus().contains ("newest"), "Refresh with nothing newer keeps v2: \"" + p.getStatus() + "\"");

    juce::MemoryBlock state;
    p.getStateInformation (state);

    // A session opens again.
    Processor again (HostKind::effect);
    again.prepareToPlay (kRate, kBlock);
    check (again.usesWebPanel(), "a new Pangea draws a version with the component library's panel, unasked (decision 77)");
    again.setStateInformation (state.getData(), (int) state.getSize());
    auto back = again.loaded();
    check (back != nullptr && back->version == 2, "a saved session opens Tape Drift v2 again from disk");
    check (again.usesWebPanel(), "and still with the library's panel");

    // The choice of panel in a session. One saved before decision 77 says web=\"0\" wherever nobody had
    // touched the switch, so that is not a choice and the library's panel is drawn. The shell's own
    // panel, once asked for, is kept.
    {
        const auto sessionWith = [&state] (const std::function<void (juce::XmlElement&)>& change)
        {
            auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
            juce::MemoryBlock changed;
            if (xml != nullptr)
            {
                change (*xml);
                juce::AudioProcessor::copyXmlToBinary (*xml, changed);
            }
            return changed;
        };
        const auto older = sessionWith ([] (juce::XmlElement& x) { x.setAttribute ("web", 0); });
        Processor old (HostKind::effect);
        old.prepareToPlay (kRate, kBlock);
        old.setStateInformation (older.getData(), (int) older.getSize());
        check (older.getSize() > 0 && old.loaded() != nullptr && old.usesWebPanel(),
               "a session saved before the change, with the old switch off, opens with the library's panel");

        again.setWebPanel (false);
        juce::MemoryBlock chosen;
        again.getStateInformation (chosen);
        Processor kept (HostKind::effect);
        kept.prepareToPlay (kRate, kBlock);
        kept.setStateInformation (chosen.getData(), (int) chosen.getSize());
        check (kept.loaded() != nullptr && ! kept.usesWebPanel(), "the shell's own panel, once chosen, is saved with the session");
        again.setWebPanel (true);
    }
    auto* time3 = slotNamed (again, "Time");
    check (time3 != nullptr && time3->paramID == timeId && std::abs (realValue (time3) - 250.0f) < 0.6f,
           "and Time is in the same slot at 250 ms");

    // A song saved with Follow on keeps the version it was saved with (decision 83). A version made while
    // it was closed is held back; one made once it is open again is followed.
    {
        auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
        juce::MemoryBlock following;
        if (xml != nullptr)
        {
            xml->setAttribute ("follow", 1);
            juce::AudioProcessor::copyXmlToBinary (*xml, following);
        }
        setHeadVersion ("test-tape-drift", 3);   // v3 was made while the song was closed
        Processor song (HostKind::effect);
        song.prepareToPlay (kRate, kBlock);
        song.setStateInformation (following.getData(), (int) following.getSize());
        song.poll();
        check (song.followsLatest() && song.loaded() != nullptr && song.loaded()->version == 2 && ! song.isBusy(),
               "a song saved with Follow on opens on its own v2, though v3 was made while it was closed");
        setHeadVersion ("test-tape-drift", 3);   // the app writes the library again, after the song opened
        song.poll();
        check (song.loaded() != nullptr && song.loaded()->version == 2 && ! song.isBusy(),
               "the first library the app writes after the song opens holds v3 back");
        setHeadVersion ("test-tape-drift", 4);   // a version made while the song is open
        song.poll();
        const auto followed = answerRequest (moduleDirFor ("test-tape-drift", 2), 2, "test-tape-drift");
        check ((int) followed.getProperty ("version", 0) == 4,
               "a version made once the song is open is followed: the plugin asks for v4 (" + juce::JSON::toString (followed, true) + ")");
        song.poll();
        juce::MemoryBlock saved;
        song.getStateInformation (saved);
        const auto savedXml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), (int) saved.getSize());
        check (savedXml != nullptr && savedXml->getIntAttribute ("follow") == 1, "Follow stays on in the song when it is saved again");
        setHeadVersion ("test-tape-drift", 2);
    }
    return state;
}

void instrumentChecks()
{
    std::cout << "Pangea Instrument: Poly Saw, Acid Line, Two Hands, Runaway" << std::endl;
    Processor p (HostKind::instrument);
    TestPlayHead head;
    p.setPlayHead (&head);
    p.prepareToPlay (kRate, kBlock);
    p.poll();

    p.choose ("test-poly-saw");
    check (p.loaded() != nullptr && p.loaded()->polyphonic, "Poly Saw opens, with Faust's polyphony (" + juce::String (p.loaded() ? p.loaded()->numVoices : 0) + " voices)");
    check (slotNamed (p, "Cutoff") != nullptr && slotNamed (p, "freq") == nullptr, "its knobs have slots and the voice controls do not");
    Run run;
    juce::MidiBuffer on;
    on.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    on.addEvent (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 100), 0);
    block (p, run, head, on, false);
    for (int b = 0; b < 40; ++b)
        block (p, run, head, {}, false);
    check (run.finite() && run.rms (0, run.left.size()) > 0.01, "a chord played into it sounds: level " + juce::String (run.rms (0, run.left.size()), 4));

    // A held chord carries over to the next version.
    p.choose ("test-acid-line");
    check (p.loaded() != nullptr && p.loaded()->name == "Acid Line" && p.loaded()->code->hasLogic, "Acid Line opens, with its logic module");
    run = {};
    for (int b = 0; b < 200; ++b)
        block (p, run, head, {}, false);
    check (run.finite() && run.rms (0, run.left.size()) > 0.01, "its logic module plays it from the transport: level " + juce::String (run.rms (0, run.left.size()), 4));

    p.choose ("test-two-hands");
    check (p.loaded() != nullptr && p.loaded()->isMidiTool(), "Two Hands, a MIDI tool, opens in Pangea Instrument");
    run = {};
    head.ppq = 0.0;
    head.playing = true;
    for (int b = 0; b < (int) (8 * 2.0 * kRate / kBlock); ++b)   // 8 bars at 120 BPM
        block (p, run, head, {}, false);
    check (run.ons > 8, "its notes come out as MIDI: " + juce::String (run.ons) + " notes in 8 bars");
    head.playing = false;
    block (p, run, head, {}, false);
    block (p, run, head, {}, false);
    check (run.sounding.empty() && run.ons == run.offs, "every note ends when the transport stops (" + juce::String (run.ons) + " on, "
           + juce::String (run.offs) + " off)");
    check (run.rms (0, run.left.size()) == 0.0, "a MIDI tool makes no sound");

    // A module that never returns: its own guard stops it, inside the module.
    p.choose ("test-runaway");
    check (p.loaded() != nullptr && p.loaded()->name == "Runaway", "Runaway opens");
    setReal (slotNamed (p, "Fault"), 1.0f);    // an endless loop
    setReal (slotNamed (p, "At bar"), 1.0f);
    head.ppq = 0.0;
    head.playing = true;
    run = {};
    double slowest = 0.0;
    for (int b = 0; b < 200; ++b)
    {
        const auto t = juce::Time::getMillisecondCounterHiRes();
        block (p, run, head, {}, false);
        slowest = juce::jmax (slowest, juce::Time::getMillisecondCounterHiRes() - t);
    }
    const int stopped = p.loaded() != nullptr ? p.loaded()->display.stopped.load() : -1;
    check (stopped == 1, "its endless loop is stopped for running too long (stopped = " + juce::String (stopped) + "); slowest block "
           + juce::String (slowest, 1) + " ms");
    check (run.sounding.empty(), "and the note it left sounding is ended");
}

// The song (decision 123): Song Gate, decision 123's song-synced example, declares a control
// marked [pangea:playing] and one marked [pangea:beat]. The Pangea plugin sets them from the play
// head before every block, as a plugin built on its own does (pushHost), and neither has a slot.
void transportChecks()
{
    std::cout << "Pangea (effect): Song Gate, a gate that follows the song" << std::endl;
    Processor p (HostKind::effect);
    TestPlayHead head;
    head.ppq = 8.0;   // bar 3
    p.setPlayHead (&head);
    p.prepareToPlay (kRate, kBlock);
    p.poll();
    p.choose ("test-song-gate");
    const auto inst = p.loaded();
    check (inst != nullptr && inst->transport.size() == 2 && inst->tempo.size() == 1,
           "Song Gate opens with its play state, its song position and its tempo bound to the host");
    if (inst == nullptr || inst->transport.size() != 2)
        return;
    check (slotNamed (p, "Playing") == nullptr && slotNamed (p, "Beat") == nullptr && slotNamed (p, "Depth") != nullptr,
           "the song's two controls have no slot, so a DAW never lists them; Depth has one");
    auto held = [&inst] (bool beat)
    {
        const auto what = beat ? Instance::SongValue::beat : Instance::SongValue::playing;
        for (const auto& t : inst->transport)
            if (t.what == what && ! t.zones.empty())
                return (double) *t.zones.front();
        return std::numeric_limits<double>::quiet_NaN();
    };
    Run run;
    const double start = head.ppq;
    block (p, run, head);
    check (held (false) == 1.0 && std::abs (held (true) - start) < 1.0e-4,
           "playing from bar 3: Playing " + juce::String (held (false), 0) + ", Beat " + juce::String (held (true), 4));
    const double next = head.ppq;
    block (p, run, head);
    check (std::abs (held (true) - next) < 1.0e-4,
           "the next block starts where the song is: Beat " + juce::String (held (true), 4) + " (expected " + juce::String (next, 4) + ")");
    head.playing = false;
    head.ppq = 33.5;
    block (p, run, head);
    block (p, run, head);
    check (held (false) == 0.0 && std::abs (held (true) - 33.5) < 1.0e-4,
           "stopped at beat 33.5: Playing " + juce::String (held (false), 0) + ", Beat " + juce::String (held (true), 4));
    check (run.finite(), "and its sound stays a number throughout");
}

// An instrument's effect section, the stage its voices share (decision 73): Dub Keys has an echo, a
// level and the safety clip there, a Tone control that its voices read too, and the host's tempo.
void effectSectionChecks()
{
    std::cout << "Pangea Instrument: Dub Keys, an instrument with an effect section" << std::endl;
    // An instrument hears no input, so the test signal carries on from here for the checks after these.
    const long signalWas = signalPos;
    {
        Processor p (HostKind::instrument);
        TestPlayHead head;
        head.bpm = 96.0;
        p.setPlayHead (&head);
        p.prepareToPlay (kRate, kBlock);
        p.poll();
        p.choose ("test-dub-keys");
        const auto inst = p.loaded();
        check (inst != nullptr && inst->polyphonic && inst->effectMaps.size() == 1,
               "Dub Keys opens, with Faust's polyphony and the effect section its module carries");
        if (inst == nullptr)
            return;
        check (slotNamed (p, "Attack") != nullptr && slotNamed (p, "Mix") != nullptr && slotNamed (p, "Level") != nullptr
               && slotNamed (p, "BPM") == nullptr,
               "the effect's controls have slots beside the voice's (Mix is " + (slotNamed (p, "Mix") ? slotNamed (p, "Mix")->paramID : juce::String ("missing"))
               + ", Level is " + (slotNamed (p, "Level") ? slotNamed (p, "Level")->paramID : juce::String ("missing")) + "), and the host's tempo has none");
        int toneSlots = 0;
        for (auto* param : p.getParameters())
            if (param->getName (100) == "Tone")
                ++toneSlots;
        size_t toneZones = 0;
        for (const auto& b : inst->bindings)
            if (b.control->label == "Tone")
                toneZones = b.zones.size();
        check (toneSlots == 1 && toneZones == (size_t) inst->numVoices + 1,
               "Tone, which the voices and the effect both read, is one slot that sets all " + juce::String ((int) toneZones) + " of its places");
        Run run;
        block (p, run, head, {}, false);
        check (inst->tempo.size() == 1 && inst->tempo[0].zones.size() == 1 && std::abs ((double) *inst->tempo[0].zones[0] - 96.0) < 0.01,
               "the echo's tempo, a control of the effect, follows the host: "
               + (inst->tempo.size() == 1 && inst->tempo[0].zones.size() == 1 ? juce::String (*inst->tempo[0].zones[0], 1) : juce::String ("not bound")) + " BPM");

        p.choose ("test-poly-saw");
        check (p.loaded() != nullptr && p.loaded()->name == "Poly Saw" && p.loaded()->effectMaps.empty(), "Poly Saw, which has no effect section, opens without one");
    }

    // A chord held for a quarter of a second, then left to ring for two, in a fresh plugin each
    // time, with one control of the effect moved first.
    const size_t held = (size_t) (0.25 * kRate / kBlock) * kBlock;
    auto chord = [held] (const juce::String& control, float value)
    {
        Processor p (HostKind::instrument);
        TestPlayHead head;
        p.setPlayHead (&head);
        p.prepareToPlay (kRate, kBlock);
        p.poll();
        p.choose ("test-dub-keys");
        if (control.isNotEmpty())
            setReal (slotNamed (p, control), value);
        Run run;
        block (p, run, head, {}, false);   // the version settles in before the first note
        run = {};
        juce::MidiBuffer on, off;
        for (int note : { 60, 64, 67 })
        {
            on.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), 0);
            off.addEvent (juce::MidiMessage::noteOff (1, note), 0);
        }
        block (p, run, head, on, false);
        while (run.left.size() < held)
            block (p, run, head, {}, false);
        block (p, run, head, off, false);
        while (run.left.size() < held + (size_t) (2.0 * kRate))
            block (p, run, head, {}, false);
        return run;
    };
    const auto usual = chord ({}, 0.0f);
    const auto quiet = chord ("Level", -30.0f);
    const auto dry = chord ("Mix", 0.0f);
    const double loud = usual.rms (0, held), soft = quiet.rms (0, held);
    check (usual.finite() && loud > 0.01, "a chord played into it sounds: level " + juce::String (loud, 4));
    check (soft > 0.0 && std::abs (juce::Decibels::gainToDecibels (soft / loud) + 24.0) < 0.5,
           "Level, a control of the effect, turned from -6 to -30 dB lowers the same chord by "
           + juce::String (-juce::Decibels::gainToDecibels (soft / loud), 1) + " dB");
    // Long after the voices have gone, only the echo is left: the effect's own sound.
    const size_t from = held + (size_t) (1.5 * kRate), to = held + (size_t) (2.0 * kRate);
    check (usual.rms (from, to) > 1.0e-4 && dry.rms (from, to) < 1.0e-6,
           "the echo rings on after the voices have ended (level " + juce::String (usual.rms (from, to), 5)
           + "), and with Mix at 0 there is nothing (level " + juce::String (dry.rms (from, to), 7) + ")");

    // What the voice declared must never reach a control of the effect. The plugin frees the voice
    // it reads before it makes the effect, so a control of the effect may sit at the address one
    // of the voice's had. Here one zone stands for both.
    {
        pangea::ControlCollector collector;
        FAUSTFLOAT zone = 0;
        collector.openVerticalBox ("Check");
        collector.declare (&zone, "unit", "Hz");
        collector.declare (&zone, "scale", "log");
        collector.declare (&zone, "style", "knob");
        collector.declare (&zone, "logic", "1");
        collector.addHorizontalSlider ("Cutoff", &zone, 1000, 100, 10000, 1);
        collector.closeBox();
        collector.beginEffect();
        collector.openVerticalBox ("Check");
        collector.addHorizontalSlider ("Width", &zone, 0.5f, 0, 1, 0.01f);
        collector.closeBox();
        const bool two = collector.controls.size() == 2;
        check (two && collector.controls[0].unit == "Hz" && collector.controls[0].logScale && collector.controls[0].logicDriven
               && collector.controls[1].label == "Width" && collector.controls[1].unit.isEmpty() && ! collector.controls[1].logScale
               && ! collector.controls[1].knob && collector.controls[1].style.isEmpty() && ! collector.controls[1].logicDriven,
               "a control of the effect at the address a voice control had takes none of what the voice declared there");
    }
    signalPos = signalWas;
}

void midiFxChecks()
{
    std::cout << "Pangea MIDI FX: Two Hands" << std::endl;
    Processor p (HostKind::midiFx);
    TestPlayHead head;
    p.setPlayHead (&head);
    p.prepareToPlay (kRate, kBlock);
    p.poll();
    p.choose ("test-tape-drift");
    check (p.loaded() == nullptr, "an effect is refused here");
    p.choose ("test-two-hands");
    check (p.loaded() != nullptr && p.isMidiEffect() && p.getTotalNumOutputChannels() == 0, "Two Hands opens in the MIDI effect, which has no audio");
    Run run;
    for (int b = 0; b < (int) (4 * 2.0 * kRate / kBlock); ++b)
        block (p, run, head, {}, false);
    check (run.ons > 4, "its notes come out: " + juce::String (run.ons) + " in 4 bars");
}

void rebuildChecks()
{
    std::cout << "A module built for another plugin version" << std::endl;
    Processor p (HostKind::effect);
    p.prepareToPlay (kRate, kBlock);
    p.poll();
    p.choose ("test-old-abi");
    check (p.loaded() == nullptr && p.isBusy(), "it is not loaded; the app is asked again");
    const auto files = host().getChildFile ("requests").findChildFiles (juce::File::findFiles, false, "*.json");
    const auto json = files.size() == 1 ? juce::JSON::parse (files[0].loadFileAsString()) : juce::var();
    check ((bool) json.getProperty ("rebuild", false) && (int) json.getProperty ("version", 0) == 1, "with rebuild set, for the same version");
    for (const auto& f : files)
        f.deleteFile();
}

// ---- Pangea's bypass (PangeaBypass.h; decision 75) ----------------------------------------------

/** What a run of blocks gave: the input it was fed, both sides of the output, the MIDI that came out. */
struct Take
{
    std::vector<float> in[2], out[2];
    std::vector<pangea::bypasscheck::Message> sent;
    bool finite = true;

    size_t length() const { return out[0].size(); }
    float peak (size_t from, size_t to) const
    {
        float p = 0.0f;
        for (size_t i = from; i < juce::jmin (to, out[0].size()); ++i)
            p = juce::jmax (p, std::abs (out[0][i]), std::abs (out[1][i]));
        return p;
    }
    /** How far the output is from the input over [from, to): nothing, for an effect that is bypassed. */
    float fromInput (size_t from, size_t to) const
    {
        float apart = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
            for (size_t i = from; i < juce::jmin (to, out[ch].size()); ++i)
                apart = juce::jmax (apart, std::abs (out[ch][i] - in[ch][i]));
        return apart;
    }
};

/** One block of a take: two smooth tones that differ into an effect, the MIDI given into the
    others, through processBlock or, for a host that calls it, processBlockBypassed. */
void takeBlock (juce::AudioProcessor& p, Take& take, TestPlayHead& head, juce::MidiBuffer midi = {}, bool bypassedCall = false)
{
    const int channels = juce::jmax (2, juce::jmax (p.getTotalNumInputChannels(), p.getTotalNumOutputChannels()));
    juce::AudioBuffer<float> buffer (channels, kBlock);
    buffer.clear();
    const auto start = (long long) take.length();
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < kBlock; ++i)
        {
            const float x = p.getTotalNumInputChannels() > ch ? pangea::bypasscheck::inputAt (ch, start + i, kRate) : 0.0f;
            buffer.setSample (ch, i, x);
            take.in[ch].push_back (x);
        }
    if (bypassedCall)
        p.processBlockBypassed (buffer, midi);
    else
        p.processBlock (buffer, midi);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < kBlock; ++i)
        {
            const float y = buffer.getSample (ch, i);
            take.finite = take.finite && std::isfinite (y);
            take.out[ch].push_back (y);
        }
    using Kind = pangea::bypasscheck::Message::Kind;
    for (const auto metadata : midi)
    {
        const auto m = metadata.getMessage();
        const long long at = start + metadata.samplePosition;
        if (m.isNoteOn()) take.sent.push_back ({ at, Kind::noteOn, m.getChannel(), m.getNoteNumber(), m.getVelocity() });
        else if (m.isNoteOff()) take.sent.push_back ({ at, Kind::noteOff, m.getChannel(), m.getNoteNumber(), 0 });
        else if (m.isController()) take.sent.push_back ({ at, Kind::controller, m.getChannel(), m.getControllerNumber(), m.getControllerValue() });
    }
    head.advance (kBlock);
}

/** A plugin of one kind, prepared, with a project open when one is named. */
std::unique_ptr<Processor> openPlugin (HostKind kind, TestPlayHead& head, const juce::String& project)
{
    auto p = std::make_unique<Processor> (kind);
    p->setPlayHead (&head);
    p->prepareToPlay (kRate, kBlock);
    p->poll();
    if (project.isNotEmpty())
        p->choose (project);
    return p;
}

/** How one take stands against the plugin's own render and what a bypassed plugin gives, on both sides. */
pangea::bypasscheck::Held heldBoth (const Take& take, const Take& usual, bool dryIsInput, const std::vector<float>& gains, bool wetKnown = true)
{
    pangea::bypasscheck::Held worst;
    for (int ch = 0; ch < 2; ++ch)
        worst.worse (pangea::bypasscheck::holdTo (take.out[ch], usual.out[ch], dryIsInput ? take.in[ch] : std::vector<float>(), gains, kRate, wetKnown));
    return worst;
}

/** The voices of the open version that hold a note. */
int voicesHolding (Processor& p)
{
    int n = 0;
    if (auto inst = p.loaded(); inst != nullptr && inst->poly != nullptr)
        for (auto* voice : inst->poly->fVoiceTable)
            n += (voice->fCurNote >= 0 || voice->fCurNote == kLegatoVoice) ? 1 : 0;
    return n;
}

juce::MidiBuffer chordOn()
{
    juce::MidiBuffer midi;
    for (int note : { 60, 64, 67 })
        midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), 0);
    return midi;
}

void bypassChecks()
{
    using pangea::bypasscheck::gainsFor;
    const int fade = pangea::bypasscheck::fadeSamples (kRate);
    const int blocks = 200, on = 60, off = 130;
    const std::vector<std::pair<int, bool>> switches { { on, true }, { off, false } };
    const auto gains = gainsFor (kRate, kBlock, blocks, false, switches);
    const auto bypassedFor = (size_t) ((off - on) * kBlock - fade + 1);

    // ---- An effect: Tape Drift in Pangea ----
    std::cout << "Pangea (effect): its own bypass, with Tape Drift open" << std::endl;
    {
        Take usual;
        {
            TestPlayHead head;
            auto p = openPlugin (HostKind::effect, head, "test-tape-drift");
            check (p->loaded() != nullptr, "Tape Drift opens");
            for (int b = 0; b < blocks; ++b)
                takeBlock (*p, usual, head);
        }
        check (usual.finite && usual.fromInput (0, usual.length()) > 0.02f, "with the bypass off it is heard: its render is "
               + juce::String (usual.fromInput (0, usual.length()), 4) + " from its input at the furthest");

        // Switched by the parameter in the middle of a render.
        Take switched;
        {
            TestPlayHead head;
            auto p = openPlugin (HostKind::effect, head, "test-tape-drift");
            for (int b = 0; b < blocks; ++b)
            {
                if (b == on) p->bypassParameter()->setValueNotifyingHost (1.0f);
                if (b == off) p->bypassParameter()->setValueNotifyingHost (0.0f);
                takeBlock (*p, switched, head);
            }
        }
        const auto h = heldBoth (switched, usual, true, gains);
        check (switched.finite && h.apartAtRest <= 0.0f && h.bypassedSamples == bypassedFor,
               "bypassed in the middle, it is its own render to the bit before the bypass and after it, tails and all, and its input between ("
               + juce::String ((int) h.bypassedSamples) + " samples)");
        check (h.fadeSamples == (size_t) (2 * (fade - 1)) && h.apartInFades <= pangea::bypasscheck::kMixed && h.overAllowed <= 0.0f,
               "each switch is a fade of " + juce::String (fade) + " samples, 30 ms, with no step larger than the fade allows: the largest is "
               + juce::String (h.largestStep, 5) + ", where the two sounds' own largest there is " + juce::String (h.ownStep, 5));

        // A host that calls processBlockBypassed instead.
        {
            TestPlayHead head;
            auto p = openPlugin (HostKind::effect, head, "test-tape-drift");
            Take take;
            for (int b = 0; b < blocks; ++b)
                takeBlock (*p, take, head, {}, b >= on && b < off);
            check (take.out[0] == switched.out[0] && take.out[1] == switched.out[1], "processBlockBypassed does what the parameter does, sample for sample");
        }

        // Bypassed with nothing open, then Tape Drift opened: bypassed from its first sample. With
        // the bypass off, it is heard.
        {
            TestPlayHead head;
            auto p = openPlugin (HostKind::effect, head, {});
            p->bypassParameter()->setValueNotifyingHost (1.0f);
            Take take;
            for (int b = 0; b < 10; ++b)
                takeBlock (*p, take, head);
            p->choose ("test-tape-drift");
            for (int b = 0; b < 40; ++b)
                takeBlock (*p, take, head);
            const size_t bypassedUntil = take.length();
            p->bypassParameter()->setValueNotifyingHost (0.0f);
            for (int b = 0; b < 60; ++b)
                takeBlock (*p, take, head);
            check (p->loaded() != nullptr && take.fromInput (0, bypassedUntil) <= 0.0f && take.fromInput (bypassedUntil, take.length()) > 0.02f,
                   "a plugin opened while Pangea is bypassed is bypassed from its first sample, and is heard once the bypass comes off");
        }

        // A new version comes in while bypassed: the crossfade between versions is not heard either.
        {
            TestPlayHead head;
            auto p = openPlugin (HostKind::effect, head, {});
            p->load (moduleDirFor ("test-tape-drift", 1), Processor::Mode::fresh);
            Take take;
            for (int b = 0; b < 20; ++b)
                takeBlock (*p, take, head);
            p->bypassParameter()->setValueNotifyingHost (1.0f);
            for (int b = 0; b < 10; ++b)
                takeBlock (*p, take, head);
            const size_t from = take.length();
            p->load (moduleDirFor ("test-tape-drift", 2), Processor::Mode::swap);
            const bool fading = p->fadeRunning();
            for (int b = 0; b < 20; ++b)
                takeBlock (*p, take, head);
            check (fading && ! p->fadeRunning() && p->loaded() != nullptr && p->loaded()->version == 2 && take.fromInput (from, take.length()) <= 0.0f,
                   "a new version that comes in while bypassed changes nothing that is heard: the input still passes to the bit");
        }

        // Saved with the session, and a session saved before the bypass opens with the plugin in.
        {
            TestPlayHead head;
            auto p = openPlugin (HostKind::effect, head, "test-tape-drift");
            p->bypassParameter()->setValueNotifyingHost (1.0f);
            juce::MemoryBlock state;
            p->getStateInformation (state);
            auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
            const auto values = xml != nullptr && xml->getChildByName ("Values") != nullptr
                                    ? juce::StringArray::fromTokens (xml->getChildByName ("Values")->getAllSubText(), " ", "") : juce::StringArray();
            check (xml != nullptr && xml->getIntAttribute ("bypass", -1) == 1 && values.size() == kSlots,
                   "the session holds the bypass by name, beside the 128 slots' values");

            TestPlayHead headAgain;
            Processor again (HostKind::effect);
            again.setPlayHead (&headAgain);
            again.prepareToPlay (kRate, kBlock);
            again.setStateInformation (state.getData(), (int) state.getSize());
            Take take;
            for (int b = 0; b < 20; ++b)
                takeBlock (again, take, headAgain);
            check (again.loaded() != nullptr && again.bypassParameter()->get() && take.fromInput (0, take.length()) <= 0.0f,
                   "a session saved bypassed opens bypassed: the input passes from the first sample");

            // The same session as it was written before the plugin had a bypass.
            if (xml != nullptr)
            {
                xml->removeAttribute ("bypass");
                juce::MemoryBlock older;
                juce::AudioProcessor::copyXmlToBinary (*xml, older);
                Processor fresh (HostKind::effect), bypassed (HostKind::effect);
                for (auto* q : { &fresh, &bypassed })
                    q->prepareToPlay (kRate, kBlock);
                bypassed.bypassParameter()->setValueNotifyingHost (1.0f);
                fresh.setStateInformation (older.getData(), (int) older.getSize());
                bypassed.setStateInformation (older.getData(), (int) older.getSize());
                check (fresh.loaded() != nullptr && ! fresh.bypassParameter()->get() && ! bypassed.bypassParameter()->get(),
                       "a session saved before Pangea had its bypass opens with the plugin in, in a new plugin and in one that was bypassed");
            }
        }

        // The panel drawn natively, which shows when the person asks for the shell's own in place
        // of the component library's (decision 77): its switch and the parameter move each other,
        // and the controls are drawn fainter.
        {
            TestPlayHead head;
            auto p = openPlugin (HostKind::effect, head, "test-tape-drift");
            p->setWebPanel (false);
            std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
            auto* window = dynamic_cast<HostEditor*> (ed.get());
            auto* panel = window != nullptr ? window->nativePanel() : nullptr;
            auto* button = panel != nullptr ? panel->getBypassSwitch() : nullptr;
            check (button != nullptr && ! button->getToggleState() && ! panel->showsBypassed(),
                   "the natively drawn panel has the bypass switch in its title row, off");
            if (button != nullptr)
            {
                button->setToggleState (true, juce::sendNotificationSync);   // a press, as the switch tells its listeners of one
                const bool pressed = p->bypassParameter()->get() && panel->showsBypassed();
                p->bypassParameter()->setValueNotifyingHost (0.0f);
                check (pressed && ! button->getToggleState() && ! panel->showsBypassed(),
                       "a press on it bypasses the plugin and the controls are drawn fainter, and a change of the parameter from outside moves it back");
            }
        }

        // No project can take it: the slots are given out among the 128 only.
        {
            TestPlayHead head;
            auto p = openPlugin (HostKind::effect, head, "test-tape-drift");
            auto* bypass = p->bypassParameter();
            p->load (moduleDirFor ("test-tape-drift", 1), Processor::Mode::swap);
            check (p->getBypassParameter() == bypass && p->getParameters().getLast() == bypass && bypass->getName (100) == "Bypass"
                   && slotNamed (*p, "Bypass") == nullptr && p->getParameters().size() == kSlots + 1,
                   "opening and changing versions leaves the bypass as it is: still the last parameter, and no slot");
        }
    }

    // ---- An instrument: Poly Saw in Pangea Instrument ----
    std::cout << "Pangea Instrument: its own bypass, with Poly Saw, Acid Line and Two Hands" << std::endl;
    {
        // Three notes held from the second block on (a note in a version's first block is another matter).
        auto run = [&] (bool bypassIt, bool bypassedCall, int* holdingBefore, int* holdingAfter, int* holdingWhilePlayed)
        {
            TestPlayHead head;
            auto p = openPlugin (HostKind::instrument, head, "test-poly-saw");
            Take take;
            for (int b = 0; b < blocks; ++b)
            {
                if (bypassIt && ! bypassedCall && b == on) p->bypassParameter()->setValueNotifyingHost (1.0f);
                if (bypassIt && ! bypassedCall && b == off) p->bypassParameter()->setValueNotifyingHost (0.0f);
                juce::MidiBuffer midi;
                if (b == 1) midi = chordOn();
                if (bypassIt && b == on + 20) midi.addEvent (juce::MidiMessage::noteOn (1, 72, (juce::uint8) 110), 0);
                if (bypassIt && b == off + 10) midi.addEvent (juce::MidiMessage::noteOn (1, 62, (juce::uint8) 110), 0);
                takeBlock (*p, take, head, midi, bypassIt && bypassedCall && b >= on && b < off);
                if (holdingBefore != nullptr && b == on - 1) *holdingBefore = voicesHolding (*p);
                if (holdingAfter != nullptr && b == on + fade / kBlock + 2) *holdingAfter = voicesHolding (*p);
                if (holdingWhilePlayed != nullptr && b == on + 25) *holdingWhilePlayed = voicesHolding (*p);
            }
            return std::make_pair (take, voicesHolding (*p));
        };
        const auto usual = run (false, false, nullptr, nullptr, nullptr).first;
        int before = -1, after = -1, whilePlayed = -1;
        const auto bypassedRun = run (true, false, &before, &after, &whilePlayed);
        const auto& bypassed = bypassedRun.first;
        // Held as far as the bypass's end: after it another note is played.
        Take untilOff = bypassed;
        for (int ch = 0; ch < 2; ++ch)
            untilOff.out[ch].resize ((size_t) off * kBlock);
        const auto h = heldBoth (untilOff, usual, false, gains);
        check (usual.peak (0, usual.length()) > 0.01f && bypassed.finite && h.apartAtRest <= 0.0f && h.bypassedSamples == bypassedFor,
               "Poly Saw, bypassed while three notes are held, is its own render to the bit up to the bypass and silent after the fade ("
               + juce::String ((int) h.bypassedSamples) + " samples of nothing)");
        check (h.fadeSamples == (size_t) (fade - 1) && h.apartInFades <= pangea::bypasscheck::kMixed && h.overAllowed <= 0.0f,
               "the way to silence is a fade of " + juce::String (fade) + " samples with no step larger than the fade allows: the largest is "
               + juce::String (h.largestStep, 5) + ", where its own largest there is " + juce::String (h.ownStep, 5));
        check (before == 3 && after == 0 && whilePlayed == 0,
               juce::String (before) + " voices hold notes before the bypass, none once its fade is over, and a note played while bypassed starts none");
        check (bypassed.peak ((size_t) (off + 10) * kBlock, bypassed.length()) > 0.01f && bypassedRun.second == 1,
               "with the bypass off again the next note plays, and it alone has a voice: level "
               + juce::String (bypassed.peak ((size_t) (off + 10) * kBlock, bypassed.length()), 4));
        const auto called = run (true, true, nullptr, nullptr, nullptr).first;
        check (called.out[0] == bypassed.out[0] && called.out[1] == bypassed.out[1], "processBlockBypassed does what the parameter does, sample for sample");

        // What was held when the bypass began is let go, and a note played while bypassed is never
        // held. So another instrument opened later is played none of them, though the keys are
        // still down: while Pangea is still bypassed, and after the bypass has come off.
        for (const bool stillBypassed : { true, false })
        {
            TestPlayHead head;
            auto p = openPlugin (HostKind::instrument, head, "test-poly-saw");
            Take take;
            takeBlock (*p, take, head);
            takeBlock (*p, take, head, chordOn());
            for (int b = 0; b < 20; ++b)
                takeBlock (*p, take, head);
            p->bypassParameter()->setValueNotifyingHost (1.0f);
            for (int b = 0; b < 10; ++b)
                takeBlock (*p, take, head);
            juce::MidiBuffer playedWhile;
            playedWhile.addEvent (juce::MidiMessage::noteOn (1, 72, (juce::uint8) 110), 0);
            takeBlock (*p, take, head, playedWhile);
            // Long enough for the voices the bypass ended to ring out, unheard.
            for (int b = 0; b < 80; ++b)
                takeBlock (*p, take, head);
            if (! stillBypassed)
            {
                p->bypassParameter()->setValueNotifyingHost (0.0f);
                for (int b = 0; b < 10; ++b)
                    takeBlock (*p, take, head);
            }
            const int holdingBefore = voicesHolding (*p);
            const size_t opened = take.length();
            p->choose ("test-dub-keys");
            for (int b = 0; b < 10; ++b)
                takeBlock (*p, take, head);
            p->bypassParameter()->setValueNotifyingHost (0.0f);
            for (int b = 0; b < 40; ++b)
                takeBlock (*p, take, head);
            check (p->loaded() != nullptr && p->loaded()->name == "Dub Keys" && holdingBefore == 0 && voicesHolding (*p) == 0
                   && take.peak (opened, take.length()) < 1.0e-6f,
                   juce::String ("an instrument opened ") + (stillBypassed ? "while Pangea is bypassed" : "after the bypass has come off")
                   + " is played none of the notes that were held when it began or played meanwhile: it is silent (level "
                   + juce::String (take.peak (opened, take.length()), 7) + ")");
        }
    }

    // ---- An instrument that its logic module plays: Acid Line ----
    {
        auto run = [&] (bool bypassIt, bool* playingWhile, int* notesWhile)
        {
            TestPlayHead head;
            auto p = openPlugin (HostKind::instrument, head, "test-acid-line");
            Take take;
            for (int b = 0; b < blocks; ++b)
            {
                if (bypassIt && b == on) p->bypassParameter()->setValueNotifyingHost (1.0f);
                if (bypassIt && b == off) p->bypassParameter()->setValueNotifyingHost (0.0f);
                takeBlock (*p, take, head);
                if (bypassIt && b == off - 1)
                {
                    *playingWhile = p->loaded()->display.playing.load();
                    *notesWhile = p->loaded()->logicNotes.count();
                }
            }
            return std::make_pair (take, p->loaded()->display.playing.load());
        };
        bool playingWhile = true;
        int notesWhile = -1;
        const auto usual = run (false, nullptr, nullptr).first;
        const auto bypassedRun = run (true, &playingWhile, &notesWhile);
        const auto h = heldBoth (bypassedRun.first, usual, false, gains, false);
        check (usual.peak (0, usual.length()) > 0.01f && h.apartAtRest <= 0.0f && h.bypassedSamples == bypassedFor,
               "Acid Line, which its logic module plays, is its own render to the bit up to the bypass and silent after the fade ("
               + juce::String ((int) h.bypassedSamples) + " samples of nothing)");
        check (! playingWhile && notesWhile == 0 && bypassedRun.second,
               "while bypassed its module is run as if the transport had stopped, with none of its notes on, and as playing again after");
        check (bypassedRun.first.peak ((size_t) (off + 10) * kBlock, bypassedRun.first.length()) > 0.01f,
               "with the bypass off again it plays: level " + juce::String (bypassedRun.first.peak ((size_t) (off + 10) * kBlock, bypassedRun.first.length()), 4));
    }

    // ---- A MIDI tool: Two Hands, in Pangea Instrument and in Pangea MIDI FX ----
    for (const auto kind : { HostKind::instrument, HostKind::midiFx })
    {
        using namespace pangea::bypasscheck;
        const juce::String where = kind == HostKind::midiFx ? "in Pangea MIDI FX" : "in Pangea Instrument";
        const int toolBlocks = 750;   // four bars at 120 BPM
        // `from` and `to` are the blocks the tool is bypassed for (none when from is negative).
        auto run = [&] (int from, int to, bool bypassedCall, long long playedAt, long long controllerAt)
        {
            TestPlayHead head;
            auto p = openPlugin (kind, head, "test-two-hands");
            Take take;
            for (int b = 0; b < toolBlocks + 3; ++b)
            {
                head.playing = b < toolBlocks;   // then the transport stops, so every note can end
                if (! bypassedCall && b == from) p->bypassParameter()->setValueNotifyingHost (1.0f);
                if (! bypassedCall && b == to) p->bypassParameter()->setValueNotifyingHost (0.0f);
                juce::MidiBuffer midi;
                const long long start = (long long) b * kBlock;
                if (playedAt >= start && playedAt < start + kBlock)
                    midi.addEvent (juce::MidiMessage::noteOn (kPlayedChannel, kPlayedNote, (juce::uint8) kPlayedVelocity), (int) (playedAt - start));
                if (controllerAt >= start && controllerAt < start + kBlock)
                    midi.addEvent (juce::MidiMessage::controllerEvent (kPlayedChannel, kPlayedController, kPlayedControllerValue), (int) (controllerAt - start));
                takeBlock (*p, take, head, midi, bypassedCall && b >= from && b < to);
            }
            return take;
        };
        const auto usual = run (-1, -1, false, -1, -1);
        bool midNote = false;
        const int from = blockInsideANote (usual.sent, kBlock, 200, midNote), to = from + 200;
        const long long onSample = (long long) from * kBlock, offSample = (long long) to * kBlock;
        const long long playedAt = onSample + 2 * kBlock + 7, controllerAt = onSample + 3 * kBlock + 11;
        const auto take = run (from, to, false, playedAt, controllerAt);
        const auto t = holdTool (usual.sent, take.sent, onSample, offSample, playedAt, controllerAt);
        check (midNote && t.sameBefore && t.endedOwn && t.ownOnsWhile == 0,
               "Two Hands " + where + ": bypassed in the middle of its notes, the " + juce::String (t.wereOn)
               + " it had on end on the bypass's first sample, and it starts none while bypassed");
        check (t.passedNote && t.passedController && t.endedPassed,
               "a note and a controller sent to it while bypassed come out on their own samples, and that note is ended when the bypass ends");
        check (t.onsAfter > 0 && t.leftOn == 0 && t.strayOffs == 0 && t.backInTime,
               "after the bypass it plays on in time (" + juce::String (t.onsAfter) + " notes, the ones it plays there with no bypass), and nothing is left on");
        const auto called = run (from, to, true, playedAt, controllerAt);
        check (called.sent == take.sent, "processBlockBypassed does what the parameter does, message for message");
    }
}

// ---- The list: where each plugin is filed, the search and the category menu (decision 85) ----

juce::String joined (const juce::StringArray& words) { return words.joinIntoString (", "); }

juce::String namesOf (const std::vector<Entry>& entries)
{
    juce::StringArray names;
    for (const auto& e : entries)
        names.add (e.name);
    return joined (names);
}

juce::String countsOf (const std::vector<CategoryCount>& categories)
{
    juce::StringArray lines;
    for (const auto& c : categories)
        lines.add (c.name + " " + juce::String (c.count));
    return joined (lines);
}

/** With PANGEA_HOST_SHOTS set to a folder, writes a picture of the window there, for a person to
    look at the list without a DAW. Nothing is checked on it. */
void shoot (juce::Component& window, const juce::String& name)
{
    const auto folder = juce::SystemStats::getEnvironmentVariable ("PANGEA_HOST_SHOTS", {});
    if (folder.isEmpty())
        return;
    const auto file = juce::File (folder).getChildFile (name + ".png");
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    const auto image = window.createComponentSnapshot (window.getLocalBounds(), true, 2.0f);
    if (juce::FileOutputStream out (file); out.openedOk() && juce::PNGImageFormat().writeImageToStream (image, out))
        std::cout << "  shot  " << file.getFullPathName() << std::endl;
}

void listChecks()
{
    std::cout << "The list: where each plugin is filed, the search and the category menu (decision 85)" << std::endl;

    // The rule, held to the cases the site's tests hold its copy to (src/lib/catalogue/catalogue.test.ts).
    check (joined (searchTextWords ("Lo-fi tape, FET-76!")) == "lo, fi, lofi, tape, fet, 76, fet76",
           "a hyphenated word in a plugin's text is its parts and the parts joined: " + joined (searchTextWords ("Lo-fi tape, FET-76!")));
    check (joined (searchQueryWords ("  Lo-Fi  DELAYS ")) == "lofi, delay" && joined (searchQueryWords ("hi-hats")) == "hihat",
           "a typed hyphenated word is one word, and a plural is its singular: " + joined (searchQueryWords ("  Lo-Fi  DELAYS ")));
    check (joined (searchQueryWords ("bus eq")) == "bus, eq" && searchQueryWords (" ,- ").isEmpty(),
           "a word of three letters keeps its s, and punctuation alone is no word");
    check (joined (searchQueryWords ("Equalizer")) == "equaliser" && joined (searchTextWords ("harmonizer")) == "harmoniser",
           "iz reads as is on both sides");

    using Parts = std::array<juce::String, 3>;
    const Parts delay { "Delay echo", "Prime Delay tape tempo-synced Effect", "A stereo delay where each side repeats. Sam" };
    const Parts gate { "Rhythmic", "Chop Shop Gate trance-gate delay reverb tempo-synced Effect", "A trance gate for vocals, followed by a ping-pong delay. Sam" };
    const Parts flanger { "Modulation", "Jet Flange 69 flanger tape Effect", "A 1960s tape flanger: the dry signal is held back by a short delay. Sam" };
    const Parts monoSynth { "Mono synth monosynth monophonic", "SH Mono 101 bass lead step-sequencer Instrument", "A mono synth voice inspired by the 1982 classic. Sam" };
    const Parts widener { "Stereo width imaging", "Mono Safe Widener widener mid-side Effect", "A stereo widener whose width lives only in the side signal. Sam" };
    const Parts sequencer { "Sequencer", "Arp in key step-sequencer MIDI tool", "Plays a pattern. Sam" };
    const Parts eq { "EQ equaliser equalizer", "Tilt Tone tilt Effect", "A tilt tone control. Sam" };
    const Parts fet { "Dynamics", "FET 76 Compressor compressor fet Effect", "A FET compressor in the manner of the 1176. Sam" };
    const auto rank = [] (const char* typed, const Parts& parts) { return searchRank (searchQueryWords (typed), parts); };
    check (rank ("delay", delay) == 1 && rank ("delay", gate) == 2 && rank ("delay", flanger) == 3 && rank ("delay", widener) == -1
           && rank ("delays", delay) == 1 && rank ("echo", delay) == 1,
           "a category ranks first, then a name or a tag, then a mention; a word that is nowhere is no match");
    check (rank ("mono synth", monoSynth) == 2 && rank ("mono synths", monoSynth) == 2 && rank ("monosynth", monoSynth) == 1
           && rank ("mono synth", widener) == -1 && rank ("mono", widener) == 2 && rank ("tape delay", delay) == 3
           && rank ("sam delay", delay) == 4 && rank ("tempo-synced delay", delay) == 3 && rank ("tempo-synced delay", gate) == 4,
           "every typed word is needed, and the rank adds up where each was found");
    check (rank ("eq", eq) == 1 && rank ("eq", sequencer) == -1 && rank ("comp", fet) == 2 && rank ("pressor", fet) == -1 && rank ("equalizer", eq) == 1,
           "a typed word matches the start of a word, not its middle");
    check (rank ("1176", fet) == 3 && rank ("fet 76", fet) == 4 && rank ("tempo-synced", delay) == 2 && rank ("tempo sync", delay) == 4
           && rank ("midi", sequencer) == 2 && rank ("instrument", monoSynth) == 2,
           "a number, a hyphenated tag and a kind are found");
    check (rank ("", delay) == 0 && rank ("", widener) == 0, "nothing typed matches everything alike");

    // The library tools/test-host.sh wrote, as the desktop app writes it.
    TestPlayHead head;
    auto p = openPlugin (HostKind::effect, head, {});
    const auto& lib = p->getLibrary();
    const auto* drift = lib.find ("test-tape-drift");
    check (drift != nullptr && drift->category == "delay" && drift->categoryName == "Delay" && joined (drift->tags) == "tape, dub"
           && joined (drift->keywords) == "echo" && drift->summary.startsWith ("A tape echo"),
           "library.json says where Tape Drift is filed: " + (drift != nullptr ? drift->categoryName + " (" + joined (drift->tags) + ")" : juce::String ("not read")));
    const auto* old = lib.find ("test-old-abi");
    const auto* runaway = lib.find ("test-runaway");
    check (old != nullptr && old->category.isEmpty() && old->tags.isEmpty() && old->summary.isEmpty()
           && runaway != nullptr && runaway->category.isEmpty() && runaway->tags.isEmpty(),
           "a plugin with none of the five fields (an older app's library.json), and one whose fields are null, read as not filed");
    check (drift != nullptr && listDetail (*drift).startsWith ("Delay, v") && listDetail (*drift).endsWith ("Use it for dub throws.")
           && old != nullptr && listDetail (*old) == "Effect, v1. A chorus from before, with a short delay inside it."
           && runaway != nullptr && listDetail (*runaway).startsWith ("MIDI tool, v1. A tool that never stops"),
           "a row says the category and the summary, or the kind and the description of a plugin that is not filed: \""
           + (drift != nullptr ? listDetail (*drift) : juce::String()) + "\"");

    // The Step Drive Filter and Meter Keys, the meter fixtures of decision 91, are not filed and come last,
    // and after them Kick Duck, the sidechain's fixture (docs/sidechain.md), and Song Gate, the song's
    // (decision 123), not filed either.
    const juce::String everything = "Tape Drift, Old Chorus, Not Mine, Step Drive Filter, Kick Duck, Song Gate, Poly Saw, Dub Keys, Acid Line, Two Hands, MIDI Steps, Runaway, Meter Keys";
    check (namesOf (listedEntries (lib, HostKind::effect, {}, {})) == everything,
           "with nothing typed the list is as it was, what Pangea opens first: " + namesOf (listedEntries (lib, HostKind::effect, {}, {})));
    check (namesOf (listedEntries (lib, HostKind::instrument, {}, {})) == "Poly Saw, Dub Keys, Acid Line, Two Hands, MIDI Steps, Runaway, Meter Keys, Tape Drift, Old Chorus, Not Mine, Step Drive Filter, Kick Duck, Song Gate",
           "and in Pangea Instrument, what that opens first");
    check (namesOf (listedEntries (lib, HostKind::effect, "delays", {})) == "Tape Drift, Not Mine, Old Chorus, Dub Keys",
           "\"delays\" lists the delays, then the effect that mentions one, then what Pangea cannot open: " + namesOf (listedEntries (lib, HostKind::effect, "delays", {})));
    check (namesOf (listedEntries (lib, HostKind::effect, "echo", {})) == "Tape Drift, Not Mine, Dub Keys",
           "\"echo\" finds the delays by the category's other name: " + namesOf (listedEntries (lib, HostKind::effect, "echo", {})));
    check (namesOf (listedEntries (lib, HostKind::instrument, "synth", {})) == "Poly Saw, Acid Line"
           && namesOf (listedEntries (lib, HostKind::instrument, "mono synths", {})) == "Acid Line",
           "\"synth\" finds both synths and \"mono synths\" the one: " + namesOf (listedEntries (lib, HostKind::instrument, "mono synths", {})));
    check (namesOf (listedEntries (lib, HostKind::effect, {}, "delay")) == "Tape Drift, Not Mine"
           && namesOf (listedEntries (lib, HostKind::effect, "ping", "delay")) == "Not Mine"
           && listedEntries (lib, HostKind::effect, "spaceship", {}).empty(),
           "a category narrows the list, with what is typed or without, and a word that is nowhere lists nothing");
    check (countsOf (listedCategories (lib, {}, {})) == "Arpeggiator 1, Delay 2, Keys 1, Mono synth 1, Poly synth 1, Sequencer 1",
           "the categories are counted, by name: " + countsOf (listedCategories (lib, {}, {})));
    check (countsOf (listedCategories (lib, "dub", {})) == "Delay 1, Keys 1" && countsOf (listedCategories (lib, "dub", "sequencer")) == "Delay 1, Keys 1, Sequencer 0",
           "over what the typed words match, and the category chosen stays when it holds nothing: " + countsOf (listedCategories (lib, "dub", "sequencer")));

    // The window: the box and the menu, pressed as a person would.
    std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
    auto* window = dynamic_cast<HostEditor*> (ed.get());
    check (window != nullptr && joined (window->listedNames()) == everything
           && joined (window->categoryMenu()) == "All categories (13), Arpeggiator (1), Delay (2), Keys (1), Mono synth (1), Poly synth (1), Sequencer (1)"
           && window->categoryShown() == "All categories (13)",
           "the window lists every plugin, and its menu every category: " + (window != nullptr ? joined (window->categoryMenu()) : juce::String ("no window")));
    if (window != nullptr)
    {
        shoot (*window, "list");
        window->typeInSearch ("delays");
        check (joined (window->listedNames()) == "Tape Drift, Not Mine, Old Chorus, Dub Keys" && joined (window->categoryMenu()) == "All categories (4), Delay (2), Keys (1)",
               "typing delays narrows the list and the menu: " + joined (window->listedNames()) + "; " + joined (window->categoryMenu()));
        shoot (*window, "list-delays");
        window->chooseCategory ("delay");
        check (joined (window->listedNames()) == "Tape Drift, Not Mine" && window->categoryShown() == "Delay (2)",
               "choosing Delay leaves the two delays: " + joined (window->listedNames()));
        window->typeInSearch ("organ");
        shoot (*window, "list-nothing");
        check (window->listedNames().isEmpty() && joined (window->categoryMenu()) == "All categories (1), Delay (0), Keys (1)" && window->categoryShown() == "Delay (0)",
               "a search the category does not hold lists nothing, and the category stays chosen: " + joined (window->categoryMenu()));
        window->chooseCategory ({});
        check (joined (window->listedNames()) == "Dub Keys" && window->categoryShown() == "All categories (1)", "every category again: " + joined (window->listedNames()));
        window->typeInSearch ({});
        check (joined (window->listedNames()) == everything, "and with the box empty the whole list is back");
    }
}

void vst3Checks (const juce::String& effectPath, const juce::MemoryBlock& state)
{
    std::cout << "The built Pangea.vst3, loaded as a DAW loads it" << std::endl;
    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> found;
    format.findAllTypesForFile (found, effectPath);
    check (! found.isEmpty(), "found in " + effectPath);
    if (found.isEmpty())
        return;
    juce::String error;
    auto plugin = format.createInstanceFromDescription (*found[0], kRate, kBlock, error);
    check (plugin != nullptr, "it loads" + (error.isNotEmpty() ? ": " + error : juce::String()));
    if (plugin == nullptr)
        return;
    plugin->enableAllBuses();
    plugin->prepareToPlay (kRate, kBlock);
    // The host sees the 128 slots, in their order, and then the plugin's own bypass, which it is
    // told is the bypass. (Before decision 75 the last was a Bypass that JUCE's wrapper added.)
    {
        const auto& all = plugin->getParameters();
        bool slotsInOrder = all.size() >= kSlots;
        for (int i = 0; slotsInOrder && i < kSlots; ++i)
            slotsInOrder = all[i]->getName (100) == SlotParameter::unusedName (i);
        check (slotsInOrder, "the host sees the 128 slots first, in their order, the first named \""
               + (all.isEmpty() ? juce::String() : all[0]->getName (100)) + "\"");
        auto* bypass = plugin->getBypassParameter();
        auto* hosted = dynamic_cast<juce::HostedAudioProcessorParameter*> (bypass);
        // A VST3 parameter's id is a hash of the plugin's own id for it; JUCE's own bypass has 'byps'.
        const auto own = juce::String (juce::VST3ClientExtensions::convertJuceParameterId (pangea::bypass::parameterId));
        check (all.size() == kSlots + 1 && bypass != nullptr && all.getLast() == bypass && bypass->getName (100) == pangea::bypass::parameterName
               && hosted != nullptr && hosted->getParameterID() == own && own != juce::String ((juce::uint32) 0x62797073),
               "and then one more, which it is told is the bypass: the plugin's own (id "
               + (hosted != nullptr ? hosted->getParameterID() : juce::String ("none")) + ", the hash of pangea_bypass), "
               + juce::String (all.size()) + " parameters in all");
    }
    TestPlayHead head;
    plugin->setPlayHead (&head);
    Run dry;
    for (int b = 0; b < 40; ++b)
        block (*plugin, dry, head);

    // The session, as a VST3 host keeps it: the plugin's state inside JUCE's VST3 container.
    juce::XmlElement vst3State ("VST3PluginState");
    vst3State.createNewChildElement ("IComponent")->addTextElement (state.toBase64Encoding());
    juce::MemoryBlock wrapped;
    juce::AudioProcessor::copyXmlToBinary (vst3State, wrapped);
    plugin->setStateInformation (wrapped.getData(), (int) wrapped.getSize());
    for (int i = 0; i < 20; ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
    juce::StringArray names;
    for (auto* param : plugin->getParameters())
        if (! param->getName (100).startsWith ("Slot "))
            names.add (param->getName (100));
    check (names.contains ("Time") && names.contains ("Drive"), "after the session loads, the host sees the controls' names: " + names.joinIntoString (", "));
    Run run;
    for (int b = 0; b < 40; ++b)
        block (*plugin, run, head);
    const double passing = dry.rms (0, dry.left.size()), playing = run.rms (0, run.left.size());
    check (run.finite() && playing > 0.02 && std::abs (playing - passing) > 0.02, "it plays Tape Drift v2: level "
           + juce::String (playing, 4) + ", where the sound passing through was " + juce::String (passing, 4));

    // The host's bypass is the plugin's own parameter: switched by the host, the input passes to
    // the bit once the fade is over, and the plugin comes back when it is switched off.
    if (auto* bypass = plugin->getBypassParameter())
    {
        const auto fade = (size_t) pangea::bypasscheck::fadeSamples (kRate);
        Take take;
        for (int b = 0; b < 20; ++b)
            takeBlock (*plugin, take, head);
        const size_t onAt = take.length();
        bypass->setValueNotifyingHost (1.0f);   // a VST3 takes a parameter with the next block
        for (int b = 0; b < 40; ++b)
            takeBlock (*plugin, take, head);
        const size_t offAt = take.length();
        bypass->setValueNotifyingHost (0.0f);
        for (int b = 0; b < 40; ++b)
            takeBlock (*plugin, take, head);
        check (take.finite && take.fromInput (0, onAt) > 0.02f && take.fromInput (onAt + fade, offAt) <= 0.0f
               && take.fromInput (offAt + fade, take.length()) > 0.02f,
               "switched by the host, its bypass passes the input to the bit once the 30 ms fade is over ("
               + juce::String ((int) (offAt - onAt - fade)) + " samples), and Tape Drift is heard again after it");

        // Saved bypassed in the host's session, it opens bypassed in a second plugin, and comes back from there.
        bypass->setValueNotifyingHost (1.0f);
        Take settle;
        for (int b = 0; b < 4; ++b)
            takeBlock (*plugin, settle, head);
        juce::MemoryBlock session;
        plugin->getStateInformation (session);
        auto second = format.createInstanceFromDescription (*found[0], kRate, kBlock, error);
        check (second != nullptr, "a second plugin loads for the session");
        if (second != nullptr)
        {
            second->enableAllBuses();
            second->prepareToPlay (kRate, kBlock);
            second->setStateInformation (session.getData(), (int) session.getSize());
            for (int i = 0; i < 20; ++i)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
            juce::StringArray secondNames;
            for (auto* param : second->getParameters())
                secondNames.add (param->getName (100));
            TestPlayHead secondHead;
            second->setPlayHead (&secondHead);
            Take opened;
            for (int b = 0; b < 20; ++b)
                takeBlock (*second, opened, secondHead);
            const size_t backAt = opened.length();
            if (auto* secondBypass = second->getBypassParameter())
                secondBypass->setValueNotifyingHost (0.0f);
            for (int b = 0; b < 40; ++b)
                takeBlock (*second, opened, secondHead);
            check (secondNames.contains ("Time") && opened.fromInput (0, backAt) <= 0.0f && opened.fromInput (backAt + fade, opened.length()) > 0.02f,
                   "the host's session keeps the bypass: a second plugin opens Tape Drift bypassed, the input passing from the first sample, and plays it once the bypass is off");
            second->releaseResources();
        }
        bypass->setValueNotifyingHost (0.0f);
    }
    else
        check (false, "the host is told of no bypass parameter");
    plugin->releaseResources();
}


/** A session as a VST3 host keeps it: this plugin's state inside JUCE's VST3 container. */
juce::MemoryBlock vst3Session (const juce::String& projectId, int version)
{
    juce::XmlElement state ("PANGEA_HOST");
    state.setAttribute ("format", 1);
    state.setAttribute ("projectId", projectId);
    state.setAttribute ("version", version);
    juce::MemoryBlock raw;
    juce::AudioProcessor::copyXmlToBinary (state, raw);
    juce::XmlElement vst3State ("VST3PluginState");
    vst3State.createNewChildElement ("IComponent")->addTextElement (raw.toBase64Encoding());
    juce::MemoryBlock wrapped;
    juce::AudioProcessor::copyXmlToBinary (vst3State, wrapped);
    return wrapped;
}

void vst3MidiChecks (const juce::String& instrumentPath)
{
    std::cout << "The built Pangea Instrument.vst3 with a MIDI tool, loaded as a DAW loads it" << std::endl;
    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> found;
    format.findAllTypesForFile (found, instrumentPath);
    check (! found.isEmpty() && found[0]->isInstrument, "found as an instrument in " + instrumentPath);
    if (found.isEmpty())
        return;
    juce::String error;
    auto plugin = format.createInstanceFromDescription (*found[0], kRate, kBlock, error);
    check (plugin != nullptr && plugin->producesMidi(), "it loads and says it sends MIDI" + (error.isNotEmpty() ? ": " + error : juce::String()));
    if (plugin == nullptr)
        return;
    plugin->enableAllBuses();
    plugin->prepareToPlay (kRate, kBlock);
    const auto session = vst3Session ("test-two-hands", 1);
    plugin->setStateInformation (session.getData(), (int) session.getSize());
    for (int i = 0; i < 20; ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
    TestPlayHead head;
    plugin->setPlayHead (&head);
    Run run;
    for (int b = 0; b < (int) (4 * 2.0 * kRate / kBlock); ++b)   // 4 bars at 120 BPM
        block (*plugin, run, head, {}, false);
    check (run.ons > 4, "Two Hands' notes come out of the VST3 as MIDI: " + juce::String (run.ons) + " in 4 bars");
    head.playing = false;
    block (*plugin, run, head, {}, false);
    block (*plugin, run, head, {}, false);
    check (run.sounding.empty() && run.ons == run.offs, "and all end when the transport stops (" + juce::String (run.ons) + " on, "
           + juce::String (run.offs) + " off)");
    plugin->releaseResources();
}

// ---- The sidechain input (docs/sidechain.md) ------------------------------------------------

/** The pad a sidechain compressor ducks: two sines at -12 dBFS together. */
float padAt (long n)
{
    const double t = (double) n / kRate;
    return (float) (0.125 * (std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * t)
                             + std::sin (2.0 * juce::MathConstants<double>::pi * 330.5 * t)));
}

/** A kick every half second from 0.25 s, as tools/render/SidechainChecks.cpp and the site make it. */
float kickAt (long n)
{
    const double since = std::fmod ((double) n / kRate - 0.25, 0.5);
    if ((double) n / kRate < 0.25 || since > 0.42)
        return 0.0f;
    // The sweep's phase, summed in closed form: 150 Hz falling to 45 Hz over 0.12 s, then held.
    const double k = std::log (45.0 / 150.0) / 0.12;
    const double phase = since < 0.12 ? 2.0 * juce::MathConstants<double>::pi * 150.0 * (std::exp (k * since) - 1.0) / k
                                      : 2.0 * juce::MathConstants<double>::pi * (150.0 * (std::exp (k * 0.12) - 1.0) / k + 45.0 * (since - 0.12));
    return (float) (std::sin (phase) * 0.7 * std::pow (0.001 / 0.9, juce::jmin (1.0, since / 0.4)));
}

/** Gives a plugin a stereo track with its sidechain off, or on in stereo, as a host does: stopped first. */
bool sidechainOn (juce::AudioProcessor& p, bool on)
{
    auto layout = p.getBusesLayout();
    if (layout.inputBuses.size() < 2)
        return false;
    layout.inputBuses.getReference (1) = on ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled();
    p.releaseResources();
    const bool ok = p.setBusesLayout (layout);
    p.prepareToPlay (kRate, kBlock);
    return ok && p.getTotalNumInputChannels() == (on ? 4 : 2);
}

/** `seconds` of the pad, with the kick in the sidechain when it is on and `kick` says so; the left
    side out. `bufferChannels`, when given, plays a host that passes fewer channels than the layout has. */
std::vector<float> sidechainTake (juce::AudioProcessor& p, TestPlayHead& head, double seconds, bool kick, int bufferChannels = 0)
{
    std::vector<float> left;
    const int channels = bufferChannels > 0 ? bufferChannels
                                            : juce::jmax (2, juce::jmax (p.getTotalNumInputChannels(), p.getTotalNumOutputChannels()));
    const int ins = juce::jmin (channels, p.getTotalNumInputChannels());
    const int mainIns = p.getMainBusNumInputChannels();
    juce::AudioBuffer<float> buffer (channels, kBlock);
    juce::MidiBuffer midi;
    for (long start = 0; start < (long) (seconds * kRate); start += kBlock)
    {
        buffer.clear();
        for (int i = 0; i < kBlock; ++i)
        {
            for (int ch = 0; ch < mainIns; ++ch)
                buffer.setSample (ch, i, padAt (start + i));
            for (int ch = mainIns; ch < ins && kick; ++ch)
                buffer.setSample (ch, i, kickAt (start + i));
        }
        midi.clear();
        p.processBlock (buffer, midi);
        left.insert (left.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + kBlock);
        head.advance (kBlock);
    }
    return left;
}

double rmsDbOf (const std::vector<float>& v, double from, double to)
{
    double sum = 0.0;
    const auto a = (size_t) (from * kRate), b = juce::jmin (v.size(), (size_t) (to * kRate));
    for (size_t i = a; i < b; ++i)
        sum += (double) v[i] * v[i];
    return 10.0 * std::log10 (sum / (double) juce::jmax ((size_t) 1, b - a) + 1.0e-20);
}

/** How far the pad dips in the 50 ms after each kick, at the least, against a take without one. */
double smallestDip (const std::vector<float>& keyed, const std::vector<float>& quiet, double seconds)
{
    double smallest = 1.0e9;
    for (double kick = 0.25; kick + 0.07 < seconds; kick += 0.5)
        smallest = juce::jmin (smallest, rmsDbOf (quiet, kick + 0.02, kick + 0.07) - rmsDbOf (keyed, kick + 0.02, kick + 0.07));
    return smallest;
}

float furthestApart (const std::vector<float>& a, const std::vector<float>& b)
{
    float worst = a.size() == b.size() ? 0.0f : 1.0f;
    for (size_t i = 0; i < juce::jmin (a.size(), b.size()); ++i)
        worst = juce::jmax (worst, std::abs (a[i] - b[i]));
    return worst;
}

/**
 * Pangea has a second input bus, Sidechain, stereo and off by default; the instrument and the MIDI
 * effect have the buses they had. A version whose code declares a sidechain (Kick Duck) is fed
 * from it, and ducks on a kick there, in the processor and in the built VST3; one that declares
 * none (Tape Drift) never reads it; and a swap between the two while the kick plays is all numbers.
 */
void sidechainChecks (const juce::String& effectPath)
{
    std::cout << "The sidechain input: Pangea's buses, Kick Duck keyed by a kick, Tape Drift deaf to it" << std::endl;
    const auto stereo = juce::AudioChannelSet::stereo(), mono = juce::AudioChannelSet::mono();
    {
        Processor fx (HostKind::effect), instrument (HostKind::instrument), midiFx (HostKind::midiFx);
        auto* side = fx.getBus (true, 1);
        check (fx.getBusCount (true) == 2 && fx.getBus (true, 0)->getName() == "Input" && fx.getBus (true, 0)->isEnabledByDefault()
                   && side != nullptr && side->getName() == "Sidechain" && side->getDefaultLayout() == stereo && ! side->isEnabledByDefault()
                   && fx.getBusCount (false) == 1 && fx.getTotalNumInputChannels() == 2 && fx.getTotalNumOutputChannels() == 2,
               "Pangea: Input, then Sidechain (stereo, off by default), and one output: two channels in and two out until a host routes a sidechain");
        check (instrument.getBusCount (true) == 0 && instrument.getBusCount (false) == 1 && midiFx.getBusCount (true) == 0 && midiFx.getBusCount (false) == 0,
               "Pangea Instrument and Pangea MIDI FX have the buses they had");
        auto layoutWith = [&fx] (juce::AudioChannelSet main, juce::AudioChannelSet sidechain)
        {
            auto layout = fx.getBusesLayout();
            layout.inputBuses.getReference (0) = main;
            layout.outputBuses.getReference (0) = main;
            layout.inputBuses.getReference (1) = sidechain;
            return fx.checkBusesLayoutSupported (layout);
        };
        check (layoutWith (stereo, juce::AudioChannelSet::disabled()) && layoutWith (stereo, mono) && layoutWith (stereo, stereo)
                   && layoutWith (mono, stereo) && ! layoutWith (stereo, juce::AudioChannelSet::discreteChannels (3)),
               "its sidechain takes off, mono and stereo on a stereo or a mono track, and refuses three channels");
    }

    constexpr double seconds = 2.5;
    TestPlayHead head;
    auto duck = openPlugin (HostKind::effect, head, "test-kick-duck");
    auto inst = duck->loaded();
    check (inst != nullptr && inst->name == "Kick Duck" && inst->sidechainInputs == 2 && inst->numInputs == 4,
           "Kick Duck opens, with the sound's two inputs and a stereo sidechain after them");
    const auto off = sidechainTake (*duck, head, seconds, true);
    check (sidechainOn (*duck, true), "a host switching the sidechain on gives it four input channels");
    const auto silent = sidechainTake (*duck, head, seconds, false);
    const auto keyed = sidechainTake (*duck, head, seconds, true);
    check (furthestApart (off, silent) < 1.0e-6f && rmsDbOf (off, 0.0, seconds) > -20.0,
           "with the sidechain off, Kick Duck gives what it gives with it on and silent: the pad, undimmed (level "
               + juce::String (rmsDbOf (off, 0.0, seconds), 1) + " dB)");
    const double dip = smallestDip (keyed, silent, seconds);
    check (dip > 10.0, "with a kick in the sidechain the pad dips by " + juce::String (dip, 1) + " dB at least after every kick");
    // A host that passes only the two main channels with the sidechain on: read as off, not past the buffer.
    check (sidechainOn (*duck, true), "the sidechain on again, from the start");
    const auto shortBuffer = sidechainTake (*duck, head, seconds, true, 2);
    check (furthestApart (shortBuffer, off) < 1.0e-6f,
           "a host that passes only the two main channels with the sidechain on: the sidechain is read as off, and Kick Duck gives what it gives with it off");

    auto tape = openPlugin (HostKind::effect, head, "test-tape-drift");
    const auto tapeOff = sidechainTake (*tape, head, seconds, true);
    auto tapeKeyed = openPlugin (HostKind::effect, head, "test-tape-drift");
    check (sidechainOn (*tapeKeyed, true), "Tape Drift in a Pangea whose sidechain is on");
    const auto tapeWithKick = sidechainTake (*tapeKeyed, head, seconds, true);
    check (furthestApart (tapeOff, tapeWithKick) == 0.0f,
           "Tape Drift, which declares no sidechain, gives the same samples with a kick on the sidechain as with it off");

    // A swap while the kick plays: Tape Drift fades out deaf to it while Kick Duck fades in hearing it.
    tapeKeyed->choose ("test-kick-duck");
    check (tapeKeyed->loaded() != nullptr && tapeKeyed->loaded()->name == "Kick Duck", "choosing Kick Duck in its place");
    const auto swapped = sidechainTake (*tapeKeyed, head, 1.0, true);
    bool finite = true;
    for (float x : swapped)
        finite = finite && std::isfinite (x);
    check (finite && rmsDbOf (swapped, 0.5, 1.0) > -40.0, "every sample through the swap is a number, and Kick Duck plays after it");

    // The built Pangea.vst3, as a DAW loads it.
    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> found;
    format.findAllTypesForFile (found, effectPath);
    juce::String error;
    std::unique_ptr<juce::AudioPluginInstance> plugin;
    if (! found.isEmpty())
        plugin = format.createInstanceFromDescription (*found[0], kRate, kBlock, error);
    check (plugin != nullptr, "Pangea.vst3 loads" + (error.isNotEmpty() ? ": " + error : juce::String()));
    if (plugin == nullptr)
        return;
    auto* hostedSide = plugin->getBus (true, 1);
    check (plugin->getBusCount (true) == 2 && hostedSide != nullptr && hostedSide->getName() == "Sidechain" && ! hostedSide->isEnabledByDefault()
               && plugin->getTotalNumInputChannels() == 2,
           "the VST3 shows the host its Sidechain bus, off by default, and two input channels");
    plugin->prepareToPlay (kRate, kBlock);
    // A session saved with Kick Duck open at its defaults, as a VST3 host keeps it.
    juce::MemoryBlock saved;
    duck->getStateInformation (saved);
    juce::XmlElement vst3State ("VST3PluginState");
    vst3State.createNewChildElement ("IComponent")->addTextElement (saved.toBase64Encoding());
    juce::MemoryBlock session;
    juce::AudioProcessor::copyXmlToBinary (vst3State, session);
    plugin->setStateInformation (session.getData(), (int) session.getSize());
    auto shown = [&plugin]
    {
        for (auto* param : plugin->getParameters())
            if (param->getName (100) == "Threshold dB")
                return true;
        return false;
    };
    for (int i = 0; i < 250 && ! shown(); ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    const bool opened = shown();
    check (opened, "a session with Kick Duck opens it: the host sees its Threshold dB");
    TestPlayHead hostedHead;
    plugin->setPlayHead (&hostedHead);
    const auto hostedOff = sidechainTake (*plugin, hostedHead, seconds, true);
    check (sidechainOn (*plugin, true), "a host routing a track to it switches the sidechain on");
    const auto hostedKeyed = sidechainTake (*plugin, hostedHead, seconds, true);
    const double hostedDip = smallestDip (hostedKeyed, hostedOff, seconds);
    check (rmsDbOf (hostedOff, 0.0, seconds) > -20.0 && hostedDip > 10.0,
           "the pad passes with nothing routed (level " + juce::String (rmsDbOf (hostedOff, 0.0, seconds), 1) + " dB), and dips by "
               + juce::String (hostedDip, 1) + " dB at least after every kick routed to it");
    plugin->releaseResources();
}

/** Runs the message loop until `done` or `seconds` pass. */
template <typename F>
bool pumpUntil (F done, double seconds)
{
    const double until = juce::Time::getMillisecondCounterHiRes() + seconds * 1000.0;
    while (! done() && juce::Time::getMillisecondCounterHiRes() < until)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    return done();
}

/** The web panel needs a window on screen (WebKit draws nothing in one it cannot see), so this runs
    in the GUI session: tools/test-host.sh starts it through tools/run-in-gui-session.sh. */
void webChecks()
{
    std::cout << "The web panel in Pangea (effect): Tape Drift v2 drawn by the component library" << std::endl;
    Processor p (HostKind::effect);
    p.prepareToPlay (kRate, kBlock);
    p.poll();
    p.choose ("test-tape-drift");
    check (p.loaded() != nullptr && p.loaded()->version == 2, "Tape Drift v2 opens (the checks before moved the library on to v2)");
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    auto* host = dynamic_cast<HostEditor*> (ed.get());
    check (host != nullptr && host->webPanel() != nullptr, "the window shows the web panel, unasked: nothing switched it on (decision 77)");
    if (host == nullptr || host->webPanel() == nullptr)
        return;
    ed->setTopLeftPosition (80, 80);
    ed->addToDesktop (juce::ComponentPeer::windowHasTitleBar);
    ed->setVisible (true);
    ed->toFront (false);

    const double t0 = juce::Time::getMillisecondCounterHiRes();
    const bool drawn = pumpUntil ([host] { return host->webPanel()->hasDrawn(); }, 20.0);
    const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
    auto* web = host->webPanel();
    check (drawn, "the page draws the plugin and says how tall it is: " + juce::String (ms, 0) + " ms, panel "
           + juce::String (web->getWidth()) + " x " + juce::String (web->getHeight()));
    if (! drawn)
        return;
    pumpUntil ([] { return false; }, 0.3);
    check (ed->getHeight() == web->getHeight() + HostEditor::barHeight + HostEditor::footHeight,
           "the window takes the panel's height: window " + juce::String (ed->getWidth()) + " x " + juce::String (ed->getHeight())
           + ", panel " + juce::String (web->getWidth()) + " x " + juce::String (web->getHeight()) + " at y " + juce::String (web->getY()));

    auto& browser = web->getBrowser();
    // What the page drew: the library's label under each control.
    juce::String labels;
    {
        bool answered = false;
        browser.evaluateJavascript ("JSON.stringify([...document.querySelectorAll('#plugin .pg-lbl')].map((e) => e.textContent.trim()))",
                                    [&] (juce::WebBrowserComponent::EvaluationResult r)
        {
            if (auto* v = r.getResult())
                labels = v->toString();
            answered = true;
        });
        pumpUntil ([&answered] { return answered; }, 3.0);
    }
    int found = 0;
    for (auto name : { "Time", "Feedback", "Tone", "Wow", "Flutter", "Mix", "Drive" })
        if (labels.contains (name))
            ++found;
    check (found == 7, "the library drew all seven of its controls: " + labels);

    // The page to the plugin: a control change as the library announces it.
    browser.evaluateJavascript ("document.getElementById('plugin').dispatchEvent(new CustomEvent('pangea:change',"
                                "{ detail: { id: 'time', value: 250 } })); 1");
    pumpUntil ([&p] { return std::abs (realValue (slotNamed (p, "Time")) - 250.0f) < 0.6f; }, 3.0);
    check (std::abs (realValue (slotNamed (p, "Time")) - 250.0f) < 0.6f, "a change on the page reaches the plugin: Time is "
           + juce::String (realValue (slotNamed (p, "Time")), 1) + " ms");

    // The plugin to the page: automation moves Feedback; the page hears it.
    browser.evaluateJavascript ("window.__JUCE__.backend.addEventListener('params', (v) => {"
                                " window.__seen = Object.assign(window.__seen || {}, v); }); 1");
    pumpUntil ([] { return false; }, 0.2);
    setReal (slotNamed (p, "Feedback"), 0.8f);
    juce::String seen;
    pumpUntil ([&]
    {
        bool answered = false;
        browser.evaluateJavascript ("JSON.stringify(window.__seen || {})", [&] (juce::WebBrowserComponent::EvaluationResult r)
        {
            if (auto* v = r.getResult())
                seen = v->toString();
            answered = true;
        });
        pumpUntil ([&answered] { return answered; }, 1.0);
        return seen.contains ("feedback");
    }, 3.0);
    const auto value = juce::JSON::parse (seen).getProperty ("feedback", {});
    check (! value.isVoid() && std::abs ((float) value - 0.8f) < 0.01f, "a change from the host reaches the page: " + seen);

    // The footer: who built the plugin, as a link to Pangea. Tape Drift v2's plugin.json names its
    // owner and the site (tools/test-host.sh). A press opens Pangea in the system's browser, which
    // onOpenSite stands in for here, and the web view stays on the panel.
    auto askPanel = [&browser] (const juce::String& script)
    {
        juce::String answer;
        bool answered = false;
        browser.evaluateJavascript (script, [&] (juce::WebBrowserComponent::EvaluationResult r)
        {
            if (auto* v = r.getResult())
                answer = v->toString();
            answered = true;
        });
        pumpUntil ([&answered] { return answered; }, 3.0);
        return answer;
    };
    const auto footer = askPanel ("document.querySelector('#plugin .pg-foot').textContent");
    check (footer == "Built by Test Member with Pangea", "the footer names the member who built it: " + footer);
    const auto link = askPanel ("(() => { const a = document.querySelector('#plugin .pg-foot a');"
                                " return a ? a.href + ' ' + a.textContent : 'no link'; })()");
    check (link == "https://pangea.example/ Built by Test Member with Pangea", "and all of it is a link to Pangea: " + link);
    juce::StringArray openedSites;
    web->onOpenSite = [&openedSites] (const juce::String& address) { openedSites.add (address); };
    askPanel ("document.querySelector('#plugin .pg-foot a').click(); 1");
    pumpUntil ([&openedSites] { return openedSites.size() == 1; }, 3.0);
    check (openedSites.size() == 1 && openedSites[0] == "https://pangea.example",
           "a press on it opens Pangea in the system's browser: " + openedSites.joinIntoString (", "));
    // Followed any other way (Open Link in the web view's own menu), the web view does not load
    // the site: the editor opens it in the system's browser. Any other address opens nothing.
    askPanel ("window.location.href = 'https://pangea.example/'; 1");
    pumpUntil ([&openedSites] { return openedSites.size() == 2; }, 3.0);
    askPanel ("window.location.href = 'https://elsewhere.example/'; 1");
    pumpUntil ([] { return false; }, 0.5);
    const auto still = askPanel ("window.location.href + ' ' + document.querySelectorAll('#plugin .pg-lbl').length");
    check (openedSites.size() == 2 && still == juce::WebBrowserComponent::getResourceProviderRoot() + " 7",
           "the web view never leaves the panel, and only Pangea's address opens anything: "
           + juce::String (openedSites.size()) + " opened, the page is " + still);
    web->onOpenSite = nullptr;

    // Pangea's bypass on the page (decision 75): the header bar, with the plugin's name on the left
    // and the bypass button at the far right, and nothing else of the library's bar. What the page
    // shows of it, as one line.
    const juce::String shownScript =
        "(() => { const root = document.getElementById('plugin'); const bar = root.querySelector('.pg-hdr');"
        " if (!bar) return 'no header bar';"
        " const name = bar.querySelector('.pg-hdr-name'), b = bar.querySelector('.pg-byp'), w = bar.querySelector('.pg-byp-word');"
        " if (!name || !b) return 'no name or no button';"
        " const parts = [...bar.children].map((e) => e.className.split(' ')[0]).join(' ');"
        " const nr = name.getBoundingClientRect(), br = b.getBoundingClientRect(), barr = bar.getBoundingClientRect();"
        " return name.textContent.trim() + ' | ' + parts + ' | ' + b.getAttribute('aria-label') + ' pressed ' + b.getAttribute('aria-pressed')"
        " + (b.classList.contains('is-on') ? ' lit' : ' dark') + (w && !w.hidden ? ' word ' + w.textContent : ' no word')"
        " + (root.classList.contains('is-bypassed') ? ' faded' : ' clear')"
        " + ' | name ' + (nr.left - barr.left < barr.width / 4 ? 'left' : 'elsewhere')"
        " + ', button ' + (barr.right - br.right < barr.width / 8 && br.left > nr.right ? 'far right' : 'elsewhere'); })()";
    const juce::String shownIn = "Tape Drift | pg-hdr-name pg-hdr-sp pg-byp-word pg-byp | Bypass pressed false lit no word clear | name left, button far right";
    const juce::String shownBypassed = "Tape Drift | pg-hdr-name pg-hdr-sp pg-byp-word pg-byp | Bypass pressed true dark word Bypassed faded | name left, button far right";
    auto pageShows = [&] (const juce::String& wanted)
    {
        juce::String shown;
        pumpUntil ([&] { shown = askPanel (shownScript); return shown == wanted; }, 3.0);
        return shown;
    };
    auto shown = pageShows (shownIn);
    check (shown == shownIn && ! p.bypassParameter()->get(), "the header bar shows the plugin's name on the left and the bypass button at the far right, lit: " + shown);

    // A press on the button moves the parameter, and the button changes when the plugin says so.
    askPanel ("document.querySelector('#plugin .pg-byp').click(); 1");
    pumpUntil ([&p] { return p.bypassParameter()->get(); }, 3.0);
    check (p.bypassParameter()->get(), "a press on the button moves the plugin's bypass parameter: bypassed");
    shown = pageShows (shownBypassed);
    check (shown == shownBypassed, "and the page shows it once the plugin says so: " + shown);
    askPanel ("document.querySelector('#plugin .pg-byp').click(); 1");
    pumpUntil ([&p] { return ! p.bypassParameter()->get(); }, 3.0);
    shown = pageShows (shownIn);
    check (! p.bypassParameter()->get() && shown == shownIn, "a second press brings the plugin back, in the parameter and on the page");

    // A change of the parameter from outside (automation, the host's own bypass button) moves the button.
    p.bypassParameter()->setValueNotifyingHost (1.0f);
    shown = pageShows (shownBypassed);
    check (shown == shownBypassed, "a change of the parameter from outside moves the button: " + shown);
    // The controls stay usable while bypassed: a change on the page still reaches the plugin.
    browser.evaluateJavascript ("document.getElementById('plugin').dispatchEvent(new CustomEvent('pangea:change',"
                                "{ detail: { id: 'time', value: 300 } })); 1");
    pumpUntil ([&p] { return std::abs (realValue (slotNamed (p, "Time")) - 300.0f) < 0.6f; }, 3.0);
    check (std::abs (realValue (slotNamed (p, "Time")) - 300.0f) < 0.6f, "while bypassed the controls still work: Time is "
           + juce::String (realValue (slotNamed (p, "Time")), 1) + " ms");

    // The window hidden and shown again, with the parameter moved meanwhile. The web view goes to
    // a blank page while hidden. The page that comes back, as it was left or drawn afresh from
    // what it was first given (the plugin in, both ways), is told where the bypass is now.
    p.bypassParameter()->setValueNotifyingHost (0.0f);
    shown = pageShows (shownIn);
    ed->setVisible (false);
    pumpUntil ([] { return false; }, 0.5);
    p.bypassParameter()->setValueNotifyingHost (1.0f);
    pumpUntil ([] { return false; }, 0.3);
    ed->setVisible (true);
    ed->toFront (false);
    const auto afterHiding = pageShows (shownBypassed);
    check (shown == shownIn && afterHiding == shownBypassed,
           "hidden and shown again with the parameter moved meanwhile, the page shows where it is now: " + afterHiding);
    p.bypassParameter()->setValueNotifyingHost (0.0f);
    shown = pageShows (shownIn);
    check (shown == shownIn, "and it still follows the parameter after that");

    ed->removeFromDesktop();
    ed.reset();

    // A MIDI tool in the web panel: MIDI Steps has a menu, accent bars with a playhead, a knob and a variation.
    std::cout << "The web panel in Pangea Instrument: MIDI Steps, a logic module's controls" << std::endl;
    Processor tool (HostKind::instrument);
    TestPlayHead head;
    tool.setPlayHead (&head);
    tool.prepareToPlay (kRate, kBlock);
    tool.poll();
    tool.choose ("test-midi-steps");
    tool.setWebPanel (true);
    std::unique_ptr<juce::AudioProcessorEditor> toolEd (tool.createEditor());
    auto* toolHost = dynamic_cast<HostEditor*> (toolEd.get());
    check (toolHost != nullptr && toolHost->webPanel() != nullptr, "a MIDI tool shows the web panel too");
    if (toolHost == nullptr || toolHost->webPanel() == nullptr)
        return;
    toolEd->setTopLeftPosition (80, 80);
    toolEd->addToDesktop (juce::ComponentPeer::windowHasTitleBar);
    toolEd->setVisible (true);
    toolEd->toFront (false);
    const bool toolDrawn = pumpUntil ([toolHost] { return toolHost->webPanel()->hasDrawn(); }, 20.0);
    check (toolDrawn, "the page draws MIDI Steps");
    if (! toolDrawn)
        return;
    auto& page = toolHost->webPanel()->getBrowser();
    auto ask = [&page] (const juce::String& script)
    {
        juce::String answer;
        bool answered = false;
        page.evaluateJavascript (script, [&] (juce::WebBrowserComponent::EvaluationResult r)
        {
            if (auto* v = r.getResult())
                answer = v->toString();
            answered = true;
        });
        pumpUntil ([&answered] { return answered; }, 3.0);
        return answer;
    };
    const auto toolLabels = ask ("JSON.stringify([...document.querySelectorAll('#plugin .pg-lbl')].map((e) => e.textContent.trim()))");
    check (toolLabels.contains ("Rate") && toolLabels.contains ("Length") && toolLabels.contains ("Variation"),
           "the library drew its controls: " + toolLabels);
    const auto barsFound = ask ("JSON.stringify(window.__pangeaHeads.count())");
    check (barsFound == "{\"bars\":4,\"found\":4}", "the page found the four accent bars that light: " + barsFound);
    // MIDI Steps' plugin.json names nobody and no site: the library's own words, and Pangea's own name.
    const auto toolFooter = ask ("(() => { const a = document.querySelector('#plugin .pg-foot a');"
                                 " return a ? a.href + ' ' + a.textContent : 'no link'; })()");
    check (toolFooter == "https://pangea.audio/ Built by Pangea artist with Pangea",
           "a plugin.json that names nobody still gives a footer that links to Pangea: " + toolFooter);
    // A MIDI tool's page has the header bar too, and its button moves the same parameter.
    const auto toolBar = ask ("(() => { const bar = document.querySelector('#plugin .pg-hdr'); if (!bar) return 'no header bar';"
                              " const b = bar.querySelector('.pg-byp'), name = bar.querySelector('.pg-hdr-name');"
                              " return (name ? name.textContent.trim() : 'no name') + ' | ' + [...bar.children].map((e) => e.className.split(' ')[0]).join(' ')"
                              " + ' | ' + (b ? b.getAttribute('aria-pressed') : 'no button'); })()");
    check (toolBar == "MIDI Steps | pg-hdr-name pg-hdr-sp pg-byp-word pg-byp | false", "a MIDI tool's page has the header bar too: " + toolBar);
    ask ("document.querySelector('#plugin .pg-byp').click(); 1");
    pumpUntil ([&tool] { return tool.bypassParameter()->get(); }, 3.0);
    juce::String toolPressed;
    pumpUntil ([&] { toolPressed = ask ("document.querySelector('#plugin .pg-byp').getAttribute('aria-pressed')"); return toolPressed == "true"; }, 3.0);
    check (tool.bypassParameter()->get() && toolPressed == "true", "a press on it bypasses the tool, and the button says so");
    tool.bypassParameter()->setValueNotifyingHost (0.0f);
    pumpUntil ([&] { toolPressed = ask ("document.querySelector('#plugin .pg-byp').getAttribute('aria-pressed')"); return toolPressed == "false"; }, 3.0);
    check (toolPressed == "false", "and with the parameter back the button is back");

    // A menu from the page: Rate to its second item (1/8), as the library reports a choice.
    ask ("document.getElementById('plugin').dispatchEvent(new CustomEvent('pangea:change',"
         "{ detail: { id: 'logic0', value: 1 } })); 1");
    pumpUntil ([&tool] { return std::abs (realValue (slotNamed (tool, "Rate")) - 1.0f) < 0.01f; }, 3.0);
    check (std::abs (realValue (slotNamed (tool, "Rate")) - 1.0f) < 0.01f, "a menu choice on the page reaches the module: Rate is "
           + juce::String (realValue (slotNamed (tool, "Rate"))));
    ask ("document.getElementById('plugin').dispatchEvent(new CustomEvent('pangea:change',"
         "{ detail: { id: 'logic0', value: 0 } })); 1");
    pumpUntil ([] { return false; }, 0.2);

    // A MIDI tool's notes reach its page (docs/felt-look.md): the page is told they will come,
    // the editor sends them as they sound, and says "quiet" when the tool stops playing.
    check (ask ("String(window.__JUCE__.initialisationData.pangea[0].notes === true)") == "true",
           "a MIDI tool's page is told its notes will come, for the Felt look's chord map and notes");
    ask ("window.__notesSeen = 0; window.__quietSeen = 0;"
         " window.__JUCE__.backend.addEventListener('notes', (n) => { window.__notesSeen += n.events.length / 4; });"
         " window.__JUCE__.backend.addEventListener('quiet', () => { window.__quietSeen += 1; }); 1");

    // The transport plays: the module shows its step in slot 0, and one accent bar lights at a time.
    head.ppq = 0.0;
    head.playing = true;
    Run run;
    juce::StringArray litSeen;
    for (int i = 0; i < 60; ++i)
    {
        block (tool, run, head, {}, false);
        pumpUntil ([] { return false; }, 0.03);
        const auto lit = ask ("String(document.querySelectorAll('#plugin .pg-playhead').length) + ':' + "
                              "[...document.querySelectorAll('#plugin .pg-playhead .pg-lbl')].map((e) => e.textContent.trim()).join()");
        litSeen.addIfNotAlreadyThere (lit);
    }
    bool oneAtATime = ! litSeen.isEmpty();
    int distinct = 0;
    for (const auto& l : litSeen)
    {
        if (l.startsWith ("1:"))
            ++distinct;
        else if (! l.startsWith ("0:"))
            oneAtATime = false;
    }
    check (oneAtATime && distinct >= 2, "while it plays one accent bar is lit at a time, and the light moves: " + litSeen.joinIntoString (" | "));
    const auto notesSeen = ask ("String(window.__notesSeen)").getIntValue();
    check (notesSeen > 0 && notesSeen <= run.ons + run.offs,
           "the notes it plays reach its page as it plays them: " + juce::String (notesSeen) + " of "
               + juce::String (run.ons + run.offs) + " ons and offs");
    head.playing = false;
    block (tool, run, head, {}, false);
    pumpUntil ([] { return false; }, 0.3);
    check (ask ("String(document.querySelectorAll('#plugin .pg-playhead').length)") == "0", "when it stops no bar is lit");
    check (ask ("String(window.__quietSeen)").getIntValue() >= 1, "and its page is told the notes have stopped");
    toolEd->removeFromDesktop();
    toolEd.reset();

    // Two Hands in the Felt look, its XY pad marked [xy:x] and [xy:y] on Balance and Amount
    // (tools/fixtures/felt-xy): the editor sends the marks, and the page draws the pad on them, as the
    // site draws it for the same marks (src/lib/ui/felt-xy.test.ts).
    std::cout << "The web panel in Pangea Instrument: Two Hands in the Felt look, its XY pad marked" << std::endl;
    tool.choose ("test-two-hands");
    std::unique_ptr<juce::AudioProcessorEditor> handsEd (tool.createEditor());
    auto* handsHost = dynamic_cast<HostEditor*> (handsEd.get());
    check (handsHost != nullptr && handsHost->webPanel() != nullptr, "Two Hands shows the web panel");
    if (handsHost == nullptr || handsHost->webPanel() == nullptr)
        return;
    handsEd->setTopLeftPosition (80, 80);
    handsEd->addToDesktop (juce::ComponentPeer::windowHasTitleBar);
    handsEd->setVisible (true);
    const bool handsDrawn = pumpUntil ([handsHost] { return handsHost->webPanel()->hasDrawn(); }, 20.0);
    check (handsDrawn, "the page draws Two Hands");
    if (handsDrawn)
    {
        auto& handsPage = handsHost->webPanel()->getBrowser();
        juce::String answer;
        bool answered = false;
        handsPage.evaluateJavascript (
            "(() => { const sent = window.__JUCE__.initialisationData.pangea[0].controls.filter((c) => c.xy)"
            ".map((c) => c.label + ':' + c.xy).join(); const P = window.__pangeaPanel, xy = P.spec.xy_pad;"
            " return P.spec.layout + ' | ' + sent + ' | ' + (xy ? P.byId[xy.x].label + ' across, ' + P.byId[xy.y].label + ' up' : 'no pad'); })()",
            [&] (juce::WebBrowserComponent::EvaluationResult r)
            {
                if (auto* v = r.getResult())
                    answer = v->toString();
                answered = true;
            });
        pumpUntil ([&answered] { return answered; }, 3.0);
        check (answer == "side_columns | Balance:x,Amount:y | Balance across, Amount up",
               "the editor sends the XY marks and the page draws the pad on them: " + answer);
    }
    handsEd->removeFromDesktop();
}

// ---- Meters and step lights (decision 91) ------------------------------------------------------------

/** The bar graphs a version taps, as the audio thread reads them: no window needed. */
void meterTapChecks()
{
    std::cout << "Meters: the bar graphs each version taps (decision 91)" << std::endl;
    Processor p (HostKind::effect);
    TestPlayHead head;
    p.setPlayHead (&head);
    p.prepareToPlay (kRate, kBlock);
    p.poll();
    p.choose ("test-step-drive");
    auto inst = p.loaded();
    check (inst != nullptr && inst->meters.kind() == "effect", "the Step Drive Filter opens as an effect");
    if (inst == nullptr)
        return;
    auto& taps = inst->meters;
    juce::StringArray addresses;
    for (size_t i = 0; i < taps.size(); ++i)
        addresses.add (taps.address (i));
    check (taps.size() == 6 && taps.soundTree().isArray() && taps.soundTree().size() == 1,
           "it taps its five meters and its step display, in a tree of one stage: " + addresses.joinIntoString (", "));
    const auto before = taps.blockCount();
    Run run;
    for (int b = 0; b < 40; ++b)
        block (p, run, head);
    check (taps.blockCount() - before == 40, "each block the sound code runs is counted once: "
           + juce::String ((int) (taps.blockCount() - before)) + " for 40");
    bool finite = true;
    float inLeft = -1000.0f, stepPlaying = -1000.0f;
    for (size_t i = 0; i < taps.size(); ++i)
    {
        finite = finite && std::isfinite (taps.value (i));
        if (taps.address (i).endsWith ("/In_L"))
            inLeft = taps.value (i);
        if (taps.address (i).endsWith ("/Step_Playing"))
            stepPlaying = taps.value (i);
    }
    check (finite && inLeft > -40.0f && inLeft <= 6.0f, "after the sound every value is a number, and In L reads the input: "
           + juce::String (inLeft, 1) + " dB");
    check (stepPlaying >= 0.0f && stepPlaying <= 7.0f && std::abs (stepPlaying - std::round (stepPlaying)) < 1.0e-6f,
           "the step display holds the step playing: " + juce::String (stepPlaying));

    // What the audio thread does for the meters each block, timed: a store for each value and a count.
    {
        const int calls = 200000;
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < calls; ++i)
            taps.capture();
        const double ns = (juce::Time::getMillisecondCounterHiRes() - t0) * 1.0e6 / calls;
        check (ns < 1000.0, "capturing the six values costs " + juce::String (ns, 1) + " ns a block (a block of 512 is 10.7 ms)");
    }

    // An instrument: the bar graph in its voice is in the tree and never tapped; the one in its effect section is.
    Processor keys (HostKind::instrument);
    keys.setPlayHead (&head);
    keys.prepareToPlay (kRate, kBlock);
    keys.poll();
    keys.choose ("test-meter-keys");
    auto keysInst = keys.loaded();
    check (keysInst != nullptr && keysInst->meters.kind() == "instrument", "Meter Keys opens as an instrument");
    if (keysInst != nullptr)
    {
        juce::StringArray keyTaps;
        for (size_t i = 0; i < keysInst->meters.size(); ++i)
            keyTaps.add (keysInst->meters.address (i));
        check (keysInst->meters.soundTree().size() == 2 && keyTaps.size() == 1 && keyTaps[0].endsWith ("/Level"),
               "its tree has the voice and the effect section, and only the effect section's Level is tapped: " + keyTaps.joinIntoString (", "));
    }

    // A version with no bar graph taps nothing, and a MIDI tool has no meters at all.
    Processor tape (HostKind::effect);
    tape.prepareToPlay (kRate, kBlock);
    tape.poll();
    tape.choose ("test-tape-drift");
    check (tape.loaded() != nullptr && tape.loaded()->meters.size() == 0, "Tape Drift, with no bar graph, taps nothing");
    Processor steps (HostKind::instrument);
    steps.prepareToPlay (kRate, kBlock);
    steps.poll();
    steps.choose ("test-midi-steps");
    check (steps.loaded() != nullptr && steps.loaded()->meters.size() == 0 && steps.loaded()->meters.kind() == "midi",
           "a MIDI tool taps nothing");
}

/** Runs blocks through `p` for about `seconds`, pumping the message loop for about one block's time
    after each, as a host's audio thread and the window's timer run side by side. */
void playFor (juce::AudioProcessor& p, Run& run, TestPlayHead& head, double seconds, juce::MidiBuffer midi = {})
{
    const double until = juce::Time::getMillisecondCounterHiRes() + seconds * 1000.0;
    bool first = true;
    while (juce::Time::getMillisecondCounterHiRes() < until)
    {
        block (p, run, head, first ? midi : juce::MidiBuffer{});
        first = false;
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
    }
}

/** The meters and the step lights on the web panel, in a window on screen (the GUI session). What the
    page finds is held to what the site finds in the same code: meters-expected.json, which
    tools/test-host.sh copies in and src/lib/faust/plugin-meters.test.ts holds to the site's rules. */
void meterChecks()
{
    const auto expected = juce::JSON::parse (hostDir().getChildFile ("meters-expected.json"));
    check (expected.isObject(), "the site's answer for the meter fixtures is there");

    std::cout << "Meters and step lights in the web panel: Step Drive Filter in Pangea (decision 91)" << std::endl;
    Processor p (HostKind::effect);
    TestPlayHead head;
    p.setPlayHead (&head);
    p.prepareToPlay (kRate, kBlock);
    p.poll();
    p.choose ("test-step-drive");
    check (p.loaded() != nullptr, "the Step Drive Filter opens");
    if (p.loaded() == nullptr)
        return;
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    auto* host = dynamic_cast<HostEditor*> (ed.get());
    check (host != nullptr && host->webPanel() != nullptr, "it shows the web panel");
    if (host == nullptr || host->webPanel() == nullptr)
        return;
    ed->setTopLeftPosition (80, 80);
    ed->addToDesktop (juce::ComponentPeer::windowHasTitleBar);
    ed->setVisible (true);
    ed->toFront (false);
    const bool drawn = pumpUntil ([host] { return host->webPanel()->hasDrawn(); }, 20.0);
    check (drawn, "the page draws it");
    if (! drawn)
        return;
    auto& page = host->webPanel()->getBrowser();
    auto ask = [&page] (const juce::String& script)
    {
        juce::String answer;
        bool answered = false;
        page.evaluateJavascript (script, [&] (juce::WebBrowserComponent::EvaluationResult r)
        {
            if (auto* v = r.getResult())
                answer = v->toString();
            answered = true;
        });
        pumpUntil ([&answered] { return answered; }, 3.0);
        return answer;
    };
    // One JSON form for both sides, so that two answers that mean the same read the same.
    auto same = [] (const juce::var& v) { return juce::JSON::toString (v, true); };
    const juce::String foundScript =
        "JSON.stringify({ meters: window.__pangeaSound.meters.map((m) => ({ key: m.key, label: m.label,"
        " groups: m.groups.slice(1), min: m.min, max: m.max, unit: m.unit, kind: m.kind })),"
        " playheads: window.__pangeaSound.playheads.map((p) => p.slot) })";

    // What the page found in the plugin's tree, against what the site finds in the same code.
    const auto found = juce::JSON::parse (ask (foundScript));
    const auto want = expected.getProperty ("step-drive", {});
    check (same (found.getProperty ("meters", {})) == same (want.getProperty ("meters", {})),
           "the page finds the meters the site finds in the same code: " + same (found.getProperty ("meters", {})));
    check (same (found.getProperty ("playheads", {})) == same (want.getProperty ("playheads", {})),
           "and the same step display: slot " + same (found.getProperty ("playheads", {})));

    // Drawn: In and Out as stereo pairs, and Gain Reduction, each in the section the mapping chose.
    const auto placed = ask (
        "(() => { const P = window.__pangeaPanel, els = window.__pangeaMeter.elements;"
        " if (els.length !== P.meters.length) return 'drew ' + els.length + ' of ' + P.meters.length;"
        " return P.meters.map((m, i) => { const s = P.metered.find((x) => x.controls.includes(m.id));"
        " const g = els[i].closest('.pg-group'); const t = g && g.querySelector('.pg-g-t') ? g.querySelector('.pg-g-t').textContent.trim() : 'no section';"
        " return m.label + (m.stereo ? ' (stereo)' : '') + ' in ' + t + (s && s.group === t ? '' : ' (wanted ' + (s ? s.group : 'none') + ')'); }).join('; '); })()");
    check (placed.contains ("In (stereo) in") && placed.contains ("Gain Reduction in") && placed.contains ("Out (stereo) in")
               && ! placed.contains ("wanted") && ! placed.startsWith ("drew"),
           "three meters are drawn, each in its own section: " + placed);

    const juce::String levelsScript = "JSON.stringify(window.__pangeaMeter.levels())";
    auto allAtRest = [] (const juce::String& levels)
    {
        const auto v = juce::JSON::parse (levels);
        auto* o = v.getDynamicObject();
        if (o == nullptr || o->getProperties().isEmpty())
            return false;
        for (const auto& kv : o->getProperties())
            if (auto* columns = kv.value.getArray())
                for (const auto& column : *columns)
                    if ((double) column > 0.001)
                        return false;
        return true;
    };
    auto numbersIn = [] (const juce::var& values)
    {
        int n = 0;
        if (auto* o = values.getDynamicObject())
            for (const auto& kv : o->getProperties())
                if (kv.value.isDouble() || kv.value.isInt() || kv.value.isInt64())
                    ++n;
        return n;
    };
    const auto restLevels = ask (levelsScript);
    check (allAtRest (restLevels), "before any sound every meter is at rest: " + restLevels);

    // The sound plays: the values come at the window's rate and move the meters.
    Run run;
    playFor (p, run, head, 0.5);
    const int events0 = ask ("String(window.__pangeaMeterEvents())").getIntValue();
    const double t0 = juce::Time::getMillisecondCounterHiRes();
    playFor (p, run, head, 1.0);
    const double seconds = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
    const int events1 = ask ("String(window.__pangeaMeterEvents())").getIntValue();
    const double rate = (events1 - events0) / seconds;
    check (rate >= 20.0 && rate <= 36.0, "while it plays the page is sent the values " + juce::String (rate, 1)
           + " times a second (the window's timer runs at 30)");
    const auto values = juce::JSON::parse (ask ("JSON.stringify(window.__pangeaMeterValues || {})"));
    check (numbersIn (values) == 6, "every tapped bar graph has a value: " + same (values));
    const auto playingLevels = juce::JSON::parse (ask (levelsScript));
    juce::StringArray moved;
    if (auto* o = playingLevels.getDynamicObject())
        for (const auto& kv : o->getProperties())
            if (auto* columns = kv.value.getArray())
                for (const auto& column : *columns)
                    if ((double) column > 0.05)
                    {
                        moved.addIfNotAlreadyThere (kv.name.toString());
                        break;
                    }
    check (moved.size() >= 2, "the input and output meters move with the sound: " + same (playingLevels));

    // The step lights: one step lit at a time, the one the plugin reports, and the light moves.
    juce::StringArray litSeen;
    bool matches = true;
    for (int i = 0; i < 40; ++i)
    {
        playFor (p, run, head, 0.05);
        const auto lit = ask ("(() => { const v = window.__pangeaMeterValues || {};"
                              " const id = window.__pangeaSound.playheads[0].id;"
                              " const on = [...document.querySelectorAll('#plugin .pg-playhead .pg-lbl')].map((e) => e.textContent.trim());"
                              " return on.length + '|' + on.join() + '|' + (typeof v[id] === 'number' ? Math.round(v[id]) + 1 : 'none'); })()");
        const auto parts = juce::StringArray::fromTokens (lit, "|", "");
        if (parts.size() != 3 || (parts[0] != "1" && parts[0] != "0"))
            matches = false;
        else if (parts[0] == "1" && ! parts[1].endsWith (" " + parts[2]))
            matches = false;
        if (parts.size() == 3)
            litSeen.addIfNotAlreadyThere (parts[0] + ":" + parts[1]);
    }
    int distinct = 0;
    for (const auto& l : litSeen)
        if (l.startsWith ("1:"))
            ++distinct;
    check (matches && distinct >= 2, "while it plays one step is lit, the one the plugin reports, and the light moves: "
           + litSeen.joinIntoString (" | "));

    // Pangea's bypass: nothing of the plugin is heard, so the meters rest and the steps go dark.
    const juce::String nothingSent = "[null,null,null,null,null,null]";
    const juce::String valuesScript = "JSON.stringify(Object.values(window.__pangeaMeterValues || {}))";
    const juce::String litScript = "String(document.querySelectorAll('#plugin .pg-playhead').length)";
    p.bypassParameter()->setValueNotifyingHost (1.0f);
    playFor (p, run, head, 0.6);
    const auto bypassedValues = ask (valuesScript);
    const auto bypassedLit = ask (litScript);
    playFor (p, run, head, 1.5);
    const auto bypassedLevels = ask (levelsScript);
    check (bypassedValues == nothingSent && bypassedLit == "0" && allAtRest (bypassedLevels),
           "bypassed, the page is sent no value, no step is lit and every meter falls to rest: " + bypassedValues + " " + bypassedLevels);
    p.bypassParameter()->setValueNotifyingHost (0.0f);
    playFor (p, run, head, 0.6);
    check (numbersIn (juce::JSON::parse (ask ("JSON.stringify(window.__pangeaMeterValues || {})"))) == 6,
           "back in, the values come again");

    // The host stops calling the plugin (its own bypass, a track it no longer runs): after a quarter
    // of a second with no block, the meters rest. The last value would otherwise stand, frozen.
    pumpUntil ([] { return false; }, 0.6);
    const auto stoppedValues = ask (valuesScript);
    pumpUntil ([] { return false; }, 1.5);
    const auto stoppedLevels = ask (levelsScript);
    check (stoppedValues == nothingSent && allAtRest (stoppedLevels) && ask (litScript) == "0",
           "when no block comes, the page is told nothing is heard and every meter falls to rest: " + stoppedValues);

    // The window open costs the audio thread nothing more: the same blocks, timed with the window
    // open (its timer and the page running between them, as on a host's other threads) and closed.
    auto medianBlockUs = [&] (bool pump)
    {
        std::vector<double> us;
        for (int b = 0; b < 1500; ++b)
        {
            const double t = juce::Time::getMillisecondCounterHiRes();
            block (p, run, head);
            us.push_back ((juce::Time::getMillisecondCounterHiRes() - t) * 1000.0);
            if (pump && b % 8 == 0)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (2);
        }
        std::sort (us.begin(), us.end());
        return us[us.size() / 2];
    };
    const double open = medianBlockUs (true);
    ed->setVisible (false);
    pumpUntil ([] { return false; }, 0.3);
    const double closed = medianBlockUs (false);
    check (open <= closed * 1.5 + 5.0, "a block takes " + juce::String (open, 1) + " us with the window open and "
           + juce::String (closed, 1) + " us with it closed (median of 1500)");
    ed->removeFromDesktop();
    ed.reset();

    // An instrument: the meter in its effect section moves with a note; the one in its voice is not drawn.
    std::cout << "Meters in the web panel: Meter Keys in Pangea Instrument" << std::endl;
    Processor keys (HostKind::instrument);
    TestPlayHead keysHead;
    keys.setPlayHead (&keysHead);
    keys.prepareToPlay (kRate, kBlock);
    keys.poll();
    keys.choose ("test-meter-keys");
    std::unique_ptr<juce::AudioProcessorEditor> keysEd (keys.createEditor());
    auto* keysHost = dynamic_cast<HostEditor*> (keysEd.get());
    check (keysHost != nullptr && keysHost->webPanel() != nullptr, "Meter Keys shows the web panel");
    if (keysHost == nullptr || keysHost->webPanel() == nullptr)
        return;
    keysEd->setTopLeftPosition (80, 80);
    keysEd->addToDesktop (juce::ComponentPeer::windowHasTitleBar);
    keysEd->setVisible (true);
    keysEd->toFront (false);
    check (pumpUntil ([keysHost] { return keysHost->webPanel()->hasDrawn(); }, 20.0), "the page draws it");
    auto& keysPage = keysHost->webPanel()->getBrowser();
    auto askKeys = [&keysPage] (const juce::String& script)
    {
        juce::String answer;
        bool answered = false;
        keysPage.evaluateJavascript (script, [&] (juce::WebBrowserComponent::EvaluationResult r)
        {
            if (auto* v = r.getResult())
                answer = v->toString();
            answered = true;
        });
        pumpUntil ([&answered] { return answered; }, 3.0);
        return answer;
    };
    const auto keysFound = juce::JSON::parse (askKeys (foundScript));
    const auto keysWant = expected.getProperty ("meter-keys", {});
    check (same (keysFound.getProperty ("meters", {})) == same (keysWant.getProperty ("meters", {}))
               && askKeys ("String(window.__pangeaMeter.elements.length)") == "1",
           "the page draws the effect section's meter only, as the site does: " + same (keysFound.getProperty ("meters", {})));
    juce::MidiBuffer noteOn;
    noteOn.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 110), 0);
    Run keysRun;
    playFor (keys, keysRun, keysHead, 0.8, noteOn);
    const auto keysLevels = juce::JSON::parse (askKeys (levelsScript));
    double level = 0.0;
    if (auto* o = keysLevels.getDynamicObject())
        for (const auto& kv : o->getProperties())
            if (auto* columns = kv.value.getArray(); columns != nullptr && ! columns->isEmpty())
                level = juce::jmax (level, (double) columns->getReference (0));
    check (level > 0.05, "a note moves it: " + same (keysLevels));
    keysEd->removeFromDesktop();
}
} // namespace

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    if (argc < 2)
    {
        std::cerr << "usage: pangea-host-check <Pangea.vst3> [<Pangea Instrument.vst3>] | --web" << std::endl;
        return 2;
    }
    std::cout << "host folder: " << hostDir().getFullPathName() << std::endl;
    if (juce::String (argv[1]) == "--web")
    {
        webChecks();
        meterChecks();
        std::cout << (failures == 0 ? "all web panel checks passed" : juce::String (failures) + " web panel checks failed") << std::endl;
        return failures == 0 ? 0 : 1;
    }
    const auto state = effectChecks();
    instrumentChecks();
    effectSectionChecks();
    transportChecks();
    meterTapChecks();
    midiFxChecks();
    rebuildChecks();
    bypassChecks();
    listChecks();
    vst3Checks (juce::String (argv[1]), state);
    sidechainChecks (juce::String (argv[1]));
    if (argc > 2)
        vst3MidiChecks (juce::String (argv[2]));
    std::cout << (failures == 0 ? "all checks passed" : juce::String (failures) + " checks failed") << std::endl;
    return failures == 0 ? 0 : 1;
}
