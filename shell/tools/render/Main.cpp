// pangea-render: proves a built shell makes sound without a DAW.
//
//   pangea-render render <out.wav> [seconds] [sampleRate] [blockSize]
//       Builds the processor from this build's FaustDsp.h, plays a chord and a melody through it
//       (instrument) or feeds it a test signal (effect), writes the result and reports peak and RMS.
//       Exits 1 when the output is silent or not finite.
//
//   pangea-render check-vst3 <plugin.vst3> [sampleRate] [blockSize]
//       Loads the VST3 as a host would, lists what it found, renders one second with MIDI or a
//       test signal, and exits 1 when it fails to load or stays silent.
//
//   pangea-render editor <out.png> [--ui <ui.json>] [--set <id>=<value>]... [--scale <n>] [--width <points>] [--play <seconds>]
//       Draws the plugin's interface offscreen and writes it as a PNG (at 2x unless --scale says
//       otherwise). --ui draws this build with another ui.json, read and checked exactly as the
//       embedded one is, so one build can show every style. --set gives a control a value first
//       (in its own units; 1 or 0 for a switch). --width resizes the editor first, keeping its shape.
//       --play runs the processor with a playing transport first, so a logic module's playheads show.
//       --stopped 1 or 2 shows the message a module stopped by the step budget leaves on the panel.
//
//   pangea-render tempo [bpm]...
//       Plays one block at each tempo (95 and 174 BPM unless given) with a transport, then one with
//       none, and checks that every control marked [pangea:bpm] follows: the tempo inside the
//       control's range, and 120 BPM with no host tempo (decision 55). Exits 1 otherwise.
//
//   pangea-render transport
//       Plays blocks while a transport plays from bar one, jumps to bar 9, stops there, and then one
//       block with no transport, and checks that every control marked [pangea:playing] holds 1
//       while the transport plays and 0 while it is stopped, and every control marked [pangea:beat]
//       the song position at the start of each block, or the beats since the first block with no
//       transport (decision 123), and every control marked [pangea:beatfrac] that position's
//       fraction of a beat (decision 124). An effect is then played over the same song at block sizes of
//       64, 441 and 1024 samples beside 256, and must sound the same at each: what follows the song
//       steps on the beat, not on a block. Exits 1 otherwise, or when no control is marked.
//
//   pangea-render keys
//       The layout key of each parameter, one a line, in the plugin's order: the name the site
//       gives each control, and what an item of a ui.json layout places it by.
//
//   pangea-render effect-check [--vst3 <plugin.vst3>] [--expect <id>]... [--shared <id>]...
//       An instrument's effect section, the stage its voices share (decision 73). Lists the
//       section's controls by their keys and says where each lives, then plays the same notes
//       with each of its parameters at its lowest and at its highest, one at a time. Exits 1 when
//       a control is not bound to the effect; when the section has parameters of its own (ones
//       the voices do not read) and none of them changes the render, alone or all together; when
//       those would not come after the other parameters in an Audio Unit's list; when a parameter
//       named by --expect is not the section's or changes nothing; or when a control named by
//       --shared is not held by the voices too. With --vst3 the built plugin, loaded as a host
//       loads it, must have each of the section's parameters by name, and its render must change
//       when one of the section's own moves. A build with no effect section says so and passes.
//
//   pangea-render notes [bars] [bpm]
//   pangea-render host-check <plugin.vst3>
//   pangea-render stop-check [<plugin.vst3>]
//       A logic module's notes, offline and through the built VST3 in a host, and the step budget
//       with tools/fixtures/runaway (MidiChecks.cpp).
//
//   pangea-render bypass-check [--vst3 <plugin.vst3>]
//       Pangea's bypass, the parameter every plugin has (decision 75): what a bypassed effect,
//       instrument or MIDI tool gives, and that every switch is a fade (BypassChecks.cpp).
//
//   pangea-render sidechain-check [--vst3 <plugin.vst3>] [--wav <out.wav>] [--expect-duck <dB>]
//   pangea-render describe-vst3 <plugin.vst3>
//       The sidechain input (docs/sidechain.md): the buses with and without one, and an
//       effect keyed by a kick in its sidechain; and what a host stores of a VST3, to compare two
//       builds (SidechainChecks.cpp).
//
//   pangea-render meter-check [--expect <n>] [--tree <out.json>]
//       The bar graphs the web panel's meters and step lights read (decision 91; PangeaMeters.h).
//       Lists what this build taps, plays its test signal or its notes for one second, and exits 1
//       unless: it taps --expect bar graphs (when given); each block is counted once; every value
//       is a number; and an instrument taps nothing in its voice. --tree writes what the page is
//       handed, `{ kind, tree }`, so a test can find the meters in it as the page does.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>
#include <iostream>
#include <map>
#include "PangeaMeters.h"
#include "RenderHooks.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();
juce::AudioProcessorEditor* pangeaCreateEditorWithUi (juce::AudioProcessor&, const void* uiData, size_t uiSize);
juce::StringArray pangeaDescribeEditor (juce::AudioProcessorEditor&);
void pangeaShowStopped (juce::AudioProcessor&, int why);

namespace pangea::render
{
int notesCommand (const juce::StringArray& args);
int hostCheckCommand (const juce::StringArray& args);
int stopCheckCommand (const juce::StringArray& args);
int bypassCheckCommand (const juce::StringArray& args);
int sidechainCheckCommand (const juce::StringArray& args);
int describeVst3Command (const juce::StringArray& args);
}

namespace
{

struct Stats
{
    float peak = 0;
    double sumSquares = 0;
    juce::int64 count = 0;
    bool finite = true;

