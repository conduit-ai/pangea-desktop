// pangea-render's check of the sidechain input (docs/sidechain.md).
//
//   pangea-render sidechain-check [--vst3 <plugin.vst3>] [--wav <out.wav>] [--expect-duck <dB>]
//       The plugin's buses, as the build's own processor declares them and, with --vst3, as a DAW
//       loading the built plugin sees them. It exits 1 unless:
//         - an effect whose code declares no sidechain has the buses it always had: one input,
//           Input, stereo and on, and one output, Output, stereo and on (an instrument or a MIDI
//           tool is not looked at: its buses do not change);
//         - an effect that declares one has a second input bus, Sidechain, stereo and off by
//           default, which takes off, mono and stereo and refuses three channels, whatever the
//           main bus is; and in the VST3 it is there, by that name, off by default, and the
//           channels the host is told the plugin has are still two in and two out;
//         - with the sidechain off, as a host that never routes one has it, the plugin gives
//           exactly what it gives with the sidechain on and silent, and that is not silence;
//         - with a kick on every beat in the sidechain, its output changes; the kick routed as
//           mono gives exactly what it gives routed as stereo with the same sound on both sides;
//         - the VST3, with its sidechain switched on as a DAW does when a track is routed to it,
//           gives what the processor gives, kick and all.
//       It plays a held pad at -12 dBFS (two sines) and reports how far the pad dips in the 50 ms
//       after each kick; --expect-duck fails a dip smaller than that. --wav writes the render with
//       the kick, left and right, then the kick itself as a third channel, for listening.
//
//   pangea-render describe-vst3 <plugin.vst3>
//       Loads a VST3 as a DAW does and prints what a host stores of it: its buses, its parameters
//       (index, id, name, default, steps) and its state as a new plugin gives it. Two builds of one
//       project can be compared line by line with it.
#include "RenderHooks.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <iostream>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace pangea::render
{

namespace
{

constexpr double kRate = 48000.0;
constexpr int kBlock = 256;
constexpr double kSeconds = 3.0;
constexpr double kBeat = 0.5;          // a kick every half second: 120 BPM
constexpr double kFirstKick = 0.25;

int failures = 0;

void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "ok    " : "FAIL  ") << what << std::endl;
    failures += ok ? 0 : 1;
}

void say (const juce::String& what) { std::cout << "note  " << what << std::endl; }

/** The pad the plugin ducks: two sines at -12 dBFS together, the same on both sides. */
float padAt (juce::int64 n)
{
    const double t = (double) n / kRate;
    return (float) (0.125 * (std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * t)
                             + std::sin (2.0 * juce::MathConstants<double>::pi * 330.5 * t)));
}

/**
 * A kick on every beat, as the site's kickTrain makes it (src/lib/faust/sidechain.ts): a sine that
 * falls from 150 Hz to 45 Hz over 0.12 s and dies away to a thousandth over 0.4 s.
 */
std::vector<float> kicks (int frames)
{
    std::vector<float> out ((size_t) frames, 0.0f);
    const int length = (int) std::lround (0.42 * kRate);
    for (double start = kFirstKick; start * kRate < frames; start += kBeat)
    {
        const int first = (int) std::lround (start * kRate);
        double phase = 0.0;
        for (int i = 0; i < length && first + i < frames; ++i)
        {
            const double t = i / kRate;
            const double f = t < 0.12 ? 150.0 * std::pow (45.0 / 150.0, t / 0.12) : 45.0;
            phase += 2.0 * juce::MathConstants<double>::pi * f / kRate;
            const double level = 0.7 * std::pow (0.001 / 0.9, juce::jmin (1.0, t / 0.4));
            out[(size_t) (first + i)] += (float) (std::sin (phase) * level);
        }
    }
    return out;
}

/** How the sidechain bus is set for a take, and what it carries. */
enum class Side { off, silent, kickStereo, kickMono };

juce::String describe (const juce::AudioChannelSet& set)
{
    if (set.isDisabled()) return "off";
    if (set == juce::AudioChannelSet::mono()) return "mono";
    if (set == juce::AudioChannelSet::stereo()) return "stereo";
    return juce::String (set.size()) + " channels";
}

