// pangea-render's check of Pangea's bypass in a built plugin (platform/PangeaBypass.h; decision 75).
//
//   pangea-render bypass-check [--vst3 <plugin.vst3>]
//       Every plugin has one bypass, a parameter Pangea adds to it. This plays the build's own
//       processor and, with --vst3, the built plugin loaded as a DAW loads it, with the bypass on
//       before the first block and switched in the middle of a render. It exits 1 unless:
//         - the parameter is pangea_bypass, named Bypass, off, the last parameter, with a version
//           hint above every other's, and the one getBypassParameter answers; and in the VST3 it
//           is the parameter the host is told is the bypass, under the plugin's own id;
//         - an effect, bypassed, gives back its input to the bit: in stereo, with a mono input on
//           a stereo output (both sides), in mono, and with a stereo input on a mono output (the
//           mean of its sides). Switched in the middle, it is its own render up to the switch,
//           crosses to the input over 30 ms with no step larger than the fade allows, and crosses
//           back to the render it would have made, because its code ran on unheard;
//         - an instrument fades to silence over 30 ms, has no voice holding a note once the fade
//           is over, takes no new notes while bypassed, and plays the next note after. Taken off
//           again inside the fade, its notes play on;
//         - a MIDI tool ends its own notes where the bypass begins and starts none, passes the
//           notes and controllers it is sent on their samples, ends those notes where the bypass
//           ends, and plays on;
//         - a logic module in an effect or an instrument is run as if the transport had stopped;
//         - processBlockBypassed does what the parameter does;
//         - the bypass is saved with the plugin's state, and a state saved before the plugin had
//           a bypass opens with the plugin in;
//         - the natively drawn panel has the switch: a press on it moves the parameter, a change
//           of the parameter moves it, and the controls are drawn fainter while bypassed.
#include "RenderHooks.h"
#include "PangeaBypass.h"
#include "../BypassHold.h"

#include <functional>
#include <iostream>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace pangea::render
{

namespace
{

using pangea::bypasscheck::Held;
using pangea::bypasscheck::kMixed;

constexpr double kRate = 48000.0;
constexpr int kBlock = 256;

int failures = 0;

void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "ok    " : "FAIL  ") << what << std::endl;
    failures += ok ? 0 : 1;
}

void say (const juce::String& what) { std::cout << "note  " << what << std::endl; }

/** A host's transport: 120 BPM from bar one, playing unless told otherwise. */
struct Head : public juce::AudioPlayHead
{
    juce::int64 samples = 0;
    bool playing = true;

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm (120.0);
        info.setTimeInSamples (samples);
        info.setTimeInSeconds ((double) samples / kRate);
        info.setPpqPosition ((double) samples * 120.0 / (60.0 * kRate));
        info.setIsPlaying (playing);
        info.setTimeSignature (TimeSignature { 4, 4 });
        return info;
    }
};

/** One MIDI message out of the plugin, at the sample of the whole run it fell on. */
struct Sent
{
    juce::int64 sample = 0;
    juce::MidiMessage message;
};

/** What happens while a take is played. Every part may be left empty. */
struct Script
{
    std::function<void (int block)> before;           // before each block: where the bypass is moved
    std::function<void (int block)> after;            // after each block: where the voices are counted
    std::function<bool (int block)> bypassedCall;     // true: the block goes to processBlockBypassed
    std::function<bool (int block)> stopped;          // true: the transport is stopped for the block
    juce::MidiMessageSequence midi;                   // what the host sends, by sample of the whole run
};

/** What one run gave: the input it was fed, the output, and the MIDI that came out. */
struct Take
{
    std::vector<std::vector<float>> in, out;
    std::vector<Sent> sent;
    std::vector<double> blockSeconds;   // how long the processor took over each block
    bool finite = true;

    size_t length() const { return out.empty() ? 0 : out.front().size(); }

    /** The usual time the processor took over one block, in microseconds, among blocks [from, to). */
    double usualMicros (int from, int to) const
    {
        std::vector<double> times (blockSeconds.begin() + juce::jlimit (0, (int) blockSeconds.size(), from),
                                   blockSeconds.begin() + juce::jlimit (0, (int) blockSeconds.size(), to));
        if (times.empty())
            return 0.0;
        std::sort (times.begin(), times.end());
        return times[times.size() / 2] * 1.0e6;
    }
};