    void add (const juce::AudioBuffer<float>& b, int n)
    {
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
        {
            const float* p = b.getReadPointer (ch);
            for (int i = 0; i < n; ++i)
            {
                if (! std::isfinite (p[i])) finite = false;
                peak = std::max (peak, std::abs (p[i]));
                sumSquares += (double) p[i] * p[i];
            }
        }
        count += (juce::int64) n * b.getNumChannels();
    }

    double rms() const { return count > 0 ? std::sqrt (sumSquares / (double) count) : 0.0; }
};

// A simple musical programme: a C minor chord, then a short melody, with the sustain pedal on the last note.
void scheduleMidi (juce::MidiMessageSequence& seq, double sampleRate)
{
    auto at = [&] (double seconds) { return seconds * sampleRate; };
    auto note = [&] (int pitch, double start, double length, int velocity)
    {
        seq.addEvent (juce::MidiMessage::noteOn (1, pitch, (juce::uint8) velocity), at (start));
        seq.addEvent (juce::MidiMessage::noteOff (1, pitch), at (start + length));
    };
    note (48, 0.05, 1.2, 100);
    note (51, 0.05, 1.2, 90);
    note (55, 0.05, 1.2, 90);
    note (60, 1.4, 0.25, 110);
    note (63, 1.7, 0.25, 110);
    note (67, 2.0, 0.25, 110);
    seq.addEvent (juce::MidiMessage::controllerEvent (1, 64, 127), at (2.3));
    note (72, 2.35, 0.1, 120);
    seq.addEvent (juce::MidiMessage::controllerEvent (1, 64, 0), at (2.9));
    seq.updateMatchedPairs();
}

// A test signal for effects: a few clicks and a decaying sine burst.
void fillTestSignal (juce::AudioBuffer<float>& b, juce::int64 startSample, double sampleRate)
{
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
    {
        float* p = b.getWritePointer (ch);
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const double t = (double) (startSample + i) / sampleRate;
            double v = 0.0;
            if (t < 0.5)
                v = 0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * t) * std::exp (-6.0 * t);
            const double beat = std::fmod (t, 0.5);
            if (beat < 1.0 / sampleRate * 4 && t > 0.9)
                v += 0.8;
            p[i] = (float) v;
        }
    }
}

// A minimal DAW transport, playing at 124 BPM from bar one, so logic modules have something to follow.
struct Transport : public juce::AudioPlayHead
{
    double sampleRate = 48000.0, bpm = 124.0;
    juce::int64 samples = 0;

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm (bpm);
        info.setTimeInSamples (samples);
        info.setTimeInSeconds ((double) samples / sampleRate);
        info.setPpqPosition ((double) samples * bpm / (60.0 * sampleRate));
        info.setIsPlaying (true);
        info.setTimeSignature (TimeSignature { 4, 4 });
        return info;
    }
};

// `heard`, when given, receives every sample of the run, block by block and channel by channel.
Stats run (juce::AudioProcessor& processor, double sampleRate, int blockSize, double seconds,
           juce::AudioFormatWriter* writer, std::vector<float>* heard = nullptr)
{
    const int totalSamples = (int) (seconds * sampleRate);
    const int ins = processor.getTotalNumInputChannels();
    const int outs = processor.getTotalNumOutputChannels();
    const int channels = std::max (1, std::max (ins, outs));

    juce::MidiMessageSequence sequence;
    if (processor.acceptsMidi())
        scheduleMidi (sequence, sampleRate);

    juce::AudioBuffer<float> buffer (channels, blockSize);
    juce::MidiBuffer midi;
    Stats stats;
    int nextEvent = 0;

    Transport transport;
    transport.sampleRate = sampleRate;
    processor.setPlayHead (&transport);
    processor.setPlayConfigDetails (ins, outs, sampleRate, blockSize);
    processor.prepareToPlay (sampleRate, blockSize);

    for (int start = 0; start < totalSamples; start += blockSize)
    {
        const int n = std::min (blockSize, totalSamples - start);
        transport.samples = start;
        buffer.clear();
        if (ins > 0)
            fillTestSignal (buffer, start, sampleRate);

        midi.clear();
        while (nextEvent < sequence.getNumEvents())
        {
            const auto* ev = sequence.getEventPointer (nextEvent);
            const int offset = (int) std::llround (ev->message.getTimeStamp()) - start;
            if (offset >= n) break;
            midi.addEvent (ev->message, std::max (0, offset));
            ++nextEvent;
        }

        juce::AudioBuffer<float> block (buffer.getArrayOfWritePointers(), channels, n);
        processor.processBlock (block, midi);
        stats.add (block, n);
        if (writer != nullptr)
            writer->writeFromAudioSampleBuffer (block, 0, n);
        if (heard != nullptr)
            for (int ch = 0; ch < channels; ++ch)
                heard->insert (heard->end(), block.getReadPointer (ch), block.getReadPointer (ch) + n);
    }
    processor.releaseResources();
    processor.setPlayHead (nullptr);
    return stats;
}

int report (const Stats& stats, const juce::String& what)
{
    const bool loud = stats.peak > 0.001f;
    std::cout << what << ": peak " << juce::String (stats.peak, 4) << " ("
              << juce::String (juce::Decibels::gainToDecibels (stats.peak), 1) << " dBFS), rms "
              << juce::String (stats.rms(), 4) << ", finite " << (stats.finite ? "yes" : "no")
              << ", sound " << (loud ? "yes" : "no") << std::endl;
    return (loud && stats.finite) ? 0 : 1;
}

