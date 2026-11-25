// pangea-render's checks for the notes of a logic module and a MIDI tool (docs/midi-tools.md).
//
//   pangea-render notes [bars] [bpm]
//       Runs this build's processor offline with a transport: plays from bar one, then stops.
//       Exits 1 unless notes come out, all in range, every one ends and none is left on after
//       stop; the same at block sizes of 64, 128 and 480; and at every control's minimum and
//       maximum. Also says what the display slots showed, and warns when the module on its own
//       leaves a note on (then only the platform's safety net ends it).
//
//   pangea-render host-check <plugin.vst3>
//       Loads the built plugin as a DAW would (like Pattern Player's host_check), drives it with a
//       transport and compares its MIDI out with the module's own notes, the module run alone
//       from the source compiled into this tool. Then the safety net: a loop back in the middle
//       of a note, a stop, a release and a bypass must leave nothing on. Then a state round trip.
//
//   pangea-render stop-check [<plugin.vst3>]
//       For tools/fixtures/runaway only (docs/midi-tools.md, "The step budget"): with each fault,
//       the processor is stopped in the block the fault starts, for the right reason, with every
//       note ended and nothing after; the editor says so; heavy honest work is never stopped;
//       and a fresh start plays again. With a VST3, the same faults through the plugin a DAW
//       loads. Every run has a watchdog, so a budget that fails is a failure, not a hang.
#include "RenderHooks.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <map>
#include <thread>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();
juce::StringArray pangeaDescribeEditor (juce::AudioProcessorEditor&);

namespace pangea::render
{

namespace
{

struct Heard
{
    juce::int64 sample = 0;
    bool on = false;
    int channel = 1, note = 0, velocity = 0;