juce::String busesOf (juce::AudioProcessor& p, bool isInput)
{
    juce::StringArray parts;
    for (int i = 0; i < p.getBusCount (isInput); ++i)
        if (auto* bus = p.getBus (isInput, i))
            parts.add (bus->getName() + " (" + describe (bus->getDefaultLayout()) + ", "
                       + (bus->isEnabledByDefault() ? "on" : "off") + " by default, now " + describe (bus->getCurrentLayout()) + ")");
    return parts.isEmpty() ? juce::String ("none") : parts.joinIntoString (", ");
}

/** Sets the main buses to stereo and the sidechain bus, when there is one, to `side`. */
bool layOut (juce::AudioProcessor& p, Side side)
{
    auto layout = p.getBusesLayout();
    if (layout.inputBuses.isEmpty() || layout.outputBuses.isEmpty())
        return false;
    layout.inputBuses.getReference (0) = juce::AudioChannelSet::stereo();
    layout.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
    if (layout.inputBuses.size() > 1)
        layout.inputBuses.getReference (1) = side == Side::off ? juce::AudioChannelSet::disabled()
                                           : side == Side::kickMono ? juce::AudioChannelSet::mono()
                                                                    : juce::AudioChannelSet::stereo();
    return p.setBusesLayout (layout);
}

/** One take: the pad on the main input, `side` on the sidechain; the left and right outputs. */
std::vector<std::vector<float>> play (juce::AudioProcessor& p, Side side, const std::vector<float>& kick,
                                      int bufferChannels = 0)
{
    const int frames = (int) (kSeconds * kRate);
    const int outs = p.getTotalNumOutputChannels();
    const int mainIns = p.getMainBusNumInputChannels();
    // As many channels as the layout has, unless a host is played that passes fewer.
    const int channels = bufferChannels > 0 ? bufferChannels : juce::jmax (2, p.getTotalNumInputChannels(), outs);
    const int ins = juce::jmin (channels, p.getTotalNumInputChannels());
    p.setRateAndBufferSizeDetails (kRate, kBlock);
    p.prepareToPlay (kRate, kBlock);
    juce::AudioBuffer<float> buffer (channels, kBlock);
    juce::MidiBuffer midi;
    std::vector<std::vector<float>> out (2);
    for (int start = 0; start < frames; start += kBlock)
    {
        const int n = juce::jmin (kBlock, frames - start);
        buffer.clear();
        for (int ch = 0; ch < mainIns; ++ch)
            for (int i = 0; i < n; ++i)
                buffer.setSample (ch, i, padAt (start + i));
        const bool kicking = side == Side::kickStereo || side == Side::kickMono;
        for (int ch = mainIns; ch < ins && kicking; ++ch)
            for (int i = 0; i < n; ++i)
                buffer.setSample (ch, i, kick[(size_t) (start + i)]);
        midi.clear();
        juce::AudioBuffer<float> block (buffer.getArrayOfWritePointers(), channels, n);
        p.processBlock (block, midi);
        for (int ch = 0; ch < 2; ++ch)
        {
            const auto* samples = block.getReadPointer (juce::jmin (ch, outs - 1));
            out[(size_t) ch].insert (out[(size_t) ch].end(), samples, samples + n);
        }
    }
    p.releaseResources();
    return out;
}

float apart (const std::vector<std::vector<float>>& a, const std::vector<std::vector<float>>& b)
{
    float worst = 0.0f;
    for (size_t ch = 0; ch < juce::jmin (a.size(), b.size()); ++ch)
        for (size_t i = 0; i < juce::jmin (a[ch].size(), b[ch].size()); ++i)
            worst = juce::jmax (worst, std::abs (a[ch][i] - b[ch][i]));
    return worst;
}

double rmsDb (const std::vector<float>& v, double from, double to)
{
    double sum = 0.0;
    const auto a = (size_t) (from * kRate), b = juce::jmin (v.size(), (size_t) (to * kRate));
    for (size_t i = a; i < b; ++i)
        sum += (double) v[i] * v[i];
    return 10.0 * std::log10 (sum / (double) juce::jmax ((size_t) 1, b - a) + 1.0e-20);
}