int renderCommand (const juce::StringArray& args)
{
    if (args.size() < 3) { std::cerr << "usage: pangea-render render <out.wav> [seconds] [sampleRate] [blockSize]\n"; return 2; }
    const juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile (args[2]);
    const double seconds = args.size() > 3 ? args[3].getDoubleValue() : 3.5;
    const double sampleRate = args.size() > 4 ? args[4].getDoubleValue() : 48000.0;
    const int blockSize = args.size() > 5 ? args[5].getIntValue() : 256;

    std::unique_ptr<juce::AudioProcessor> processor (createPluginFilter());
    std::cout << "processor: " << processor->getName() << ", inputs " << processor->getTotalNumInputChannels()
              << ", outputs " << processor->getTotalNumOutputChannels() << ", midi "
              << (processor->acceptsMidi() ? "yes" : "no") << ", parameters " << processor->getParameters().size() << std::endl;
    for (auto* p : processor->getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            std::cout << "  " << ranged->getParameterID() << " = " << ranged->getCurrentValueAsText()
                      << " (" << ranged->getName (64) << ")" << std::endl;

    out.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (new juce::FileOutputStream (out), sampleRate,
                                                                          (unsigned) std::max (1, processor->getTotalNumOutputChannels()),
                                                                          24, {}, 0));
    if (writer == nullptr) { std::cerr << "cannot write " << out.getFullPathName() << "\n"; return 1; }

    const auto stats = run (*processor, sampleRate, blockSize, seconds, writer.get());
    writer.reset();
    std::cout << "wrote " << out.getFullPathName() << " (" << seconds << " s at " << sampleRate << " Hz, block " << blockSize << ")" << std::endl;
    return report (stats, "render");
}

int checkVst3Command (const juce::StringArray& args)
{
    if (args.size() < 3) { std::cerr << "usage: pangea-render check-vst3 <plugin.vst3> [sampleRate] [blockSize]\n"; return 2; }
    const juce::File bundle = juce::File::getCurrentWorkingDirectory().getChildFile (args[2]);
    const double sampleRate = args.size() > 3 ? args[3].getDoubleValue() : 48000.0;
    const int blockSize = args.size() > 4 ? args[4].getIntValue() : 512;

    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> descriptions;
    format.findAllTypesForFile (descriptions, bundle.getFullPathName());
    if (descriptions.isEmpty()) { std::cerr << "no plugin found in " << bundle.getFullPathName() << "\n"; return 1; }

    juce::String error;
    auto instance = format.createInstanceFromDescription (*descriptions[0], sampleRate, blockSize, error);
    if (instance == nullptr) { std::cerr << "load failed: " << error << "\n"; return 1; }

    const auto& d = *descriptions[0];
    std::cout << "vst3: " << d.name << " by " << d.manufacturerName << ", version " << d.version
              << ", " << (d.isInstrument ? "instrument" : "effect") << ", " << d.numInputChannels << " in, "
              << d.numOutputChannels << " out, " << instance->getParameters().size() << " parameters" << std::endl;

    instance->enableAllBuses();
    const auto stats = run (*instance, sampleRate, blockSize, 2.5, nullptr);
    return report (stats, "vst3 render");
}

// One block at each tempo, then one with no transport: each [pangea:bpm] control must follow (decision 55).
int tempoCommand (const juce::StringArray& args)
{
    std::vector<double> tempos;
    for (int i = 2; i < args.size(); ++i)
        tempos.push_back (args[i].getDoubleValue());
    if (tempos.empty())
        tempos = { 95.0, 174.0 };

    const double sampleRate = 48000.0;
    const int blockSize = 256;
    std::unique_ptr<juce::AudioProcessor> processor (createPluginFilter());
    const int ins = processor->getTotalNumInputChannels();
    const int outs = processor->getTotalNumOutputChannels();
    processor->setPlayConfigDetails (ins, outs, sampleRate, blockSize);
    processor->prepareToPlay (sampleRate, blockSize);
    juce::AudioBuffer<float> buffer (std::max (1, std::max (ins, outs)), blockSize);
    juce::MidiBuffer midi;
    Transport transport;
    transport.sampleRate = sampleRate;

    int failures = 0;
    auto check = [&] (const juce::String& what, double tempo)
    {
        buffer.clear();
        midi.clear();
        processor->processBlock (buffer, midi);
        std::vector<pangea::render::TempoValue> values;
        for (const auto& v : pangeaTempoValues (*processor))
            if (v.kind == "bpm")
                values.push_back (v);
        if (values.empty())
        {
            std::cout << what << ": no control is marked [pangea:bpm]" << std::endl;
            ++failures;
            return;
        }
        for (const auto& v : values)
        {
            const double expected = juce::jlimit ((double) v.min, (double) v.max, tempo);
            const bool ok = std::abs ((double) v.value - expected) < 0.01;
            std::cout << what << ": " << v.label << " = " << juce::String (v.value, 2) << " (expected "
                      << juce::String (expected, 2) << ") " << (ok ? "ok" : "WRONG") << std::endl;
            if (! ok)
                ++failures;
        }
    };

    processor->setPlayHead (&transport);
    for (double bpm : tempos)
    {
        transport.bpm = bpm;
        check ("host at " + juce::String (bpm, 1) + " BPM", bpm);
    }
    processor->setPlayHead (nullptr);
    check ("no host tempo", 120.0);
    processor->releaseResources();
    return failures == 0 ? 0 : 1;
}

// ---- The host's transport (decision 123) ---------------------------------------------------------

// A DAW's play head that a check moves by hand: the tempo, the song position and whether it plays.
struct SongHead : public juce::AudioPlayHead
{
    double bpm = 120.0, ppq = 0.0;
    bool playing = true;
    bool hasPpq = true;   // false: a position with no beats, as JUCE's AU wrapper gives a host without a transport

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm (bpm);
        if (hasPpq)
            info.setPpqPosition (ppq);
        info.setIsPlaying (playing);
        info.setTimeSignature (TimeSignature { 4, 4 });
        return info;
    }
};