/** Plays `blocks` blocks through a processor that has its layout already. */
Take play (juce::AudioProcessor& p, int blocks, const Script& script = {})
{
    const int ins = p.getTotalNumInputChannels(), outs = p.getTotalNumOutputChannels();
    const int channels = juce::jmax (1, ins, outs);
    Take take;
    take.in.resize ((size_t) ins);
    take.out.resize ((size_t) outs);
    Head head;
    p.setPlayHead (&head);
    p.setRateAndBufferSizeDetails (kRate, kBlock);
    p.prepareToPlay (kRate, kBlock);
    juce::AudioBuffer<float> buffer (channels, kBlock);
    juce::MidiBuffer midi;
    int nextEvent = 0;
    for (int b = 0; b < blocks; ++b)
    {
        const juce::int64 start = (juce::int64) b * kBlock;
        head.samples = start;
        head.playing = ! (script.stopped && script.stopped (b));
        buffer.clear();
        for (int ch = 0; ch < ins; ++ch)
        {
            auto* samples = buffer.getWritePointer (ch);
            for (int i = 0; i < kBlock; ++i)
                samples[i] = pangea::bypasscheck::inputAt (ch, start + i, kRate);
            take.in[(size_t) ch].insert (take.in[(size_t) ch].end(), samples, samples + kBlock);
        }
        midi.clear();
        for (; nextEvent < script.midi.getNumEvents(); ++nextEvent)
        {
            const auto& m = script.midi.getEventPointer (nextEvent)->message;
            const auto at = (juce::int64) m.getTimeStamp();
            if (at >= start + kBlock)
                break;
            midi.addEvent (m, (int) juce::jmax ((juce::int64) 0, at - start));
        }
        if (script.before)
            script.before (b);
        const auto began = juce::Time::getHighResolutionTicks();
        if (script.bypassedCall && script.bypassedCall (b))
            p.processBlockBypassed (buffer, midi);
        else
            p.processBlock (buffer, midi);
        take.blockSeconds.push_back (juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - began));
        for (int ch = 0; ch < outs; ++ch)
        {
            const auto* samples = buffer.getReadPointer (ch);
            for (int i = 0; i < kBlock; ++i)
                take.finite = take.finite && std::isfinite (samples[i]);
            take.out[(size_t) ch].insert (take.out[(size_t) ch].end(), samples, samples + kBlock);
        }
        for (const auto metadata : midi)
            take.sent.push_back ({ start + metadata.samplePosition, metadata.getMessage() });
        if (script.after)
            script.after (b);
    }
    p.releaseResources();
    p.setPlayHead (nullptr);
    return take;
}

using Make = std::function<std::unique_ptr<juce::AudioProcessor>()>;

void setBypass (juce::AudioProcessor& p, bool on)
{
    if (auto* bypass = p.getBypassParameter())
        bypass->setValueNotifyingHost (on ? 1.0f : 0.0f);
}

/** A script that moves the bypass parameter before the blocks named: { block, on }. */
Script switching (juce::AudioProcessor& p, std::vector<std::pair<int, bool>> switches)
{
    Script script;
    auto* processor = &p;
    script.before = [processor, switches] (int block)
    {
        for (const auto& s : switches)
            if (s.first == block)
                setBypass (*processor, s.second);
    };
    return script;
}

/** The gain the plugin's fader gives each sample of a run with those switches (../BypassHold.h). */
std::vector<float> gainsFor (int blocks, bool fromStart, const std::vector<std::pair<int, bool>>& switches)
{
    return pangea::bypasscheck::gainsFor (kRate, kBlock, blocks, fromStart, switches);
}

/** One channel of a take against the plugin's own render and the dry sound (../BypassHold.h). */
Held holdTo (const std::vector<float>& out, const std::vector<float>& usual, const std::vector<float>& dry,
             const std::vector<float>& gains, bool wetKnown = true)
{
    return pangea::bypasscheck::holdTo (out, usual, dry, gains, kRate, wetKnown);
}

juce::String level (float x) { return juce::String (x, 7); }

float peak (const std::vector<float>& v, size_t from, size_t to)
{
    float p = 0.0f;
    for (size_t i = from; i < juce::jmin (to, v.size()); ++i)
        p = juce::jmax (p, std::abs (v[i]));
    return p;
}

bool sameSamples (const Take& a, const Take& b)
{
    return a.out == b.out;
}

/** Gives a processor the layout of a track with those channels. False when it will not take it. A
    sidechain input, when the plugin has one (docs/sidechain.md), is switched off, as on a
    track that routes nothing to it. */
bool layOut (juce::AudioProcessor& p, int ins, int outs)
{
    auto set = [] (int channels) { return channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo(); };
    juce::AudioProcessor::BusesLayout layout;
    if (ins > 0)
        layout.inputBuses.add (set (ins));
    for (int bus = 1; bus < p.getBusCount (true); ++bus)
        layout.inputBuses.add (juce::AudioChannelSet::disabled());
    layout.outputBuses.add (set (outs));
    return p.setBusesLayout (layout) && p.getTotalNumInputChannels() == ins && p.getTotalNumOutputChannels() == outs;
}

// ---- The parameter -----------------------------------------------------------------------------

/** The build's own processor: the parameter as the plugin declares it. Answers how many other
    parameters are named Bypass (a control of the project's own). */