    bool operator== (const Heard& o) const
    {
        return sample == o.sample && on == o.on && channel == o.channel && note == o.note && (! on || velocity == o.velocity);
    }
};

/** Notes in one order, whoever made them: by sample, offs first, then channel and note. */
void normalise (std::vector<Heard>& notes)
{
    std::stable_sort (notes.begin(), notes.end(), [] (const Heard& a, const Heard& b)
    {
        if (a.sample != b.sample) return a.sample < b.sample;
        if (a.on != b.on) return ! a.on;
        if (a.channel != b.channel) return a.channel < b.channel;
        return a.note < b.note;
    });
}

juce::String noteName (int note) { return juce::MidiMessage::getMidiNoteName (note, true, true, 4) + " (" + juce::String (note) + ")"; }

juce::String describe (const Heard& h, double samplesPerBeat, double beatsPerBar)
{
    const double beats = (double) h.sample / samplesPerBeat;
    const int bar = (int) std::floor (beats / beatsPerBar);
    const double beat = beats - bar * beatsPerBar;
    return "bar " + juce::String (bar + 1) + " beat " + juce::String (beat + 1.0, 2) + ": "
         + (h.on ? "on  " : "off ") + noteName (h.note) + " ch " + juce::String (h.channel)
         + (h.on ? " vel " + juce::String (h.velocity) : juce::String());
}

/** What is wrong with a stream of notes: out of range, overlapping, or left on at the end. Empty when nothing is. */
juce::StringArray problemsWith (const std::vector<Heard>& notes)
{
    juce::StringArray problems;
    std::map<int, int> sounding;   // channel * 128 + note -> ons not yet ended
    for (const auto& h : notes)
    {
        if (h.channel < 1 || h.channel > 16 || h.note < 0 || h.note > 127 || (h.on && (h.velocity < 1 || h.velocity > 127)))
            problems.addIfNotAlreadyThere ("a note out of range: " + noteName (h.note) + " ch " + juce::String (h.channel));
        auto& count = sounding[h.channel * 128 + h.note];
        if (h.on)
        {
            if (count > 0)
                problems.addIfNotAlreadyThere ("a note on for " + noteName (h.note) + " while it was already on");
            ++count;
        }
        else if (count > 0)
            --count;
        else
            problems.addIfNotAlreadyThere ("a note off for " + noteName (h.note) + " that was not on");
    }
    int stuck = 0;
    for (const auto& [key, count] : sounding)
        stuck += count;
    if (stuck > 0)
        problems.add (juce::String (stuck) + " notes left on at the end");
    return problems;
}

int countOns (const std::vector<Heard>& notes)
{
    int n = 0;
    for (const auto& h : notes)
        n += h.on ? 1 : 0;
    return n;
}

/** A host's transport, playing one block of a plan at a time. */
struct PlanPlayHead : public juce::AudioPlayHead
{
    Block block;
    double sampleRate = 48000.0;

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm (block.bpm);
        info.setPpqPosition (block.ppq);
        info.setTimeInSamples ((juce::int64) std::llround (block.ppq * sampleRate * 60.0 / block.bpm));
        info.setTimeInSeconds (block.ppq * 60.0 / block.bpm);
        info.setIsPlaying (block.playing);
        info.setTimeSignature (TimeSignature { juce::jmax (1, (int) std::lround (block.beatsPerBar)), 4 });
        return info;
    }
};

/** Plays from bar one for `bars` bars, then stops for a few blocks. */
std::vector<Block> straightPlan (double sampleRate, double bpm, int blockSize, double bars, double beatsPerBar = 4.0)
{
    std::vector<Block> plan;
    const double samplesPerBeat = sampleRate * 60.0 / bpm;
    const juce::int64 total = (juce::int64) std::llround (bars * beatsPerBar * samplesPerBeat);
    juce::int64 pos = 0;
    for (; pos < total; pos += blockSize)
        plan.push_back ({ (double) pos / samplesPerBeat, bpm, beatsPerBar, (int) juce::jmin ((juce::int64) blockSize, total - pos), true });
    for (int i = 0; i < 3; ++i)
        plan.push_back ({ (double) total / samplesPerBeat, bpm, beatsPerBar, blockSize, false });
    return plan;
}

/** Plays `beatsBefore` beats, then jumps to `jumpTo` and plays `beatsAfter`, then stops. */
std::vector<Block> loopPlan (double sampleRate, double bpm, int blockSize, double beatsBefore, double jumpTo, double beatsAfter)
{
    std::vector<Block> plan;
    const double samplesPerBeat = sampleRate * 60.0 / bpm;
    auto play = [&] (double from, double beats)
    {
        const juce::int64 start = (juce::int64) std::llround (from * samplesPerBeat);
        const juce::int64 end = start + (juce::int64) std::llround (beats * samplesPerBeat);
        for (juce::int64 pos = start; pos < end; pos += blockSize)
            plan.push_back ({ (double) pos / samplesPerBeat, bpm, 4.0, (int) juce::jmin ((juce::int64) blockSize, end - pos), true });
        return (double) end / samplesPerBeat;
    };
    play (0.0, beatsBefore);
    const double end = play (jumpTo, beatsAfter);
    for (int i = 0; i < 3; ++i)
        plan.push_back ({ end, bpm, 4.0, blockSize, false });
    return plan;
}

/** Where each block of a plan starts, counting every sample the host played. */
std::vector<juce::int64> blockStarts (const std::vector<Block>& plan)
{
    std::vector<juce::int64> starts;
    juce::int64 at = 0;
    for (const auto& b : plan)
    {
        starts.push_back (at);
        at += b.numSamples;
    }
    return starts;
}

/** The module alone through the plan, as notes at the host's sample positions. */
std::vector<Heard> moduleNotes (double sampleRate, const std::vector<float>& params, const std::vector<Block>& plan,
                                int* stoppedAt = nullptr)
{
    std::vector<ModuleEvent> events;
    pangeaRunModule (sampleRate, params, plan, events, stoppedAt);
    const auto starts = blockStarts (plan);
    std::vector<Heard> notes;
    for (const auto& e : events)
        if (e.control == LOGIC_NOTE_ON || e.control == LOGIC_NOTE_OFF)
            notes.push_back ({ starts[(size_t) e.block] + e.offset, e.control == LOGIC_NOTE_ON, e.channel, e.note,
                               (int) std::lround (e.value) });
    normalise (notes);
    return notes;
}

/** A host's run of the plan: what the plugin's MIDI out said. `midiIn` is fed to the block it falls in. */
std::vector<Heard> runPlugin (juce::AudioProcessor& plugin, double sampleRate, const std::vector<Block>& plan,
                              const std::function<void (int block)>& beforeBlock = {}, const juce::MidiMessageSequence* midiIn = nullptr)
{
    PlanPlayHead head;
    head.sampleRate = sampleRate;
    int maxBlock = 16;
    for (const auto& b : plan)
        maxBlock = juce::jmax (maxBlock, b.numSamples);
    plugin.setPlayHead (&head);
    plugin.setRateAndBufferSizeDetails (sampleRate, maxBlock);
    plugin.prepareToPlay (sampleRate, maxBlock);
    juce::AudioBuffer<float> audio (juce::jmax (2, juce::jmax (plugin.getTotalNumInputChannels(), plugin.getTotalNumOutputChannels())), maxBlock);
    juce::MidiBuffer midi;
    std::vector<Heard> heard;
    const auto starts = blockStarts (plan);
    for (size_t i = 0; i < plan.size(); ++i)
    {
        if (beforeBlock)
            beforeBlock ((int) i);
        head.block = plan[i];
        const int n = plan[i].numSamples;
        juce::AudioBuffer<float> block (audio.getArrayOfWritePointers(), audio.getNumChannels(), n);
        block.clear();
        midi.clear();
        if (midiIn != nullptr)
            for (int k = 0; k < midiIn->getNumEvents(); ++k)
            {
                const auto& m = midiIn->getEventPointer (k)->message;
                const auto at = (juce::int64) m.getTimeStamp();
                if (at >= starts[i] && at < starts[i] + n)
                    midi.addEvent (m, (int) (at - starts[i]));
            }
        plugin.processBlock (block, midi);
        for (const auto metadata : midi)
        {
            const auto m = metadata.getMessage();
            if (m.isNoteOn())
                heard.push_back ({ starts[i] + metadata.samplePosition, true, m.getChannel(), m.getNoteNumber(), m.getVelocity() });
            else if (m.isNoteOff())
                heard.push_back ({ starts[i] + metadata.samplePosition, false, m.getChannel(), m.getNoteNumber(), 0 });
        }
    }
    plugin.releaseResources();
    plugin.setPlayHead (nullptr);
    normalise (heard);
    return heard;
}

/** Where two note streams first differ, or an empty string when they are the same. */
juce::String firstDifference (const std::vector<Heard>& got, const std::vector<Heard>& want, double samplesPerBeat)
{
    for (size_t i = 0; i < juce::jmin (got.size(), want.size()); ++i)
        if (! (got[i] == want[i]))
            return "note " + juce::String ((int) i + 1) + ": the plugin said " + describe (got[i], samplesPerBeat, 4.0)
                 + ", the module " + describe (want[i], samplesPerBeat, 4.0);
    if (got.size() != want.size())
        return "the plugin sent " + juce::String ((int) got.size()) + " note messages, the module " + juce::String ((int) want.size());
    return {};
}

std::vector<float> defaults (const std::vector<LogicControl>& controls)
{
    std::vector<float> v;
    for (const auto& c : controls)
        v.push_back (c.init);
    return v;
}

juce::String styleName (LogicControl::Style s)
{
    switch (s)
    {
        case LogicControl::Style::menu: return "menu";
        case LogicControl::Style::bars: return "bars";
        case LogicControl::Style::generate: return "generate";
        case LogicControl::Style::knob: break;
    }
    return "knob";
}

void printControls (const std::vector<LogicControl>& controls)
{
    for (const auto& c : controls)
    {
        juce::String line = "control " + juce::String (c.index) + " \"" + c.name + "\": " + styleName (c.style);
        if (c.style == LogicControl::Style::menu)
            line << " " << c.labels.joinIntoString (", ");
        if (c.playhead >= 0)
            line << ", lit by display slot " << c.playhead << " at position " << c.position;
        line << ", " << juce::String (c.min) << " to " << juce::String (c.max) << ", default " << juce::String (c.init)
             << ", key \"" << c.key() << "\", parameter " << c.id << " \"" << c.hostName() << "\"";
        std::cout << line << std::endl;
    }
}

/** Sets an in-process processor's logic controls to `values` (in their own units). */
void setLogicValues (juce::AudioProcessor& processor, const std::vector<LogicControl>& controls, const std::vector<float>& values)
{
    for (auto* p : processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            for (size_t i = 0; i < controls.size(); ++i)
                if (ranged->getParameterID() == controls[i].id)
                    ranged->setValueNotifyingHost (ranged->convertTo0to1 (values[i]));
}

int checkFailed (int& failures, bool ok, const juce::String& what)
{
    std::cout << (ok ? "ok    " : "FAIL  ") << what << std::endl;
    failures += ok ? 0 : 1;
    return failures;
}

} // namespace

int notesCommand (const juce::StringArray& args)
{
    const double bars = args.size() > 2 ? juce::jmax (1.0, args[2].getDoubleValue()) : 8.0;
    const double bpm = args.size() > 3 ? juce::jlimit (20.0, 400.0, args[3].getDoubleValue()) : 120.0;
    const double sampleRate = 48000.0;
    const double samplesPerBeat = sampleRate * 60.0 / bpm;
    const auto controls = pangeaLogicControls();
    if (controls.empty() && ! pangeaIsMidiTool())
        std::cout << "this build has no logic module controls" << std::endl;
    printControls (controls);

    int failures = 0;
    std::unique_ptr<juce::AudioProcessor> processor (createPluginFilter());
    std::cout << "processor: " << processor->getName() << ", midi out " << (processor->producesMidi() ? "yes" : "no")
              << ", " << processor->getParameters().size() << " parameters" << std::endl;
    checkFailed (failures, processor->producesMidi(), "the processor says it produces MIDI");

    // 1. At the defaults: notes, all of them ending; what the display showed.
    const auto plan = straightPlan (sampleRate, bpm, 512, bars);
    std::map<int, juce::SortedSet<int>> shown;
    auto* raw = processor.get();
    const auto heard = runPlugin (*processor, sampleRate, plan, [&] (int)
    {
        for (int slot = 0; slot < 8; ++slot)
            if (const int at = pangeaDisplayPosition (*raw, slot); at >= 0)
                shown[slot].add (at);
    });
    const auto problems = problemsWith (heard);
    std::cout << juce::String (bars, 0) << " bars at " << juce::String (bpm, 0) << " BPM, block 512, then stopped: "
              << countOns (heard) << " notes" << std::endl;
    for (size_t i = 0; i < heard.size() && i < 10; ++i)
        std::cout << "  " << describe (heard[i], samplesPerBeat, 4.0) << std::endl;
    if (heard.size() > 10)
        std::cout << "  ... and " << (heard.size() - 10) << " more note messages, the last " << describe (heard.back(), samplesPerBeat, 4.0) << std::endl;
    for (const auto& [slot, values] : shown)
    {
        juce::StringArray v;
        for (auto x : values)
            v.add (juce::String (x));
        std::cout << "display slot " << slot << " showed " << v.joinIntoString (", ") << std::endl;
    }
    checkFailed (failures, countOns (heard) > 0, "notes come out at the defaults while playing");
    checkFailed (failures, problems.isEmpty(), "every note is in range and ends, none left on after stop"
                                                   + (problems.isEmpty() ? juce::String() : ": " + problems.joinIntoString ("; ")));

    // 2. The module on its own: does it end its own notes, or lean on the safety net?
    const auto alone = moduleNotes (sampleRate, defaults (controls), plan);
    const auto aloneProblems = problemsWith (alone);
    if (aloneProblems.isEmpty())
        std::cout << "ok    the module ends its own notes (" << countOns (alone) << " notes on its own)" << std::endl;
    else
        std::cout << "warn  the module on its own: " << aloneProblems.joinIntoString ("; ")
                  << " (the platform ends them, but the check before saving fails a module that relies on it)" << std::endl;

    // 3. The same notes at any block size.
    for (int blockSize : { 64, 128, 480 })
    {
        const auto other = runPlugin (*processor, sampleRate, straightPlan (sampleRate, bpm, blockSize, bars));
        const auto diff = firstDifference (other, heard, samplesPerBeat);
        checkFailed (failures, diff.isEmpty(), "block " + juce::String (blockSize) + ": the same " + juce::String (countOns (other)) + " notes"
                                                   + (diff.isEmpty() ? juce::String() : ": " + diff));
    }

    // 4. Every control at its minimum, then at its maximum, still runs and ends its notes.
    for (const bool high : { false, true })
    {
        std::vector<float> values;
        for (const auto& c : controls)
            values.push_back (high ? c.max : c.min);
        setLogicValues (*processor, controls, values);
        const auto extreme = runPlugin (*processor, sampleRate, straightPlan (sampleRate, bpm, 512, 2.0));
        const auto p = problemsWith (extreme);
        checkFailed (failures, p.isEmpty(), juce::String ("every control at its ") + (high ? "maximum" : "minimum") + ": "
                                                + juce::String (countOns (extreme)) + " notes, none left on" + (p.isEmpty() ? juce::String() : ": " + p.joinIntoString ("; ")));
    }
    setLogicValues (*processor, controls, defaults (controls));

    std::cout << (failures == 0 ? juce::String ("all note checks passed") : juce::String (failures) + " note checks failed") << std::endl;
    return failures == 0 ? 0 : 1;
}

int hostCheckCommand (const juce::StringArray& args)
{
    if (args.size() < 3) { std::cerr << "usage: pangea-render host-check <plugin.vst3>\n"; return 2; }
    const auto path = juce::File::getCurrentWorkingDirectory().getChildFile (args[2]).getFullPathName();
    const double sampleRate = 48000.0, bpm = 120.0;
    const double samplesPerBeat = sampleRate * 60.0 / bpm;
    const auto controls = pangeaLogicControls();

    juce::AudioPluginFormatManager formats;
    formats.addFormat (new juce::VST3PluginFormat());
    juce::OwnedArray<juce::PluginDescription> found;
    for (auto* f : formats.getFormats())
        if (f->fileMightContainThisPluginType (path))
            f->findAllTypesForFile (found, path);
    if (found.isEmpty()) { std::cout << "FAIL  no plugin found in " << path << std::endl; return 1; }
    const auto desc = *found[0];
    juce::String error;
    auto plugin = formats.createPluginInstance (desc, sampleRate, 512, error);
    if (plugin == nullptr) { std::cout << "FAIL  could not load it: " << error << std::endl; return 1; }
    std::cout << "loaded " << desc.name << " (" << desc.pluginFormatName << ", " << (desc.isInstrument ? "instrument" : "effect")
              << ", version " << desc.version << ") from " << path << std::endl;

    int failures = 0;
    checkFailed (failures, plugin->producesMidi(), "it produces MIDI");
    checkFailed (failures, plugin->acceptsMidi(), "it accepts MIDI (an instrument track can route to it)");

    // The module's controls, found among the host's parameters by the names the plugin gives them.
    std::vector<juce::AudioProcessorParameter*> params;
    for (const auto& c : controls)
    {
        juce::AudioProcessorParameter* match = nullptr;
        for (auto* p : plugin->getParameters())
            if (p->getName (128) == c.hostName())
                match = p;
        params.push_back (match);
    }
    const bool allFound = std::find (params.begin(), params.end(), nullptr) == params.end();
    checkFailed (failures, allFound && ! params.empty(), juce::String ((int) params.size()) + " controls of the module are host parameters");
    if (! allFound)
        return 1;

    auto setAll = [&] (juce::AudioPluginInstance& instance, const std::vector<float>& values)
    {
        for (size_t i = 0; i < controls.size(); ++i)
            for (auto* p : instance.getParameters())
                if (p->getName (128) == controls[i].hostName())
                    p->setValueNotifyingHost ((values[i] - controls[i].min) / (controls[i].max - controls[i].min));
        // A VST3 takes parameter changes with the next block it processes.
        runPlugin (instance, sampleRate, { { 0.0, bpm, 4.0, 64, false } });
    };

    // 1. At the defaults, the plugin plays exactly what the module plays, at any block size.
    for (int blockSize : { 512, 64, 1024, 333 })
    {
        const auto plan = straightPlan (sampleRate, bpm, blockSize, 8.0);
        const auto got = runPlugin (*plugin, sampleRate, plan);
        const auto want = moduleNotes (sampleRate, defaults (controls), plan);
        const auto diff = firstDifference (got, want, samplesPerBeat);
        checkFailed (failures, diff.isEmpty() && countOns (got) > 0,
                     "block " + juce::String (blockSize) + ": " + juce::String (countOns (got)) + " notes over 8 bars, the same as the module's"
                         + (diff.isEmpty() ? juce::String() : ": " + diff));
    }

    // 2. Every control at its minimum, then at its maximum: the same as the module with those values.
    for (const bool high : { false, true })
    {
        std::vector<float> values;
        for (const auto& c : controls)
            values.push_back (high ? c.max : c.min);
        setAll (*plugin, values);
        const auto plan = straightPlan (sampleRate, bpm, 512, 4.0);
        const auto got = runPlugin (*plugin, sampleRate, plan);
        const auto want = moduleNotes (sampleRate, values, plan);
        const auto diff = firstDifference (got, want, samplesPerBeat);
        checkFailed (failures, diff.isEmpty(), juce::String ("every control at its ") + (high ? "maximum" : "minimum") + ": "
                                                   + juce::String (countOns (got)) + " notes, the same as the module's" + (diff.isEmpty() ? juce::String() : ": " + diff));
    }
    setAll (*plugin, defaults (controls));

    // 3. The safety net, in a host. A loop back in the middle of a note:
    {
        const auto plan = loopPlan (sampleRate, bpm, 512, 4.0 + 0.05, 0.05, 4.0);
        const auto got = runPlugin (*plugin, sampleRate, plan);
        const auto p = problemsWith (got);
        checkFailed (failures, p.isEmpty() && countOns (got) > 0, "a loop back in the middle of a note leaves nothing on (" + juce::String (countOns (got)) + " notes)"
                                                                      + (p.isEmpty() ? juce::String() : ": " + p.joinIntoString ("; ")));
    }
    // ... a stop in the middle of a note:
    {
        const auto got = runPlugin (*plugin, sampleRate, straightPlan (sampleRate, bpm, 512, 2.0 + 0.0125));
        const auto p = problemsWith (got);
        checkFailed (failures, p.isEmpty(), "a stop in the middle of a note leaves nothing on" + (p.isEmpty() ? juce::String() : ": " + p.joinIntoString ("; ")));
    }
    // ... a release and a fresh start in the middle of a note (the host's buffer size changes):
    {
        auto plan = straightPlan (sampleRate, bpm, 512, 1.0 + 0.0125);
        plan.resize (plan.size() - 3);   // still playing when it is released
        auto first = runPlugin (*plugin, sampleRate, plan);
        const bool leftSounding = countOns (first) > (int) (first.size() - (size_t) countOns (first));
        auto after = runPlugin (*plugin, sampleRate, { { 0.0, bpm, 4.0, 256, false }, { 0.0, bpm, 4.0, 256, false } });
        for (auto& h : after)
            h.sample += 1000000;   // after everything in the first run
        first.insert (first.end(), after.begin(), after.end());
        const auto p = problemsWith (first);
        checkFailed (failures, p.isEmpty(), juce::String ("a release ") + (leftSounding ? "with a note sounding" : "between notes")
                                                + " is followed by its note off" + (p.isEmpty() ? juce::String() : ": " + p.joinIntoString ("; ")));
    }
    // ... a bypass in the middle of a note, with a note played in while bypassed (it passes through):
    if (auto* bypass = plugin->getBypassParameter())
    {
        const auto plan = straightPlan (sampleRate, bpm, 512, 2.0);
        const int bypassAt = (int) (plan.size() / 2) + 1, resumeAt = (int) (plan.size() * 3 / 4);
        const auto starts = blockStarts (plan);
        juce::MidiMessageSequence in;
        in.addEvent (juce::MidiMessage::noteOn (5, 40, (juce::uint8) 99), (double) starts[(size_t) bypassAt + 2]);
        const auto got = runPlugin (*plugin, sampleRate, plan, [&] (int block)
        {
            if (block == bypassAt) bypass->setValueNotifyingHost (1.0f);
            if (block == resumeAt) bypass->setValueNotifyingHost (0.0f);
        }, &in);
        bypass->setValueNotifyingHost (0.0f);
        bool passed = false;
        for (const auto& h : got)
            passed |= h.on && h.channel == 5 && h.note == 40;
        const auto p = problemsWith (got);
        checkFailed (failures, p.isEmpty() && passed, juce::String ("a bypass ends the tool's notes, lets the notes played in through, and ends those when it comes back")
                                                          + (passed ? juce::String() : " (the note played in did not come through)")
                                                          + (p.isEmpty() ? juce::String() : ": " + p.joinIntoString ("; ")));
    }
    else
        std::cout << "skip  the host gave no bypass parameter" << std::endl;

    // 4. A changed control survives saving and loading into a new instance, which then plays the same.
    {
        std::vector<float> values = defaults (controls);
        values.back() = juce::jmin (controls.back().max, controls.back().min + 7.0f);
        setAll (*plugin, values);
        juce::MemoryBlock state;
        plugin->getStateInformation (state);
        auto second = formats.createPluginInstance (desc, sampleRate, 512, error);
        if (second == nullptr) { std::cout << "FAIL  could not load a second instance: " << error << std::endl; return 1; }
        second->setStateInformation (state.getData(), (int) state.getSize());
        runPlugin (*second, sampleRate, { { 0.0, bpm, 4.0, 64, false } });
        const auto plan = straightPlan (sampleRate, bpm, 512, 4.0);
        const auto a = runPlugin (*plugin, sampleRate, plan), b = runPlugin (*second, sampleRate, plan);
        const auto want = moduleNotes (sampleRate, values, plan);
        const auto diff = firstDifference (b, a, samplesPerBeat);
        checkFailed (failures, diff.isEmpty() && firstDifference (a, want, samplesPerBeat).isEmpty(),
                     "\"" + controls.back().hostName() + "\" at " + juce::String (values.back()) + " survives saving and loading, and plays the same"
                         + (diff.isEmpty() ? juce::String() : ": " + diff));
    }

    std::cout << (failures == 0 ? juce::String ("all host checks passed") : juce::String (failures) + " host checks failed") << std::endl;
    return failures == 0 ? 0 : 1;
}

// ---- stop-check ----------------------------------------------------------------------------

namespace
{

/**
 * Runs `f` here, on the message thread (a hosted VST3 locks it to prepare), while a watchdog on
 * its own thread ends the whole check when `f` is not done within `seconds`.
 */
template <typename F>
void withWatchdog (const juce::String& what, double seconds, F&& f)
{
    std::atomic<bool> done { false };
    std::thread watchdog ([&done, what, seconds]
    {
        const auto until = std::chrono::steady_clock::now() + std::chrono::duration<double> (seconds);
        while (! done && std::chrono::steady_clock::now() < until)
            std::this_thread::sleep_for (std::chrono::milliseconds (5));
        if (! done)
        {
            std::cout << "FAIL  " << what << ": not done after " << seconds << " s, so the budget did not stop it" << std::endl;
            std::_Exit (1);
        }
    });
    f();
    done = true;
    watchdog.join();
}

void setByName (juce::AudioProcessor& processor, const juce::String& name, float value, float min, float max)
{
    for (auto* p : processor.getParameters())
        if (p->getName (128) == name)
            p->setValueNotifyingHost ((value - min) / (max - min));
}

/** A run of `bars` bars at 120 BPM in blocks of 512, timed block by block. */
struct TimedRun
{
    std::vector<Heard> heard;
    double slowestMs = 0.0, slowestAfterStopMs = 0.0;
};

TimedRun timedRun (juce::AudioProcessor& processor, double bars, juce::int64 stopSample)
{
    TimedRun r;
    const auto plan = straightPlan (48000.0, 120.0, 512, bars);
    const auto starts = blockStarts (plan);
    std::chrono::steady_clock::time_point last {};
    int lastBlock = -1;
    auto lap = [&] (int block)
    {
        const auto now = std::chrono::steady_clock::now();
        if (lastBlock >= 0)
        {
            const double ms = std::chrono::duration<double, std::milli> (now - last).count();
            r.slowestMs = juce::jmax (r.slowestMs, ms);
            if (starts[(size_t) lastBlock] > stopSample)
                r.slowestAfterStopMs = juce::jmax (r.slowestAfterStopMs, ms);
        }
        last = now;
        lastBlock = block;
    };
    r.heard = runPlugin (processor, 48000.0, plan, lap);
    return r;
}

} // namespace

int stopCheckCommand (const juce::StringArray& args)
{
    const auto controls = pangeaLogicControls();
    if (controls.size() != 2 || controls[0].name != "Fault")
    {
        std::cout << "FAIL  stop-check is for tools/fixtures/runaway, whose controls are Fault and At bar" << std::endl;
        return 2;
    }
    const double sampleRate = 48000.0, samplesPerBeat = sampleRate * 60.0 / 120.0;
    const auto faultSample = (juce::int64) std::llround ((4.0 + 0.25) * samplesPerBeat);   // bar 2, a quarter beat in
    const int faultBlock = (int) (faultSample / 512);
    int failures = 0;

    auto notesAfter = [] (const std::vector<Heard>& heard, juce::int64 at)
    {
        int n = 0;
        for (const auto& h : heard)
            n += h.on && h.sample >= at ? 1 : 0;
        return n;
    };
    auto lastOff = [] (const std::vector<Heard>& heard)
    {
        juce::int64 at = -1;
        for (const auto& h : heard)
            if (! h.on)
                at = juce::jmax (at, h.sample);
        return at;
    };

    std::unique_ptr<juce::AudioProcessor> processor (createPluginFilter());
    std::cout << "processor: " << processor->getName() << std::endl;
    for (const auto& [fault, want, label] : { std::tuple { 1.0f, 1, "an endless loop" }, std::tuple { 2.0f, 2, "an endless recursion" } })
    {
        setLogicValues (*processor, controls, { fault, 2.0f });
        TimedRun run;
        withWatchdog (label, 20.0, [&] { run = timedRun (*processor, 4.0, faultSample); });
        const auto p = problemsWith (run.heard);
        const int stopped = pangeaLogicStopped (*processor);
        checkFailed (failures, stopped == want, juce::String (label) + ": the processor says "
                                                    + (stopped == 1 ? "stopped for running too long" : stopped == 2 ? "stopped for going too deep" : "not stopped"));
        checkFailed (failures, countOns (run.heard) == 5 && notesAfter (run.heard, faultSample) == 0,
                     juce::String (label) + ": " + juce::String (countOns (run.heard)) + " notes, the 5 before the fault and none after it");
        const auto off = lastOff (run.heard);
        checkFailed (failures, p.isEmpty() && off >= faultBlock * 512 && off < (faultBlock + 1) * 512,
                     juce::String (label) + ": the note sounding at the fault ends in that block, at sample " + juce::String (off)
                         + ", and none is left on" + (p.isEmpty() ? juce::String() : ": " + p.joinIntoString ("; ")));
        checkFailed (failures, run.slowestMs < 250.0 && run.slowestAfterStopMs < 1.0,
                     juce::String (label) + ": the slowest block took " + juce::String (run.slowestMs, 2) + " ms, every block after the stop under "
                         + juce::String (juce::jmax (0.01, run.slowestAfterStopMs), 2) + " ms");

        std::unique_ptr<juce::AudioProcessorEditor> editor (processor->createEditorIfNeeded());
        juce::String message;
        for (const auto& line : pangeaDescribeEditor (*editor))
            if (line.startsWith ("stopped message: "))
                message = line.fromFirstOccurrenceOf (": ", false, false);
        checkFailed (failures, message.isNotEmpty(), "the panel says: " + (message.isEmpty() ? juce::String ("nothing") : message));
        editor.reset();

        // The module on its own, as host-check compares it: stopped in the same block.
        int stoppedAt = -2;
        // A copy: before C++20 a lambda cannot capture a structured binding (Apple clang 14 refuses it).
        const auto faultValue = fault;
        withWatchdog (juce::String (label) + " alone", 20.0, [&] { moduleNotes (sampleRate, { faultValue, 2.0f }, straightPlan (sampleRate, 120.0, 512, 4.0), &stoppedAt); });
        checkFailed (failures, stoppedAt == faultBlock, juce::String (label) + ": the module alone stops in block " + juce::String (stoppedAt)
                                                           + ", where the fault starts (" + juce::String (faultBlock) + ")");
    }

    // Heavy honest work: never stopped. Then a fresh start after a stop plays on.
    setLogicValues (*processor, controls, { 3.0f, 2.0f });
    TimedRun heavy;
    withWatchdog ("heavy work", 60.0, [&] { heavy = timedRun (*processor, 4.0, faultSample); });
    checkFailed (failures, pangeaLogicStopped (*processor) == 0 && countOns (heavy.heard) == 16,
                 "heavy work: not stopped, all 16 notes, at most " + juce::String (pangeaMostLogicSteps (*processor))
                     + " steps a block, the slowest block " + juce::String (heavy.slowestMs, 2) + " ms");
    setLogicValues (*processor, controls, { 1.0f, 2.0f });
    withWatchdog ("a stop before a fresh start", 20.0, [&] { timedRun (*processor, 4.0, faultSample); });
    setLogicValues (*processor, controls, { 0.0f, 2.0f });
    TimedRun again;
    withWatchdog ("a fresh start", 20.0, [&] { again = timedRun (*processor, 4.0, faultSample); });
    checkFailed (failures, pangeaLogicStopped (*processor) == 0 && countOns (again.heard) == 16,
                 "after a stop, the host preparing the plugin again starts a fresh module: " + juce::String (countOns (again.heard)) + " notes");

    // The plugin a DAW loads.
    if (args.size() > 2)
    {
        const auto path = juce::File::getCurrentWorkingDirectory().getChildFile (args[2]).getFullPathName();
        juce::AudioPluginFormatManager formats;
        formats.addFormat (new juce::VST3PluginFormat());
        juce::OwnedArray<juce::PluginDescription> found;
        for (auto* f : formats.getFormats())
            if (f->fileMightContainThisPluginType (path))
                f->findAllTypesForFile (found, path);
        juce::String error;
        std::unique_ptr<juce::AudioPluginInstance> plugin;
        if (! found.isEmpty())
            plugin = formats.createPluginInstance (*found[0], sampleRate, 512, error);
        checkFailed (failures, plugin != nullptr, "the VST3 loads from " + path + (error.isEmpty() ? juce::String() : ": " + error));
        if (plugin != nullptr)
        {
            for (const auto& [fault, label] : { std::pair { 1.0f, "an endless loop" }, std::pair { 2.0f, "an endless recursion" } })
            {
                setByName (*plugin, controls[0].hostName(), fault, 0.0f, 3.0f);
                setByName (*plugin, controls[1].hostName(), 2.0f, 1.0f, 8.0f);
                runPlugin (*plugin, sampleRate, { { 0.0, 120.0, 4.0, 64, false } });   // a VST3 takes changes with the next block
                TimedRun run;
                withWatchdog (juce::String ("the VST3 with ") + label, 20.0, [&] { run = timedRun (*plugin, 4.0, faultSample); });
                const auto p = problemsWith (run.heard);
                checkFailed (failures, countOns (run.heard) == 5 && notesAfter (run.heard, faultSample) == 0 && p.isEmpty(),
                             juce::String ("the VST3 with ") + label + ": " + juce::String (countOns (run.heard))
                                 + " notes before the fault, none after, none left on; the slowest block " + juce::String (run.slowestMs, 2) + " ms"
                                 + (p.isEmpty() ? juce::String() : ": " + p.joinIntoString ("; ")));
            }
            setByName (*plugin, controls[0].hostName(), 0.0f, 0.0f, 3.0f);
            runPlugin (*plugin, sampleRate, { { 0.0, 120.0, 4.0, 64, false } });
            TimedRun fresh;
            withWatchdog ("the VST3, fresh", 20.0, [&] { fresh = timedRun (*plugin, 4.0, faultSample); });
            checkFailed (failures, countOns (fresh.heard) == 16, "the VST3 with no fault, prepared again: " + juce::String (countOns (fresh.heard)) + " notes");
        }
    }

    std::cout << (failures == 0 ? juce::String ("all stop checks passed") : juce::String (failures) + " stop checks failed") << std::endl;
    return failures == 0 ? 0 : 1;
}

} // namespace pangea::render