// The left channel of an effect played for four seconds over a song from bar one at 120 BPM, in
// blocks of `blockSize`, fed a 440 Hz sine whose every sample is the same whatever the block size.
std::vector<float> songTake (double sampleRate, int blockSize)
{
    std::unique_ptr<juce::AudioProcessor> processor (createPluginFilter());
    const int ins = processor->getTotalNumInputChannels();
    const int outs = processor->getTotalNumOutputChannels();
    processor->setPlayConfigDetails (ins, outs, sampleRate, blockSize);
    processor->prepareToPlay (sampleRate, blockSize);
    SongHead head;
    processor->setPlayHead (&head);
    juce::AudioBuffer<float> buffer (std::max (1, std::max (ins, outs)), blockSize);
    juce::MidiBuffer midi;
    std::vector<float> left;
    const int total = (int) (4.0 * sampleRate);
    for (int done = 0; done < total; done += blockSize)
    {
        buffer.clear();
        for (int ch = 0; ch < ins; ++ch)
            for (int i = 0; i < blockSize; ++i)
                buffer.setSample (ch, i, 0.25f * (float) std::sin (juce::MathConstants<double>::twoPi * 440.0 * (done + i) / sampleRate));
        midi.clear();
        processor->processBlock (buffer, midi);
        for (int i = 0; i < blockSize; ++i)
            left.push_back (buffer.getSample (0, i));
        head.ppq += blockSize * head.bpm / (60.0 * sampleRate);
    }
    processor->setPlayHead (nullptr);
    processor->releaseResources();
    left.resize ((size_t) total);
    return left;
}

int transportCommand()
{
    const double sampleRate = 48000.0;
    const int blockSize = 256;
    std::unique_ptr<juce::AudioProcessor> processor (createPluginFilter());
    const int ins = processor->getTotalNumInputChannels();
    const int outs = processor->getTotalNumOutputChannels();
    processor->setPlayConfigDetails (ins, outs, sampleRate, blockSize);
    processor->prepareToPlay (sampleRate, blockSize);
    juce::AudioBuffer<float> buffer (std::max (1, std::max (ins, outs)), blockSize);
    juce::MidiBuffer midi;
    SongHead head;
    processor->setPlayHead (&head);
    const double beatsPerBlock = blockSize * head.bpm / (60.0 * sampleRate);

    int failures = 0;
    juce::int64 blocks = 0;
    // One block, then each transport control must hold what a host at this point gives it.
    auto play = [&] (const juce::String& what, double playing, double beat)
    {
        buffer.clear();
        midi.clear();
        processor->processBlock (buffer, midi);
        ++blocks;
        int seen = 0;
        for (const auto& v : pangeaTempoValues (*processor))
        {
            if (v.kind == "bpm")
                continue;
            ++seen;
            const double held = v.kind == "playing" ? playing : v.kind == "beatfrac" ? beat - std::floor (beat) : beat;
            const double expected = juce::jlimit ((double) v.min, (double) v.max, held);
            const bool ok = std::abs ((double) v.value - expected) < 0.001 + std::abs (expected) * 1.0e-6;
            std::cout << what << ": " << v.label << " = " << juce::String (v.value, 4) << " (expected "
                      << juce::String (expected, 4) << ") " << (ok ? "ok" : "WRONG") << std::endl;
            if (! ok)
                ++failures;
        }
        return seen;
    };

    if (play ("playing from bar 1", 1.0, 0.0) == 0)
    {
        std::cout << "no control is marked [pangea:playing] or [pangea:beat]" << std::endl;
        return 1;
    }
    for (int b = 2; b <= 3; ++b)
    {
        head.ppq += beatsPerBlock;
        play ("playing, block " + juce::String (b), 1.0, head.ppq);
    }
    head.ppq = 32.0;   // a jump to bar 9, as a cycle or a click on the ruler makes
    play ("a jump to bar 9", 1.0, 32.0);
    head.ppq += beatsPerBlock;
    play ("bar 9, the next block", 1.0, head.ppq);
    head.playing = false;
    head.ppq = 33.5;
    play ("stopped at beat 33.5", 0.0, 33.5);
    play ("still stopped", 0.0, 33.5);
    // A host that says stopped but gives no beats counts as playing, from the beats counted since
    // the start (PangeaSong.h; decision 124): otherwise a pattern would never move in it.
    head.hasPpq = false;
    play ("stopped, with no beats", 1.0, (double) blocks * beatsPerBlock);
    processor->setPlayHead (nullptr);
    const double counted = (double) blocks * beatsPerBlock;
    play ("no transport", 1.0, counted);
    processor->releaseResources();

    if (ins == 0)
    {
        std::cout << "an instrument: the sound is checked at one block size only, by render" << std::endl;
        return failures == 0 ? 0 : 1;
    }

    // A step one sample early or late under a 1 ms ramp moves a 0.25 signal by about 0.005; a step on
    // a block boundary instead of the beat moves it by up to the whole level.
    const auto reference = songTake (sampleRate, 256);
    float lowest = 1.0f, highest = 0.0f;
    for (size_t i = 0; i + 96 <= reference.size(); i += 96)
    {
        float peak = 0.0f;
        for (size_t k = i; k < i + 96; ++k)
            peak = std::max (peak, std::abs (reference[k]));
        lowest = std::min (lowest, peak);
        highest = std::max (highest, peak);
    }
    std::cout << "over the song at block size 256 the level moves between " << juce::String (lowest, 3)
              << " and " << juce::String (highest, 3) << std::endl;
    for (int size : { 64, 441, 1024 })
    {
        const auto other = songTake (sampleRate, size);
        float worst = 0.0f;
        for (size_t i = 0; i < reference.size() && i < other.size(); ++i)
            if (std::isfinite (other[i]))
                worst = std::max (worst, std::abs (other[i] - reference[i]));
            else
                worst = 1.0f;
        const bool ok = worst < 0.02f;
        std::cout << "block size " << size << ": differs from block size 256 by at most " << juce::String (worst, 4)
                  << (ok ? " ok" : " WRONG") << std::endl;
        if (! ok)
            ++failures;
    }
    return failures == 0 ? 0 : 1;
}