/** How far the pad dips in the 50 ms after each kick, at the least, against the take without one. */
double smallestDip (const std::vector<float>& keyed, const std::vector<float>& quiet)
{
    double smallest = 1.0e9;
    for (double kick = kFirstKick; kick + 0.07 < kSeconds; kick += kBeat)
        smallest = juce::jmin (smallest, rmsDb (quiet, kick + 0.02, kick + 0.07) - rmsDb (keyed, kick + 0.02, kick + 0.07));
    return smallest;
}

void writeWav (const juce::File& file, const std::vector<std::vector<float>>& take, const std::vector<float>& kick)
{
    file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (new juce::FileOutputStream (file), kRate, 3, 24, {}, 0));
    if (writer == nullptr)
    {
        check (false, "the render can be written to " + file.getFullPathName());
        return;
    }
    juce::AudioBuffer<float> buffer (3, (int) take[0].size());
    for (int ch = 0; ch < 2; ++ch)
        buffer.copyFrom (ch, 0, take[(size_t) ch].data(), (int) take[(size_t) ch].size());
    buffer.copyFrom (2, 0, kick.data(), juce::jmin ((int) kick.size(), buffer.getNumSamples()));
    writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
    say ("wrote " + file.getFullPathName() + ": the plugin's left and right with the kick in its sidechain, then the kick");
}

std::unique_ptr<juce::AudioProcessor> loadVst3 (const juce::String& path, juce::String& error)
{
    const juce::File bundle = juce::File::getCurrentWorkingDirectory().getChildFile (path);
    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> descriptions;
    format.findAllTypesForFile (descriptions, bundle.getFullPathName());
    if (descriptions.isEmpty())
    {
        error = "no plugin found in " + bundle.getFullPathName();
        return nullptr;
    }
    std::cout << "vst3: " << descriptions[0]->name << " from " << bundle.getFullPathName() << std::endl;
    auto instance = format.createInstanceFromDescription (*descriptions[0], kRate, kBlock, error);
    return std::unique_ptr<juce::AudioProcessor> (instance.release());
}

} // namespace