int parameterChecks()
{
    std::unique_ptr<juce::AudioProcessor> p (createPluginFilter());
    auto* bypass = dynamic_cast<juce::RangedAudioParameter*> (p->getBypassParameter());
    check (bypass != nullptr && bypass->getParameterID() == pangea::bypass::parameterId && bypass->getName (64) == pangea::bypass::parameterName,
           "the plugin answers getBypassParameter with a parameter of its own: "
               + (bypass != nullptr ? bypass->getParameterID() + ", named " + bypass->getName (64) : juce::String ("none")));
    if (bypass == nullptr)
        return 0;
    int withId = 0, highestOther = 0, othersNamedBypass = 0;
    for (auto* param : p->getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
        {
            if (ranged->getParameterID() == pangea::bypass::parameterId)
                ++withId;
            if (ranged != bypass)
            {
                highestOther = juce::jmax (highestOther, ranged->getVersionHint());
                othersNamedBypass += ranged->getName (64) == pangea::bypass::parameterName ? 1 : 0;
            }
        }
    check (withId == 1 && p->getParameters().getLast() == bypass,
           "it is one parameter, the last of the plugin's " + juce::String (p->getParameters().size()));
    check (bypass->getVersionHint() == pangea::bypass::versionHint && bypass->getVersionHint() > highestOther,
           "in an Audio Unit's list it comes after every other parameter (version hint " + juce::String (bypass->getVersionHint())
               + ", the others at most " + juce::String (highestOther) + ")");
    check (bypass->isBoolean() && bypass->getDefaultValue() < 0.5f && bypass->getValue() < 0.5f, "it is a switch, and a new plugin is not bypassed");
    if (othersNamedBypass > 0)
        say ("this project has " + juce::String (othersNamedBypass) + " control of its own named Bypass, so a host shows more than one parameter of that name");
    return othersNamedBypass;
}

/** The built VST3: the host is told which parameter is the bypass, and it is the plugin's own. */
void hostedParameterChecks (juce::AudioProcessor& plugin, int othersNamedBypass)
{
    auto* bypass = plugin.getBypassParameter();
    auto* hosted = dynamic_cast<juce::HostedAudioProcessorParameter*> (bypass);
    // A VST3 parameter's id is a hash of the plugin's id for it. JUCE's wrapper, left to add a
    // bypass of its own, gives that one the fixed id 'byps'.
    const auto own = juce::String (juce::VST3ClientExtensions::convertJuceParameterId (pangea::bypass::parameterId));
    const auto jucesOwn = juce::String ((juce::uint32) 0x62797073);
    check (bypass != nullptr && bypass->getName (64) == pangea::bypass::parameterName && hosted != nullptr && hosted->getParameterID() == own
               && hosted->getParameterID() != jucesOwn,
           "the VST3 tells the host that its bypass is the plugin's own parameter (id "
               + (hosted != nullptr ? hosted->getParameterID() : juce::String ("none")) + ", the hash of pangea_bypass; JUCE's own would be " + jucesOwn + ")");
    int named = 0;
    for (auto* param : plugin.getParameters())
        named += param->getName (64) == pangea::bypass::parameterName ? 1 : 0;
    check (named == 1 + othersNamedBypass, "the host sees " + juce::String (named) + " parameter named Bypass"
                                               + (othersNamedBypass > 0 ? juce::String (", one of them Pangea's") : juce::String()));
}

// ---- The panel ---------------------------------------------------------------------------------

/** The panel a built plugin opens: its switch and the plugin's parameter move each other, and
    the controls are drawn fainter while the plugin is bypassed. */
void panelChecks()
{
    if (pangeaHasWebPanel())
    {
        say ("this build's panel is the component library in a web view: its bypass button is checked in the Pangea plugin (build-host.sh --check)");
        return;
    }
    std::unique_ptr<juce::AudioProcessor> p (createPluginFilter());
    std::unique_ptr<juce::AudioProcessorEditor> editor (p->createEditorIfNeeded());
    auto* bypass = p->getBypassParameter();
    auto panel = editor != nullptr ? pangeaPanelBypass (*editor) : PanelBypass();
    check (panel.button != nullptr && bypass != nullptr && ! panel.button->getToggleState() && ! panel.controlsFainter,
           "the panel has a bypass switch, off, with the controls drawn in full");
    if (panel.button == nullptr || bypass == nullptr)
        return;
    // A press, as the switch tells its listeners of one.
    panel.button->setToggleState (true, juce::sendNotificationSync);
    panel = pangeaPanelBypass (*editor);
    check (bypass->getValue() >= 0.5f && panel.controlsFainter, "a press on the switch moves the plugin's bypass parameter, and the controls are drawn fainter");
    // The parameter moved from outside: automation, or a host's own bypass.
    bypass->setValueNotifyingHost (0.0f);
    panel = pangeaPanelBypass (*editor);
    check (! panel.button->getToggleState() && ! panel.controlsFainter, "a change of the parameter from outside moves the switch back, and the controls are drawn in full again");
    bypass->setValueNotifyingHost (1.0f);
    panel = pangeaPanelBypass (*editor);
    check (panel.button->getToggleState() && panel.controlsFainter, "and the other way: bypassed from outside, the switch is on");
    editor.reset();
}

// ---- An effect ---------------------------------------------------------------------------------

constexpr int kEffectBlocks = 400;                 // a little over two seconds
constexpr int kSwitchOn = 150, kSwitchOff = 280;   // the blocks before which the bypass is moved

void effectChecks (const Make& make, const juce::String& what, bool inProcess)
{
    const bool wetKnown = ! pangeaHasLogicModule();
    const int fade = juce::roundToInt (kRate * pangea::bypass::fadeSeconds);

    auto usualPlugin = make();
    const int ins = usualPlugin->getTotalNumInputChannels(), outs = usualPlugin->getTotalNumOutputChannels();
    const auto usual = play (*usualPlugin, kEffectBlocks);
    check (usual.finite && outs > 0, what + ": " + juce::String (ins) + " in, " + juce::String (outs) + " out, its own render has peak "
                                         + level (peak (usual.out[0], 0, usual.length())));
    // What a bypassed effect gives on output channel `ch`, for a take's own input.
    auto dryOf = [] (const Take& take, int ch) -> std::vector<float>
    {
        if (take.in.empty())
            return {};
        if (take.out.size() == 1 && take.in.size() >= 2)
        {
            std::vector<float> mean (take.in[0].size());
            for (size_t s = 0; s < mean.size(); ++s)
                mean[s] = (take.in[0][s] + take.in[1][s]) * 0.5f;
            return mean;
        }
        return take.in[(size_t) juce::jmin (ch, (int) take.in.size() - 1)];
    };

    // 1. Bypassed before the first block: the input, to the bit, from the first sample.
    {
        auto plugin = make();
        setBypass (*plugin, true);
        const auto take = play (*plugin, kEffectBlocks);
        float apart = 0.0f;
        for (int ch = 0; ch < outs; ++ch)
        {
            const auto dry = dryOf (take, ch);
            for (size_t s = 0; s < take.length(); ++s)
                apart = juce::jmax (apart, std::abs (take.out[(size_t) ch][s] - (dry.empty() ? 0.0f : dry[s])));
        }
        check (take.finite && apart == 0.0f, what + ": bypassed from the start, it gives back its input on " + juce::String (outs)
                                                 + (outs == 1 ? " channel" : " channels") + " to the bit (furthest sample " + level (apart) + ")");
        // What the bypass costs: the plugin's code runs on unheard, so a bypassed block takes what a
        // block takes with the plugin in. Measured, not held: a timing is no pass or fail.
        if (inProcess)
            say (what + ": its code runs on while bypassed: a block of " + juce::String (kBlock) + " samples takes "
                     + juce::String (take.usualMicros (50, kEffectBlocks), 1) + " microseconds of processor time bypassed and "
                     + juce::String (usual.usualMicros (50, kEffectBlocks), 1) + " with the plugin in (the block lasts "
                     + juce::String (juce::roundToInt (kBlock * 1.0e6 / kRate)) + " microseconds)");
    }

    // 2. Switched in the middle: on before one block, off before a later one.
    Take switched;
    {
        auto plugin = make();
        const std::vector<std::pair<int, bool>> switches { { kSwitchOn, true }, { kSwitchOff, false } };
        switched = play (*plugin, kEffectBlocks, switching (*plugin, switches));
        const auto gains = gainsFor (kEffectBlocks, false, switches);
        Held worst;
        for (int ch = 0; ch < outs; ++ch)
        {
            const auto h = holdTo (switched.out[(size_t) ch], usual.out[(size_t) ch], dryOf (switched, ch), gains, wetKnown);
            worst.worse (h);
        }
        const auto bypassedFor = (size_t) ((kSwitchOff - kSwitchOn) * kBlock - fade + 1);
        if (wetKnown)
        {
            check (switched.finite && worst.apartAtRest == 0.0f && worst.bypassedSamples == bypassedFor,
                   what + ": switched in the middle, it is its own render to the bit before the bypass and after it, tails and all, and its input between ("
                       + juce::String ((int) worst.bypassedSamples) + " samples)");
            check (worst.fadeSamples == (size_t) (2 * (fade - 1)) && worst.apartInFades <= kMixed,
                   what + ": each switch is a fade of " + juce::String (fade) + " samples from one to the other (furthest sample from the mix " + level (worst.apartInFades) + ")");
            check (worst.overAllowed <= 0.0f, what + ": no step inside a fade is larger than the fade allows: the largest is " + level (worst.largestStep)
                                                  + ", where the two sounds' own largest there is " + level (worst.ownStep));
        }
        else
        {
            check (switched.finite && worst.apartAtRest == 0.0f && worst.bypassedSamples == bypassedFor,
                   what + ": switched in the middle, it is its own render to the bit before the bypass, and its input while bypassed ("
                       + juce::String ((int) worst.bypassedSamples) + " samples)");
            say (what + ": it carries a logic module, which is run as stopped while bypassed, so its sound through the fades is not held to its usual render");
        }
    }

    // 3. A host that calls processBlockBypassed instead: the same, sample for sample.
    if (inProcess)
    {
        auto plugin = make();
        Script script;
        script.bypassedCall = [] (int block) { return block >= kSwitchOn && block < kSwitchOff; };
        const auto take = play (*plugin, kEffectBlocks, script);
        check (sameSamples (take, switched), what + ": processBlockBypassed does what the parameter does, sample for sample");
    }

    // 4. Other tracks: a mono input on a stereo output, mono, and a stereo input on a mono output.
    for (const auto& shape : { std::pair<int, int> { 1, 2 }, std::pair<int, int> { 1, 1 }, std::pair<int, int> { 2, 1 } })
    {
        if (ins == 0)
            break;
        const auto name = juce::String (shape.first == 1 ? "a mono input" : "a stereo input") + " on a " + (shape.second == 1 ? "mono" : "stereo") + " output";
        auto plain = make(), bypassed = make(), moved = make();
        if (! layOut (*plain, shape.first, shape.second) || ! layOut (*bypassed, shape.first, shape.second) || ! layOut (*moved, shape.first, shape.second))
        {
            say (what + ": it does not take " + name);
            continue;
        }
        const auto own = play (*plain, kEffectBlocks);
        setBypass (*bypassed, true);
        const auto through = play (*bypassed, kEffectBlocks);
        const std::vector<std::pair<int, bool>> switches { { kSwitchOn, true } };
        const auto half = play (*moved, kEffectBlocks, switching (*moved, switches));
        const auto gains = gainsFor (kEffectBlocks, false, switches);
        float apart = 0.0f, apartHalf = 0.0f, over = 0.0f;
        for (int ch = 0; ch < shape.second; ++ch)
        {
            const auto dry = dryOf (through, ch);
            for (size_t s = 0; s < through.length(); ++s)
                apart = juce::jmax (apart, std::abs (through.out[(size_t) ch][s] - dry[s]));
            const auto h = holdTo (half.out[(size_t) ch], own.out[(size_t) ch], dryOf (half, ch), gains, wetKnown);
            apartHalf = juce::jmax (apartHalf, h.apartAtRest, wetKnown ? h.apartInFades - kMixed : 0.0f);
            over = juce::jmax (over, wetKnown ? h.overAllowed : 0.0f);
        }
        check (through.finite && apart == 0.0f, what + ": " + name + ", bypassed, gives "
                                                    + (shape.first == 1 && shape.second == 2 ? "the input on both sides"
                                                       : (shape.first == 2 && shape.second == 1 ? "the mean of its two sides" : "the input"))
                                                    + " to the bit (furthest sample " + level (apart) + ")");
        check (half.finite && apartHalf <= 0.0f && over <= 0.0f, what + ": " + name + ", switched in the middle, fades from its own render to that with no step the fade does not allow");
    }

    // 5. The bypass is saved with the plugin, and a plugin opened from that state starts bypassed.
    {
        auto plugin = make();
        setBypass (*plugin, true);
        play (*plugin, 4);   // a hosted plugin takes a parameter with the next block
        juce::MemoryBlock state;
        plugin->getStateInformation (state);
        auto again = make();
        again->setStateInformation (state.getData(), (int) state.getSize());
        const auto take = play (*again, 40);
        float apart = 0.0f;
        for (int ch = 0; ch < outs; ++ch)
        {
            const auto dry = dryOf (take, ch);
            for (size_t s = 0; s < take.length(); ++s)
                apart = juce::jmax (apart, std::abs (take.out[(size_t) ch][s] - (dry.empty() ? 0.0f : dry[s])));
        }
        check (apart == 0.0f, what + ": saved bypassed, it opens bypassed, and gives its input from the first sample");
    }
}

// ---- An instrument -----------------------------------------------------------------------------

constexpr int kInstrumentBlocks = 480;   // two and a half seconds

/** A chord from a twentieth of a second in, held to the end unless `offAt` says when it is let go. */
void addChord (juce::MidiMessageSequence& midi, int notes, int offAtBlock = -1)
{
    const int pitches[] = { 60, 64, 67 };
    for (int i = 0; i < notes && i < 3; ++i)
    {
        midi.addEvent (juce::MidiMessage::noteOn (1, pitches[i], (juce::uint8) 100), 0.05 * kRate);
        if (offAtBlock >= 0)
            midi.addEvent (juce::MidiMessage::noteOff (1, pitches[i]), (double) offAtBlock * kBlock);
    }
}

void instrumentChecks (const Make& make, const juce::String& what, bool inProcess)
{
    const bool wetKnown = ! pangeaHasLogicModule();
    const int fade = juce::roundToInt (kRate * pangea::bypass::fadeSeconds);
    const auto shape = [] { std::unique_ptr<juce::AudioProcessor> p (createPluginFilter()); return pangeaSounding (*p); }();
    const int chord = shape.polyphonic ? juce::jmin (3, shape.numVoices) : 0;

    // The same notes with the plugin in throughout.
    Script plain;
    addChord (plain.midi, chord);
    auto usualPlugin = make();
    const int outs = usualPlugin->getTotalNumOutputChannels();
    const auto usual = play (*usualPlugin, kInstrumentBlocks, plain);
    const float usualPeak = outs > 0 ? peak (usual.out[0], 0, usual.length()) : 0.0f;
    check (usual.finite && outs > 0 && usualPeak > 0.001f, what + ": " + (shape.polyphonic ? juce::String (chord) + " notes held, " : juce::String ("played by its logic module, "))
                                                               + "its own render has peak " + level (usualPeak));
    if (outs == 0)
        return;

    // 1. Bypassed in the middle of the held notes, then a note played while bypassed, the keys let
    // go, the bypass taken off and a new note played.
    const int on = 150, playedWhile = 200, releasedWhile = 220, letGo = 230, off = 300, next = 310;
    Take bypassed;
    {
        auto plugin = make();
        const std::vector<std::pair<int, bool>> switches { { on, true }, { off, false } };
        auto script = switching (*plugin, switches);
        addChord (script.midi, chord, letGo);
        script.midi.addEvent (juce::MidiMessage::noteOn (1, 72, (juce::uint8) 110), (double) playedWhile * kBlock);
        script.midi.addEvent (juce::MidiMessage::noteOff (1, 72), (double) releasedWhile * kBlock);
        script.midi.addEvent (juce::MidiMessage::noteOn (1, 62, (juce::uint8) 110), (double) next * kBlock);
        script.midi.sort();
        Sounding beforeBypass, afterFade, whilePlayed, atReturn, afterNext, whileModule;
        auto* raw = plugin.get();
        script.after = [&, raw] (int block)
        {
            if (! inProcess)
                return;
            if (block == on - 1) beforeBypass = pangeaSounding (*raw);
            if (block == on + fade / kBlock + 2) afterFade = pangeaSounding (*raw);
            if (block == playedWhile + 5) whilePlayed = pangeaSounding (*raw);
            if (block == off - 1) atReturn = pangeaSounding (*raw);
            if (block == off - 1) whileModule = pangeaSounding (*raw);
            if (block == next + 5) afterNext = pangeaSounding (*raw);
        };
        bypassed = play (*plugin, kInstrumentBlocks, script);
        const auto gains = gainsFor (kInstrumentBlocks, false, switches);
        Held worst;
        for (int ch = 0; ch < outs; ++ch)
        {
            // Up to the bypass the notes are the usual render's. Once it comes off they are not (the
            // keys were let go, and another note is played), so the take is held only as far as that.
            const auto untilOff = (std::ptrdiff_t) off * kBlock;
            const auto h = holdTo (std::vector<float> (bypassed.out[(size_t) ch].begin(), bypassed.out[(size_t) ch].begin() + untilOff),
                                   usual.out[(size_t) ch], {}, std::vector<float> (gains.begin(), gains.begin() + untilOff), wetKnown);
            worst.worse (h);
        }
        const auto silentFor = (size_t) ((off - on) * kBlock - fade + 1);
        check (bypassed.finite && worst.apartAtRest == 0.0f && worst.bypassedSamples == silentFor,
               what + ": bypassed in the middle of its notes, it is its own render to the bit up to the bypass and silent after the fade ("
                   + juce::String ((int) worst.bypassedSamples) + " samples of nothing, a note played meanwhile included)");
        if (wetKnown)
        {
            check (worst.fadeSamples == (size_t) (fade - 1) && worst.apartInFades <= kMixed,
                   what + ": the way to silence is a fade of " + juce::String (fade) + " samples (furthest sample from it " + level (worst.apartInFades) + ")");
            check (worst.overAllowed <= 0.0f, what + ": no step inside the fade is larger than the fade allows: the largest is " + level (worst.largestStep)
                                                  + ", where its own largest there is " + level (worst.ownStep));
        }
        else
            say (what + ": it carries a logic module, which is run as stopped while bypassed, so its sound through the fade is not held to its usual render");
        const size_t from = (size_t) next * kBlock;
        const float after = peak (bypassed.out[0], from, bypassed.length());
        check (after > 0.001f, what + ": with the bypass off again " + (shape.polyphonic ? "the next note plays" : "it plays again") + ": peak " + level (after));
        if (inProcess && shape.polyphonic)
        {
            // A logic module that plays the voices adds its own notes to the ones played here.
            const bool module = pangeaHasLogicModule();
            check ((module ? beforeBypass.voicesHolding >= chord : beforeBypass.voicesHolding == chord)
                       && afterFade.voicesHolding == 0 && afterFade.logicNotes == 0,
                   what + ": " + juce::String (beforeBypass.voicesHolding) + " voices hold notes before the bypass, and none once its fade is over"
                       + (module ? juce::String (", its module's notes ended with the rest") : juce::String()));
            check (whilePlayed.voicesHolding == 0, what + ": a note played while it is bypassed starts no voice"
                                                       + (module ? juce::String (", and its module starts none") : juce::String()));
            check (module ? afterNext.voicesHolding >= 1 : afterNext.voicesHolding == 1,
                   what + ": after it, the note played next has a voice" + (module ? juce::String (", beside its module's: ") : juce::String (", and only that one: "))
                       + juce::String (afterNext.voicesHolding));
            if (atReturn.voicesBusy > 0)
                say (what + ": " + juce::String (atReturn.voicesBusy) + " of its voices were still ringing out unheard when the bypass came off, "
                         + juce::String ((off - on) * kBlock / kRate, 2) + " s after it began");
            else
                check (true, what + ": by the time the bypass comes off every voice has rung out and is free");
            // What the bypass costs: the ended voices ring out and are then not run, as after any
            // note. Measured over the last blocks before the bypass comes off, not held.
            say (what + ": bypassed, a block of " + juce::String (kBlock) + " samples takes " + juce::String (bypassed.usualMicros (off - 40, off), 1)
                     + " microseconds of processor time, and " + juce::String (usual.usualMicros (off - 40, off), 1) + " with the plugin in and its "
                     + juce::String (chord) + " notes held (the block lasts " + juce::String (juce::roundToInt (kBlock * 1.0e6 / kRate)) + " microseconds)");
        }
        if (inProcess && pangeaHasLogicModule())
            check (! whileModule.modulePlaying && whileModule.logicNotes == 0 && afterNext.modulePlaying,
                   what + ": its logic module is run as stopped while bypassed, with none of its notes left on, and as playing after");
    }

    // 2. Taken off again inside the fade: the notes were never ended, so they play on.
    if (wetKnown && shape.polyphonic)
    {
        auto plugin = make();
        const std::vector<std::pair<int, bool>> switches { { on, true }, { on + 2, false } };
        auto script = switching (*plugin, switches);
        addChord (script.midi, chord);
        Sounding after;
        auto* raw = plugin.get();
        script.after = [&, raw] (int block) { if (inProcess && block == on + 12) after = pangeaSounding (*raw); };
        const auto take = play (*plugin, kInstrumentBlocks, script);
        const auto gains = gainsFor (kInstrumentBlocks, false, switches);
        Held worst;
        for (int ch = 0; ch < outs; ++ch)
        {
            const auto h = holdTo (take.out[(size_t) ch], usual.out[(size_t) ch], {}, gains);
            worst.worse (h);
        }
        check (take.finite && worst.apartAtRest == 0.0f && worst.apartInFades <= kMixed && worst.overAllowed <= 0.0f && worst.bypassedSamples == 0
                   && worst.fadeSamples == (size_t) (4 * kBlock - 1) && (! inProcess || after.voicesHolding == chord),
               what + ": taken off again inside the fade, it dips and comes back with no step the fade does not allow, and its notes play on"
                   + (inProcess ? " (" + juce::String (after.voicesHolding) + " voices still hold them)" : juce::String()));
    }

    // 3. A host that calls processBlockBypassed instead: the same, sample for sample.
    if (inProcess)
    {
        auto plugin = make();
        Script script;
        addChord (script.midi, chord, letGo);
        script.midi.addEvent (juce::MidiMessage::noteOn (1, 72, (juce::uint8) 110), (double) playedWhile * kBlock);
        script.midi.addEvent (juce::MidiMessage::noteOff (1, 72), (double) releasedWhile * kBlock);
        script.midi.addEvent (juce::MidiMessage::noteOn (1, 62, (juce::uint8) 110), (double) next * kBlock);
        script.midi.sort();
        script.bypassedCall = [on, off] (int block) { return block >= on && block < off; };
        const auto take = play (*plugin, kInstrumentBlocks, script);
        check (sameSamples (take, bypassed), what + ": processBlockBypassed does what the parameter does, sample for sample");
    }

    // 4. Saved bypassed, it opens bypassed: silent from the first sample, whatever is played.
    {
        auto plugin = make();
        setBypass (*plugin, true);
        play (*plugin, 4);
        juce::MemoryBlock state;
        plugin->getStateInformation (state);
        auto again = make();
        again->setStateInformation (state.getData(), (int) state.getSize());
        const auto take = play (*again, 120, plain);
        float loudest = 0.0f;
        for (int ch = 0; ch < outs; ++ch)
            loudest = juce::jmax (loudest, peak (take.out[(size_t) ch], 0, take.length()));
        check (loudest == 0.0f, what + ": saved bypassed, it opens bypassed, and is silent from the first sample");
    }
}

// ---- A MIDI tool -------------------------------------------------------------------------------

constexpr int kToolBlocks = 1500;   // four bars at 120 BPM
constexpr int kToolStopped = 3;     // then the transport stops, so every note can end

using pangea::bypasscheck::Message;

/** What a take sent, as the shared check reads it (../BypassHold.h). */
std::vector<Message> messagesOf (const Take& take)
{
    std::vector<Message> out;
    for (const auto& s : take.sent)
    {
        const auto& m = s.message;
        if (m.isNoteOn()) out.push_back ({ s.sample, Message::Kind::noteOn, m.getChannel(), m.getNoteNumber(), m.getVelocity() });
        else if (m.isNoteOff()) out.push_back ({ s.sample, Message::Kind::noteOff, m.getChannel(), m.getNoteNumber(), 0 });
        else if (m.isController()) out.push_back ({ s.sample, Message::Kind::controller, m.getChannel(), m.getControllerNumber(), m.getControllerValue() });
        else out.push_back ({ s.sample, Message::Kind::other, m.getChannel(), 0, 0 });
    }
    return out;
}

void midiToolChecks (const Make& make, const juce::String& what, bool inProcess)
{
    using namespace pangea::bypasscheck;
    Script plain;
    plain.stopped = [] (int block) { return block >= kToolBlocks; };
    auto usualPlugin = make();
    const auto usual = messagesOf (play (*usualPlugin, kToolBlocks + kToolStopped, plain));
    int usualOns = 0;
    for (const auto& m : usual)
        usualOns += m.kind == Message::Kind::noteOn ? 1 : 0;
    check (usualOns > 0, what + ": it plays " + juce::String (usualOns) + " notes of its own in four bars");

    // The bypass begins at the start of a block in the middle of one of the tool's notes, when it has one.
    bool midNote = false;
    const int on = blockInsideANote (usual, kBlock, 400, midNote), off = on + 400;
    const juce::int64 onSample = (juce::int64) on * kBlock, offSample = (juce::int64) off * kBlock;
    const juce::int64 playedAt = onSample + 2 * kBlock + 7, controllerAt = onSample + 3 * kBlock + 11;

    auto played = [&] (Script script)
    {
        script.stopped = plain.stopped;
        script.midi.addEvent (juce::MidiMessage::noteOn (kPlayedChannel, kPlayedNote, (juce::uint8) kPlayedVelocity), (double) playedAt);
        script.midi.addEvent (juce::MidiMessage::controllerEvent (kPlayedChannel, kPlayedController, kPlayedControllerValue), (double) controllerAt);
        script.midi.sort();
        return script;
    };
    auto plugin = make();
    const auto take = messagesOf (play (*plugin, kToolBlocks + kToolStopped, played (switching (*plugin, { { on, true }, { off, false } }))));
    const auto h = holdTool (usual, take, onSample, offSample, playedAt, controllerAt);

    if (! midNote)
        say (what + ": none of its notes was sounding where the bypass began, so that it ends them there is not shown here");
    check (h.sameBefore && h.endedOwn, what + ": up to the bypass it plays as it always does, and the " + juce::String (h.wereOn)
                                           + (h.wereOn == 1 ? " note" : " notes") + " it had on then end on the bypass's first sample");
    check (h.ownOnsWhile == 0, what + ": it starts no note of its own while bypassed");
    check (h.passedNote && h.passedController, what + ": a note and a controller sent to it while bypassed come out on their own samples, unchanged");
    check (h.endedPassed, what + ": the note that passed through is ended on the first sample after the bypass");
    check (h.onsAfter > 0 && h.leftOn == 0 && h.strayOffs == 0, what + ": it plays on after the bypass (" + juce::String (h.onsAfter)
                                                                    + " notes), and nothing is left on at the end");
    // Back in time: from the bypass's end on, the notes it starts are the ones it always starts there.
    if (h.backInTime)
        check (true, what + ": it comes back in time: the notes it starts after the bypass are the ones it starts there with no bypass");
    else
        say (what + ": the notes it starts after the bypass are not the ones it starts there with no bypass: a module that keeps its place by itself");

    if (inProcess)
    {
        auto other = make();
        Script script;
        script.bypassedCall = [on, off] (int block) { return block >= on && block < off; };
        const auto again = messagesOf (play (*other, kToolBlocks + kToolStopped, played (script)));
        check (again == take, what + ": processBlockBypassed does what the parameter does, message for message");
    }

    // Saved bypassed, it opens bypassed: none of its own notes, and what it is sent passes.
    {
        auto first = make();
        setBypass (*first, true);
        play (*first, 4);
        juce::MemoryBlock state;
        first->getStateInformation (state);
        auto again = make();
        again->setStateInformation (state.getData(), (int) state.getSize());
        Script script;
        script.midi.addEvent (juce::MidiMessage::noteOn (kPlayedChannel, kPlayedNote, (juce::uint8) kPlayedVelocity), 3.0 * kBlock + 7.0);
        const auto opened = messagesOf (play (*again, 400, script));
        int ons = 0;
        bool passed = false;
        for (const auto& m : opened)
            if (m.kind == Message::Kind::noteOn)
            {
                ++ons;
                passed = passed || (m.sample == 3 * kBlock + 7 && m.channel == kPlayedChannel && m.number == kPlayedNote);
            }
        check (ons == 1 && passed, what + ": saved bypassed, it opens bypassed: it plays nothing of its own and passes what it is sent");
    }
}

// ---- The state, as the plugin itself keeps it ---------------------------------------------------

/** A state saved before the plugin had a bypass says nothing of it: it opens with the plugin in,
    even in a plugin that is bypassed when it arrives. */
void olderStateCheck()
{
    std::unique_ptr<juce::AudioProcessor> saved (createPluginFilter());
    setBypass (*saved, true);
    juce::MemoryBlock state;
    saved->getStateInformation (state);
    auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
    bool hadIt = false;
    if (xml != nullptr)
        for (auto* child = xml->getFirstChildElement(); child != nullptr; child = child->getNextElement())
            if (child->getStringAttribute ("id") == pangea::bypass::parameterId)
            {
                hadIt = child->getDoubleAttribute ("value") >= 0.5;
                xml->removeChildElement (child, true);
                break;
            }
    check (xml != nullptr && hadIt, "the plugin's state holds the bypass beside its other parameters");
    if (xml == nullptr)
        return;
    juce::MemoryBlock older;
    juce::AudioProcessor::copyXmlToBinary (*xml, older);

    std::unique_ptr<juce::AudioProcessor> fresh (createPluginFilter()), bypassed (createPluginFilter());
    setBypass (*bypassed, true);
    fresh->setStateInformation (older.getData(), (int) older.getSize());
    bypassed->setStateInformation (older.getData(), (int) older.getSize());
    check (fresh->getBypassParameter()->getValue() < 0.5f && bypassed->getBypassParameter()->getValue() < 0.5f,
           "a state saved before the plugin had a bypass opens with the plugin in, in a new plugin and in one that was bypassed");
    std::unique_ptr<juce::AudioProcessor> again (createPluginFilter());
    again->setStateInformation (state.getData(), (int) state.getSize());
    check (again->getBypassParameter()->getValue() >= 0.5f, "and a state saved bypassed opens bypassed");
}

} // namespace