// The layout key of each parameter, one a line: what the site calls each control (src/lib/ui/keys.ts).
int keysCommand()
{
    std::unique_ptr<juce::AudioProcessor> processor (createPluginFilter());
    for (const auto& key : pangeaParameterKeys (*processor))
        std::cout << key << std::endl;
    return 0;
}

// ---- An instrument's effect section (decision 73) -----------------------------------------------

struct Take
{
    Stats stats;
    std::vector<float> heard;
};

constexpr double kTakeRate = 48000.0, kTakeSeconds = 2.5;
constexpr int kTakeBlock = 256;
constexpr float kHeard = 1.0e-5f;   // -100 dBFS: two takes further apart than this differ

// How far apart two takes of the same notes are at their furthest sample. A take with a sample
// that is not a number proves nothing either way, so it is never apart from anything.
float apart (const Take& a, const Take& b)
{
    if (! a.stats.finite || ! b.stats.finite || a.heard.size() != b.heard.size())
        return 0.0f;
    float furthest = 0.0f;
    for (size_t i = 0; i < a.heard.size(); ++i)
        furthest = std::max (furthest, std::abs (a.heard[i] - b.heard[i]));
    return furthest;
}

// True when two takes of the same notes are not the same sound.
bool differ (const Take& a, const Take& b) { return apart (a, b) > kHeard; }

juce::String level (const Take& take)
{
    return take.stats.finite ? "peak " + juce::String (juce::Decibels::gainToDecibels (take.stats.peak), 1) + " dBFS" : juce::String ("not a number");
}

// The usual notes through a fresh processor, with the named parameters at one end of their ranges
// first (none gives the defaults).
Take takeWith (const juce::StringArray& ids, bool highest)
{
    std::unique_ptr<juce::AudioProcessor> processor (createPluginFilter());
    for (auto* p : processor->getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p); ranged != nullptr && ids.contains (ranged->getParameterID()))
            ranged->setValueNotifyingHost (highest ? 1.0f : 0.0f);
    Take take;
    take.stats = run (*processor, kTakeRate, kTakeBlock, kTakeSeconds, nullptr, &take.heard);
    return take;
}

// A host sees names, not ids: the parameter with this name, the first, second... of that name.
juce::AudioProcessorParameter* parameterNamed (juce::AudioProcessor& processor, const juce::String& name, int occurrence)
{
    for (auto* p : processor.getParameters())
        if (p->getName (128) == name && occurrence-- == 0)
            return p;
    return nullptr;
}

// The same notes through the built VST3, loaded afresh as a host loads it.
bool takeFromVst3 (const juce::PluginDescription& description, const juce::String& name, int occurrence, bool highest,
                   Take& take, juce::String& error)
{
    juce::VST3PluginFormat format;
    auto instance = format.createInstanceFromDescription (description, kTakeRate, kTakeBlock, error);
    if (instance == nullptr)
        return false;
    instance->enableAllBuses();
    if (name.isNotEmpty())
    {
        auto* p = parameterNamed (*instance, name, occurrence);
        if (p == nullptr)
        {
            error = "it has no parameter named " + name;
            return false;
        }
        p->setValueNotifyingHost (highest ? 1.0f : 0.0f);   // taken with the first block it processes
    }
    take.stats = run (*instance, kTakeRate, kTakeBlock, kTakeSeconds, nullptr, &take.heard);
    return true;
}