int sidechainCheckCommand (const juce::StringArray& args)
{
    juce::String vst3Path, wavPath;
    double expectDuck = -1.0;
    for (int i = 2; i < args.size(); ++i)
    {
        if (args[i] == "--vst3" && i + 1 < args.size()) vst3Path = args[++i];
        else if (args[i] == "--wav" && i + 1 < args.size()) wavPath = args[++i];
        else if (args[i] == "--expect-duck" && i + 1 < args.size()) expectDuck = args[++i].getDoubleValue();
        else { std::cerr << "usage: pangea-render sidechain-check [--vst3 <plugin.vst3>] [--wav <out.wav>] [--expect-duck <dB>]\n"; return 2; }
    }

    std::unique_ptr<juce::AudioProcessor> own (createPluginFilter());
    const int declared = pangeaSidechainInputs (*own);
    std::cout << "sidechain-check: " << own->getName() << ", "
              << (pangeaIsMidiTool() ? "a MIDI tool" : pangeaIsInstrument() ? "an instrument" : "an effect")
              << ", the code declares " << (declared == 0 ? juce::String ("no sidechain") : juce::String (declared == 2 ? "a stereo sidechain" : "a mono sidechain"))
              << std::endl;
    std::cout << "inputs:  " << busesOf (*own, true) << std::endl;
    std::cout << "outputs: " << busesOf (*own, false) << std::endl;
    if (pangeaIsMidiTool() || pangeaIsInstrument())
    {
        check (own->getBusCount (true) <= 1, "it has no sidechain bus: its buses are as they were");
        std::cout << (failures == 0 ? "sidechain-check: passed" : "sidechain-check: " + juce::String (failures) + " failed") << std::endl;
        return failures == 0 ? 0 : 1;
    }

    auto isBus = [] (juce::AudioProcessor::Bus* bus, const juce::String& name, bool onByDefault)
    {
        return bus != nullptr && bus->getName() == name && bus->getDefaultLayout() == juce::AudioChannelSet::stereo()
               && bus->isEnabledByDefault() == onByDefault;
    };
    const bool mainAsBefore = own->getBusCount (false) == 1 && isBus (own->getBus (false, 0), "Output", true)
                              && isBus (own->getBus (true, 0), "Input", true);
    if (declared == 0)
    {
        check (mainAsBefore && own->getBusCount (true) == 1,
               "no sidechain declared: one input bus and one output bus, both stereo and on, as before");
        if (vst3Path.isNotEmpty())
        {
            juce::String error;
            auto hosted = loadVst3 (vst3Path, error);
            check (hosted != nullptr && hosted->getBusCount (true) == 1 && hosted->getBusCount (false) == 1,
                   "the VST3 has one input bus and one output bus" + (error.isEmpty() ? juce::String() : ": " + error));
        }
        std::cout << (failures == 0 ? "sidechain-check: passed" : "sidechain-check: " + juce::String (failures) + " failed") << std::endl;
        return failures == 0 ? 0 : 1;
    }

    check (mainAsBefore && own->getBusCount (true) == 2 && isBus (own->getBus (true, 1), "Sidechain", false),
           "a sidechain declared: the main buses as before, then an input bus named Sidechain, stereo and off by default");
    {
        auto layoutWith = [&own] (juce::AudioChannelSet main, juce::AudioChannelSet side)
        {
            auto layout = own->getBusesLayout();
            layout.inputBuses.getReference (0) = main;
            layout.outputBuses.getReference (0) = main;
            layout.inputBuses.getReference (1) = side;
            return own->checkBusesLayoutSupported (layout);
        };
        const auto mono = juce::AudioChannelSet::mono(), stereo = juce::AudioChannelSet::stereo();
        const auto off = juce::AudioChannelSet::disabled(), three = juce::AudioChannelSet::discreteChannels (3);
        check (layoutWith (stereo, off) && layoutWith (stereo, mono) && layoutWith (stereo, stereo)
                   && layoutWith (mono, off) && layoutWith (mono, mono) && layoutWith (mono, stereo) && ! layoutWith (stereo, three),
               "the sidechain takes off, mono and stereo on a stereo or a mono track, and refuses three channels");
    }

    const auto kick = kicks ((int) (kSeconds * kRate));
    auto take = [&] (Side side)
    {
        std::unique_ptr<juce::AudioProcessor> p (createPluginFilter());
        const juce::String what = side == Side::off ? "off" : side == Side::silent ? "on and silent"
                                : side == Side::kickMono ? "mono with a kick" : "stereo with a kick";
        check (layOut (*p, side), "the processor takes a stereo track with its sidechain " + what);
        auto out = play (*p, side, kick);
        return std::make_pair (out, pangeaSidechainHeard (*p));
    };
    const auto [off, offHeard] = take (Side::off);
    const auto [silent, silentHeard] = take (Side::silent);
    const auto [stereoKick, stereoHeard] = take (Side::kickStereo);
    const auto [monoKick, monoHeard] = take (Side::kickMono);
    check (offHeard == 0 && silentHeard == 2 && stereoHeard == 2 && monoHeard == 1,
           "the code is fed from the bus only while it is on: " + juce::String (offHeard) + ", " + juce::String (silentHeard) + ", "
               + juce::String (stereoHeard) + " and " + juce::String (monoHeard) + " channels in the four takes");
    float peak = 0.0f;
    for (const auto& ch : off)
        for (float x : ch)
            peak = juce::jmax (peak, std::abs (x));
    check (apart (off, silent) == 0.0f && peak > 0.01f,
           "with the sidechain off it gives exactly what it gives with it on and silent, and that is sound (peak "
               + juce::String (juce::Decibels::gainToDecibels (peak), 1) + " dBFS)");
    const float changed = apart (stereoKick, silent);
    check (changed > 1.0e-4f, "a kick in the sidechain changes the output (by up to " + juce::String (changed, 4) + ")");
    check (apart (monoKick, stereoKick) == 0.0f, "the kick routed as mono gives exactly what it gives routed as stereo");
    // A host that passes fewer channels than the layout it agreed to: the sidechain is read only as
    // far as the buffer goes, here not at all, so the plugin gives what it gives with it off.
    {
        std::unique_ptr<juce::AudioProcessor> p (createPluginFilter());
        check (layOut (*p, Side::kickStereo), "the processor takes a stereo track with its sidechain on, for a host that then passes two channels");
        const auto shortTake = play (*p, Side::kickStereo, kick, 2);
        check (apart (shortTake, off) == 0.0f && pangeaSidechainHeard (*p) == 0,
               "a host that passes only the two main channels with the sidechain on: the sidechain is read as off, and the plugin gives what it gives with it off");
    }
    const double dip = smallestDip (stereoKick[0], silent[0]);
    say ("the pad dips by " + juce::String (dip, 1) + " dB at least in the 50 ms after each kick, against the take with nothing routed");
    if (expectDuck >= 0.0)
        check (dip >= expectDuck, "it ducks by " + juce::String (expectDuck, 1) + " dB or more after every kick");
    if (wavPath.isNotEmpty())
        writeWav (juce::File::getCurrentWorkingDirectory().getChildFile (wavPath), stereoKick, kick);

    if (vst3Path.isNotEmpty())
    {
        juce::String error;
        auto hosted = loadVst3 (vst3Path, error);
        check (hosted != nullptr, "the VST3 loads" + (error.isEmpty() ? juce::String() : ": " + error));
        if (hosted != nullptr)
        {
            std::cout << "vst3 inputs:  " << busesOf (*hosted, true) << std::endl;
            std::cout << "vst3 outputs: " << busesOf (*hosted, false) << std::endl;
            check (hosted->getBusCount (true) == 2 && hosted->getBus (true, 1)->getName() == "Sidechain"
                       && ! hosted->getBus (true, 1)->isEnabledByDefault() && hosted->getTotalNumInputChannels() == 2,
                   "the VST3 has the Sidechain bus, off by default, and two input channels until a host switches it on");
            check (layOut (*hosted, Side::kickStereo) && hosted->getTotalNumInputChannels() == 4,
                   "a host switching the sidechain on gives the VST3 four input channels");
            const auto hostedKick = play (*hosted, Side::kickStereo, kick);
            check (apart (hostedKick, stereoKick) == 0.0f, "the VST3 with a kick in its sidechain gives what the processor gives");
            check (layOut (*hosted, Side::off), "a host switching it off again");
            const auto hostedOff = play (*hosted, Side::off, kick);
            check (apart (hostedOff, off) == 0.0f, "the VST3 with its sidechain off gives what the processor gives");
        }
    }

    std::cout << (failures == 0 ? "sidechain-check: passed" : "sidechain-check: " + juce::String (failures) + " failed") << std::endl;
    return failures == 0 ? 0 : 1;
}