int bypassCheckCommand (const juce::StringArray& args)
{
    juce::String vst3Path;
    for (int i = 2; i < args.size(); ++i)
    {
        if (args[i] == "--vst3" && i + 1 < args.size()) vst3Path = args[++i];
        else { std::cerr << "unknown or incomplete option: " << args[i] << "\n"; return 2; }
    }

    const Make own = [] { return std::unique_ptr<juce::AudioProcessor> (createPluginFilter()); };
    const auto name = own()->getName();
    const auto kind = pangeaIsMidiTool() ? "a MIDI tool" : (pangeaIsInstrument() ? "an instrument" : "an effect");
    std::cout << "bypass-check: " << name << ", " << kind << (pangeaHasLogicModule() && ! pangeaIsMidiTool() ? " with a logic module" : "") << std::endl;

    const int othersNamedBypass = parameterChecks();
    olderStateCheck();
    panelChecks();
    auto run = [] (const Make& make, const juce::String& what, bool inProcess)
    {
        if (pangeaIsMidiTool()) midiToolChecks (make, what, inProcess);
        else if (pangeaIsInstrument()) instrumentChecks (make, what, inProcess);
        else effectChecks (make, what, inProcess);
    };
    run (own, "the processor", true);

    if (vst3Path.isNotEmpty())
    {
        const juce::File bundle = juce::File::getCurrentWorkingDirectory().getChildFile (vst3Path);
        juce::VST3PluginFormat format;
        juce::OwnedArray<juce::PluginDescription> descriptions;
        format.findAllTypesForFile (descriptions, bundle.getFullPathName());
        juce::String error;
        auto first = descriptions.isEmpty() ? nullptr : format.createInstanceFromDescription (*descriptions[0], kRate, kBlock, error);
        check (first != nullptr, "the VST3 loads from " + bundle.getFullPathName() + (error.isEmpty() ? juce::String() : ": " + error));
        if (first != nullptr)
        {
            hostedParameterChecks (*first, othersNamedBypass);
            first.reset();
            const auto description = *descriptions[0];
            const Make hosted = [&format, description]
            {
                juce::String why;
                std::unique_ptr<juce::AudioProcessor> instance (format.createInstanceFromDescription (description, kRate, kBlock, why).release());
                if (instance != nullptr)
                    instance->enableAllBuses();
                return instance;
            };
            run (hosted, "the VST3", false);
        }
    }

    std::cout << (failures == 0 ? juce::String ("bypass-check: passed") : "bypass-check: " + juce::String (failures) + " failed") << std::endl;
    return failures == 0 ? 0 : 1;
}

} // namespace pangea::render