// The effect section is in the plugin and is heard: its controls are parameters, each bound to the
// effect, and moving the ones that are its own changes what the same notes sound like. A control
// the voices read too proves nothing of the effect, since it changes the sound through them.
int effectCheckCommand (const juce::StringArray& args)
{
    juce::String vst3Path;
    juce::StringArray expected, shared;
    for (int i = 2; i < args.size(); ++i)
    {
        const bool hasValue = i + 1 < args.size();
        if (args[i] == "--vst3" && hasValue) vst3Path = args[++i];
        else if (args[i] == "--expect" && hasValue) expected.add (args[++i]);
        else if (args[i] == "--shared" && hasValue) shared.add (args[++i]);
        else { std::cerr << "unknown or incomplete option: " << args[i] << "\n"; return 2; }
    }

    std::unique_ptr<juce::AudioProcessor> processor (createPluginFilter());
    const auto controls = pangeaEffectControls (*processor);
    if (! pangeaHasEffectSection())
    {
        std::cout << "effect-check: " << processor->getName() << " has no effect section" << std::endl;
        return expected.isEmpty() && shared.isEmpty() ? 0 : 1;
    }

    int failures = 0;
    auto fail = [&failures] (const juce::String& what)
    {
        std::cout << "FAIL  " << what << std::endl;
        ++failures;
    };
    auto find = [&controls] (const juce::String& id) -> const pangea::render::EffectControl*
    {
        for (const auto& c : controls)
            if (c.id == id)
                return &c;
        return nullptr;
    };

    std::cout << "effect-check: " << processor->getName() << " has an effect section with " << controls.size()
              << (controls.size() == 1 ? " control" : " controls") << std::endl;
    const auto usual = takeWith ({}, false);
    std::cout << "  at the defaults: " << level (usual) << std::endl;

    struct Moved { juce::String id; bool highest = false; bool own = false; float by = 0.0f; };
    std::vector<Moved> moved;   // each parameter that changes the render, the end of its range that changes it most, and by how much
    juce::StringArray own;      // the parameters that only the effect has
    int parameters = 0;
    for (const auto& c : controls)
    {
        const bool isOwn = c.voiceZones == 0;
        const auto where = isOwn ? juce::String ("in the effect")
                                 : "in the effect and in " + juce::String (c.voiceZones) + (c.voiceZones == 1 ? " voice" : " voices");
        if (c.effectZones != 1)
        {
            fail (c.key + " (" + c.id + ") is not bound to the effect section");
            continue;
        }
        if (! c.parameter)
        {
            std::cout << "  " << c.key << ": " << where << ", not a parameter (the host's tempo, a voice control or a logic control)" << std::endl;
            continue;
        }
        ++parameters;
        if (isOwn)
            own.add (c.id);
        // Each end of its range in turn, unless that end is where it already stands.
        float standsAt = 0.5f;
        for (auto* p : processor->getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p); ranged != nullptr && ranged->getParameterID() == c.id)
                standsAt = ranged->getDefaultValue();
        float mostBy = 0.0f;         // the furthest either end takes the render from the defaults
        bool mostAtHighest = false;
        juce::String line = "  " + c.key + " (" + c.id + "): " + where;
        for (const bool highest : { false, true })
        {
            const auto end = juce::String (highest ? "highest " : "lowest ") + juce::String (highest ? c.max : c.min);
            if (std::abs (standsAt - (highest ? 1.0f : 0.0f)) < 1.0e-6f)
            {
                line += "; its " + end + " is the default";
                continue;
            }
            const auto take = takeWith ({ c.id }, highest);
            const float by = apart (usual, take);
            if (by > mostBy)
            {
                mostBy = by;
                mostAtHighest = highest;
            }
            line += "; at its " + end + " " + level (take) + ((by > kHeard || ! take.stats.finite) ? "" : ", no change");
        }
        if (mostBy > kHeard)
            moved.push_back ({ c.id, mostAtHighest, isOwn, mostBy });
        std::cout << line << std::endl;
    }

    // Heard: one of its own parameters changes the render, alone or, failing that, all together
    // (one may do nothing until another is turned up).
    bool heard = false;
    for (const auto& m : moved)
        heard = heard || m.own;
    if (own.isEmpty())
        std::cout << "  the effect section has no parameter of its own, so nothing here shows whether it is heard" << std::endl;
    else if (! heard)
    {
        for (const bool highest : { false, true })
        {
            const auto take = takeWith (own, highest);
            const bool moves = differ (usual, take);
            heard = heard || moves;
            std::cout << "  all " << own.size() << " of its own parameters at their " << (highest ? "highest" : "lowest") << " together: "
                      << level (take) << ((moves || ! take.stats.finite) ? "" : ", no change") << std::endl;
        }
        if (! heard)
            fail ("none of the effect section's own " + juce::String (own.size()) + " parameters changes the render, alone or together: the effect is not heard");
    }

    auto movedBy = [&moved] (const juce::String& id) -> const Moved*
    {
        for (const auto& m : moved)
            if (m.id == id)
                return &m;
        return nullptr;
    };
    for (const auto& id : expected)
    {
        const auto* c = find (id);
        if (c == nullptr || ! c->parameter)
            fail (id + " should be a parameter of the effect section and is not");
        else if (movedBy (id) == nullptr)
            fail (id + " should change the render and does not");
    }
    for (const auto& id : shared)
        if (const auto* c = find (id); c == nullptr || c->voiceZones < 1 || c->effectZones != 1)
            fail (id + " should be one control of the voices and of the effect section, and is not");

    // An Audio Unit lists parameters by version hint, and Logic keeps automation by place in the
    // list: the effect's own parameters must come after every other control, as they would in a
    // plugin that was first built without its effect section. Pangea's bypass (decision 75) is
    // no control of the project's: it must come after them all, the effect's own included.
    {
        int latestOther = 0, earliestOwn = 0, latestOwn = 0, bypassHint = 0;
        bool anyOther = false, anyOwn = false;
        const auto* bypass = processor->getBypassParameter();
        for (auto* p : processor->getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            {
                const int hint = ranged->getVersionHint();
                if (ranged == bypass)
                    bypassHint = hint;
                else if (own.contains (ranged->getParameterID()))
                {
                    earliestOwn = anyOwn ? std::min (earliestOwn, hint) : hint;
                    latestOwn = anyOwn ? std::max (latestOwn, hint) : hint;
                    anyOwn = true;
                }
                else
                {
                    latestOther = anyOther ? std::max (latestOther, hint) : hint;
                    anyOther = true;
                }
            }
        if (anyOwn && anyOther)
        {
            if (earliestOwn > latestOther)
                std::cout << "  in an Audio Unit's list its own parameters come after the others (version hints " << earliestOwn
                          << " and " << latestOther << ")" << std::endl;
            else
                fail ("the effect section's own parameters would not come after the others in an Audio Unit's list (version hints "
                      + juce::String (earliestOwn) + " and " + juce::String (latestOther) + ")");
        }
        if (bypass == nullptr)
            fail ("the plugin has no bypass of Pangea's own");
        else if (bypassHint > std::max (latestOwn, latestOther))
            std::cout << "  and Pangea's bypass comes after them all (version hint " << bypassHint << ")" << std::endl;
        else
            fail ("Pangea's bypass would not come last in an Audio Unit's list (version hint " + juce::String (bypassHint)
                  + ", where a control has " + juce::String (std::max (latestOwn, latestOther)) + ")");
    }

    if (vst3Path.isNotEmpty())
    {
        const juce::File bundle = juce::File::getCurrentWorkingDirectory().getChildFile (vst3Path);
        juce::VST3PluginFormat format;
        juce::OwnedArray<juce::PluginDescription> descriptions;
        format.findAllTypesForFile (descriptions, bundle.getFullPathName());
        juce::String error;
        auto instance = descriptions.isEmpty() ? nullptr : format.createInstanceFromDescription (*descriptions[0], kTakeRate, kTakeBlock, error);
        if (instance == nullptr)
        {
            fail ("the VST3 did not load from " + bundle.getFullPathName() + (error.isEmpty() ? juce::String() : ": " + error));
        }
        else
        {
            // Each of the section's parameters, by the name and the place it has in the processor.
            struct Hosted { juce::String name; int occurrence = 0; };
            std::map<juce::String, Hosted> hosted;
            std::map<juce::String, int> seen;
            for (auto* p : processor->getParameters())
                if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
                {
                    const auto name = ranged->getName (128);
                    hosted[ranged->getParameterID()] = { name, seen[name]++ };
                }
            int found = 0;
            for (const auto& c : controls)
            {
                if (! c.parameter)
                    continue;
                const auto& h = hosted[c.id];
                if (parameterNamed (*instance, h.name, h.occurrence) != nullptr)
                    ++found;
                else
                    fail ("the VST3 has no parameter named " + h.name + " (" + c.key + ")");
            }
            instance.reset();
            std::cout << "  the VST3, loaded as a host loads it, has " << found << " of the section's " << parameters << " parameters" << std::endl;

            // One parameter moved in the VST3: every one that was expected, or the one of the
            // effect's own that changes the render most.
            std::vector<Moved> probes;
            for (const auto& id : expected)
                if (const auto* m = movedBy (id))
                    probes.push_back (*m);
            if (probes.empty())
            {
                const Moved* most = nullptr;
                for (const auto& m : moved)
                    if (m.own && (most == nullptr || m.by > most->by))
                        most = &m;
                if (most != nullptr)
                    probes.push_back (*most);
            }
            Take usualVst3;
            if (probes.empty())
                std::cout << "  no parameter of its own changes the render alone, so none was moved in the VST3" << std::endl;
            else if (! takeFromVst3 (*descriptions[0], {}, 0, false, usualVst3, error))
                fail ("the VST3 could not be played: " + error);
            else
                for (const auto& probe : probes)
                {
                    const auto& h = hosted[probe.id];
                    Take changed;
                    if (! takeFromVst3 (*descriptions[0], h.name, h.occurrence, probe.highest, changed, error))
                        fail ("the VST3 could not be played with " + h.name + " moved: " + error);
                    else if (! differ (usualVst3, changed))
                        fail ("moving " + h.name + " in the VST3 does not change its render");
                    else
                        std::cout << "  in the VST3, " << h.name << " at its " << (probe.highest ? "highest" : "lowest") << ": " << level (changed)
                                  << ", where the defaults give " << level (usualVst3) << std::endl;
                }
        }
    }

    std::cout << (failures == 0 ? juce::String ("effect-check: passed") : "effect-check: " + juce::String (failures) + " failed") << std::endl;
    return failures == 0 ? 0 : 1;
}