int describeVst3Command (const juce::StringArray& args)
{
    if (args.size() < 3) { std::cerr << "usage: pangea-render describe-vst3 <plugin.vst3>\n"; return 2; }
    juce::String error;
    auto hosted = loadVst3 (args[2], error);
    if (hosted == nullptr) { std::cerr << "load failed: " << error << "\n"; return 1; }
    std::cout << "inputs: " << busesOf (*hosted, true) << std::endl;
    std::cout << "outputs: " << busesOf (*hosted, false) << std::endl;
    const auto parameters = hosted->getParameters();
    std::cout << "parameters: " << parameters.size() << std::endl;
    for (auto* p : parameters)
    {
        const auto* hostedParam = dynamic_cast<juce::HostedAudioProcessorParameter*> (p);
        std::cout << "  " << p->getParameterIndex() << " " << (hostedParam != nullptr ? hostedParam->getParameterID() : juce::String ("?"))
                  << " \"" << p->getName (64) << "\" default " << juce::String (p->getDefaultValue(), 6)
                  << " steps " << p->getNumSteps() << " label \"" << p->getLabel() << "\"" << std::endl;
    }
    juce::MemoryBlock state;
    hosted->getStateInformation (state);
    std::cout << "state: " << state.getSize() << " bytes" << std::endl;
    if (auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize()))
        std::cout << xml->toString (juce::XmlElement::TextFormat().singleLine()) << std::endl;
    else
        std::cout << state.toBase64Encoding() << std::endl;
    return 0;
}

} // namespace pangea::render