// Draws the plugin's interface offscreen and writes it as a PNG, so the look can be checked without a host.
int editorCommand (const juce::StringArray& args)
{
    if (args.size() < 3) { std::cerr << "usage: pangea-render editor <out.png> [--ui <ui.json>] [--set <id>=<value>]... [--scale <n>] [--width <points>]\n"; return 2; }
    const juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile (args[2]);
    juce::String uiPath;
    juce::StringArray settings;
    float scale = 2.0f;
    int width = 0;
    double playSeconds = 0.0;
    int stoppedWhy = 0;
    for (int i = 3; i < args.size(); ++i)
    {
        const bool hasValue = i + 1 < args.size();
        if (args[i] == "--ui" && hasValue) uiPath = args[++i];
        else if (args[i] == "--set" && hasValue) settings.add (args[++i]);
        else if (args[i] == "--scale" && hasValue) scale = juce::jlimit (0.5f, 4.0f, args[++i].getFloatValue());
        else if (args[i] == "--width" && hasValue) width = args[++i].getIntValue();
        else if (args[i] == "--play" && hasValue) playSeconds = juce::jlimit (0.0, 60.0, args[++i].getDoubleValue());
        else if (args[i] == "--stopped" && hasValue) stoppedWhy = juce::jlimit (0, 3, args[++i].getIntValue());
        else { std::cerr << "unknown or incomplete option: " << args[i] << "\n"; return 2; }
    }

    std::unique_ptr<juce::AudioProcessor> processor (createPluginFilter());
    for (const auto& setting : settings)
    {
        const auto id = setting.upToFirstOccurrenceOf ("=", false, false);
        const auto value = setting.fromFirstOccurrenceOf ("=", false, false).getFloatValue();
        bool found = false;
        for (auto* p : processor->getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p); ranged != nullptr && ranged->getParameterID() == id)
            {
                ranged->setValueNotifyingHost (ranged->convertTo0to1 (value));
                found = true;
            }
        if (! found) { std::cerr << "no parameter with the id " << id << "\n"; return 2; }
    }
    if (playSeconds > 0.0)
    {
        run (*processor, 48000.0, 256, playSeconds, nullptr);   // the transport is still playing at the end
        std::cout << "editor: played " << playSeconds << " s at 124 BPM first" << std::endl;
    }
    if (stoppedWhy > 0)
        pangeaShowStopped (*processor, stoppedWhy);

    std::unique_ptr<juce::AudioProcessorEditor> editor;
    if (uiPath.isNotEmpty())
    {
        juce::MemoryBlock bytes;
        if (! juce::File::getCurrentWorkingDirectory().getChildFile (uiPath).loadFileAsData (bytes))
        {
            std::cerr << "cannot read " << uiPath << "\n";
            return 1;
        }
        editor.reset (pangeaCreateEditorWithUi (*processor, bytes.getData(), bytes.getSize()));
    }
    else
        editor.reset (processor->createEditorIfNeeded());
    if (editor == nullptr) { std::cerr << "the processor has no editor\n"; return 1; }
    if (width > 0)
    {
        const double aspect = (double) editor->getWidth() / (double) editor->getHeight();
        editor->setSize (width, juce::roundToInt (width / aspect));
    }

    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), false, scale);
    out.deleteFile();
    juce::FileOutputStream stream (out);
    if (! stream.openedOk() || ! juce::PNGImageFormat().writeImageToStream (image, stream))
    {
        std::cerr << "cannot write " << out.getFullPathName() << "\n";
        return 1;
    }
    for (const auto& line : pangeaDescribeEditor (*editor))
        std::cout << "editor: " << line << std::endl;
    std::cout << "editor: " << editor->getWidth() << " x " << editor->getHeight() << " points at " << scale
              << "x, wrote " << out.getFullPathName() << std::endl;
    editor.reset();
    return 0;
}

} // namespace

int meterCheckCommand (const juce::StringArray& args)
{
    int expected = -1;
    juce::String treeOut;
    for (int i = 2; i < args.size(); ++i)
    {
        const bool hasValue = i + 1 < args.size();
        if (args[i] == "--expect" && hasValue) expected = args[++i].getIntValue();
        else if (args[i] == "--tree" && hasValue) treeOut = args[++i];
        else { std::cerr << "unknown or incomplete option: " << args[i] << "\n"; return 2; }
    }
    std::unique_ptr<juce::AudioProcessor> processor (createPluginFilter());
    const auto* taps = pangeaMeterTaps (*processor);
    if (taps == nullptr) { std::cout << "meter-check: FAIL  the processor has no taps" << std::endl; return 1; }
    int failures = 0;
    auto fail = [&failures] (const juce::String& what)
    {
        std::cout << "FAIL  " << what << std::endl;
        ++failures;
    };
    std::cout << "meter-check: " << processor->getName() << " (" << (taps->kind().isEmpty() ? juce::String ("midi") : taps->kind())
              << ") taps " << (int) taps->size() << " bar graphs" << std::endl;
    juce::StringArray tapped;
    for (size_t i = 0; i < taps->size(); ++i)
    {
        tapped.add (taps->address (i));
        std::cout << "  " << taps->address (i) << std::endl;
    }
    if (expected >= 0 && (int) taps->size() != expected)
        fail ("it taps " + juce::String ((int) taps->size()) + ", not " + juce::String (expected));

    // An instrument's voice, the first stage of the tree, has no tapped address.
    if (taps->kind() == "instrument" && taps->soundTree().size() > 0)
    {
        juce::StringArray voice;
        std::function<void (const juce::var&)> walk = [&] (const juce::var& item)
        {
            if (const auto address = item.getProperty ("address", {}).toString(); address.isNotEmpty())
                voice.add (address);
            if (auto* items = item.getProperty ("items", {}).getArray())
                for (const auto& child : *items)
                    walk (child);
        };
        walk (taps->soundTree()[0]);
        for (const auto& a : tapped)
            if (voice.contains (a))
                fail ("the voice's " + a + " is tapped");
    }

    if (treeOut.isNotEmpty())
    {
        juce::DynamicObject::Ptr page = new juce::DynamicObject();
        page->setProperty ("kind", taps->kind());
        page->setProperty ("tree", taps->soundTree());
        const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (treeOut);
        if (! file.replaceWithText (juce::JSON::toString (juce::var (page.get()))))
            fail ("could not write " + file.getFullPathName());
    }

    const double sampleRate = 48000.0;
    const int blockSize = 512;
    const auto before = taps->blockCount();
    run (*processor, sampleRate, blockSize, 1.0, nullptr);
    const auto blocks = (int) (taps->blockCount() - before);
    const int wanted = (int) (sampleRate * 1.0) / blockSize;
    if (blocks < wanted || blocks > wanted + 1)
        fail ("it counted " + juce::String (blocks) + " blocks for about " + juce::String (wanted));
    for (size_t i = 0; i < taps->size(); ++i)
    {
        std::cout << "  " << taps->address (i) << " = " << taps->value (i) << std::endl;
        if (std::isnan (taps->value (i)))
            fail (taps->address (i) + " is not a number");
    }
    std::cout << (failures == 0 ? juce::String ("meter-check: passed") : "meter-check: " + juce::String (failures) + " failed") << std::endl;
    return failures == 0 ? 0 : 1;
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    juce::StringArray args (argv, argc);
    if (args.size() < 2) { std::cerr << "usage: pangea-render render|check-vst3|editor|tempo|transport|keys|effect-check|bypass-check|sidechain-check|describe-vst3|meter-check|notes|host-check|stop-check ...\n"; return 2; }
    if (args[1] == "meter-check") return meterCheckCommand (args);
    if (args[1] == "render") return renderCommand (args);
    if (args[1] == "check-vst3") return checkVst3Command (args);
    if (args[1] == "editor") return editorCommand (args);
    if (args[1] == "tempo") return tempoCommand (args);
    if (args[1] == "transport") return transportCommand();
    if (args[1] == "keys") return keysCommand();
    if (args[1] == "effect-check") return effectCheckCommand (args);
    if (args[1] == "notes") return pangea::render::notesCommand (args);
    if (args[1] == "host-check") return pangea::render::hostCheckCommand (args);
    if (args[1] == "stop-check") return pangea::render::stopCheckCommand (args);
    if (args[1] == "bypass-check") return pangea::render::bypassCheckCommand (args);
    if (args[1] == "sidechain-check") return pangea::render::sidechainCheckCommand (args);
    if (args[1] == "describe-vst3") return pangea::render::describeVst3Command (args);
    std::cerr << "unknown command: " << args[1] << "\n";
    return 2;
}
