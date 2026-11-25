// Pangea native shell: the one bypass every Pangea plugin has, which Pangea supplies (decisions 74
// and 75).
//
// The model that writes a plugin's sound never writes a bypass for the whole plugin, and the Faust
// code and the logic module know nothing of this one. It is a plugin parameter with one id and one
// name in every plugin. Both processors return it from getBypassParameter, so a host whose own
// bypass tells the plugin drives it, and both act on it inside processBlock through the fader
// below, so every switch is a short fade: HybridShell.cpp for a plugin built for one project,
// PangeaHost.h for the Pangea plugin.
//
// Plain C++ and std only. Nothing here allocates, locks or calls the system, so it runs on the
// audio thread, and tools/test-platform.sh tests it with a bare compiler.
#pragma once

#include <algorithm>
#include <cmath>

namespace pangea::bypass
{

/** The parameter's id, the same in every plugin. A Faust control's id is its label reduced to
    lower-case letters and digits, and a logic control's is "logic" and a number, so no project's
    code can make an id with an underscore: a control the model labels Bypass has the id "bypass". */
constexpr const char* parameterId = "pangea_bypass";

/** The name a host shows for it. */
constexpr const char* parameterName = "Bypass";

/** Its version hint. An Audio Unit lists its parameters by this hint and then by a hash of the
    id, and Logic keeps automation by a parameter's place in that list. Every control has 1, and
    the controls that only an instrument's effect section has have 2 (decision 73). With 3 the
    bypass comes after them all and moves none of them. */
constexpr int versionHint = 3;

/** How long a switch takes, from end to end. */
constexpr double fadeSeconds = 0.03;

/**
 * The gain between the plugin and what a bypassed plugin gives: 1 is the plugin, 0 is bypassed.
 * It moves towards what was last asked for by one step a sample and takes fadeSeconds from end to
 * end. It is counted in whole samples, so it arrives exactly and at the same sample every time.
 * A processor asks once a block (setTarget), takes the block's ramp (advance) and uses it sample
 * by sample (blend).
 */
class Fader
{
public:
    /** How the gain goes across one block. */
    struct Ramp
    {
        float start = 1.0f;   // the gain before the block's first sample
        float end = 1.0f;     // the gain of the block's last sample, and from then on

        /** How many samples of the block the gain moves for. After them it rests at `end`. */
        int moving() const noexcept { return to > from ? to - from : from - to; }

        /** The gain of one sample of the block, counted from 0. */
        float at (int sample) const noexcept
        {
            const int moved = from + direction * (sample + 1);
            return (float) (direction < 0 ? std::max (to, moved) : std::min (to, moved)) / (float) length;
        }

    private:
        friend class Fader;
        int from = 1, to = 1, length = 1, direction = 1;
    };

    /** Sets how many samples a fade takes at this rate. Nothing sounds across a prepare, so
        whatever is asked for next is taken at once, with no fade. */
    void prepare (double sampleRate) noexcept
    {
        const double rate = std::isfinite (sampleRate) && sampleRate > 0.0 ? sampleRate : 48000.0;
        fadeLength = std::max (1, (int) std::lround (std::min (rate, 1.0e7) * fadeSeconds));
        position = asked ? 0 : fadeLength;
        fresh = true;
    }

    /** What is asked for from now on: true to bypass, false for the plugin. */
    void setTarget (bool bypass) noexcept
    {
        asked = bypass;
        if (fresh)
        {
            position = bypass ? 0 : fadeLength;
            fresh = false;
        }
    }

    /** Moves the gain across a block of `numSamples` and answers how it went. */
    Ramp advance (int numSamples) noexcept
    {
        fresh = false;
        Ramp ramp;
        ramp.length = fadeLength;
        ramp.from = position;
        ramp.direction = asked ? -1 : 1;
        const int steps = std::min (std::max (numSamples, 0), fadeLength);
        position = std::min (fadeLength, std::max (0, position + ramp.direction * steps));
        ramp.to = position;
        ramp.start = (float) ramp.from / (float) fadeLength;
        ramp.end = (float) ramp.to / (float) fadeLength;
        return ramp;
    }

    /** True while a bypass is asked for, whether or not the fade is over. */
    bool bypassAsked() const noexcept { return asked; }
    /** True once a bypass that was asked for has arrived: nothing of the plugin is heard. */
    bool fullyBypassed() const noexcept { return asked && position == 0; }
    /** True while the plugin is in and no fade is running: a block needs nothing done to it. */
    bool fullyIn() const noexcept { return ! asked && position == fadeLength; }
    /** The gain now, from 0 (bypassed) to 1 (the plugin). */
    float gain() const noexcept { return (float) position / (float) fadeLength; }
    /** How many samples a fade takes from end to end. */
    int lengthInSamples() const noexcept { return fadeLength; }

private:
    int fadeLength = 1440;   // 30 ms at 48 kHz, until prepare says otherwise
    int position = 1440;     // fadeLength is the plugin, 0 is bypassed
    bool asked = false;
    bool fresh = true;
};

/**
 * What a bypassed effect gives on one channel of its output: its input, as it is on the host's
 * track. A mono input goes to both sides of a stereo output. A stereo input on a mono output is
 * the mean of its two sides, as a stereo result is there. With no input there is nothing to give.
 * The processors keep this before the plugin's output overwrites the host's buffer.
 */
inline void keepDry (float* dry, int outputChannel, int numOutputs, const float* const* inputs, int numInputs, int numSamples) noexcept
{
    if (numInputs <= 0)
        std::fill (dry, dry + numSamples, 0.0f);
    else if (numOutputs == 1 && numInputs >= 2)
        for (int i = 0; i < numSamples; ++i)
            dry[i] = (inputs[0][i] + inputs[1][i]) * 0.5f;
    else
        std::copy (inputs[std::min (outputChannel, numInputs - 1)], inputs[std::min (outputChannel, numInputs - 1)] + numSamples, dry);
}

/**
 * One channel of one block through a ramp. `wet` is what the plugin made, and is changed in
 * place. `dry` is what a bypassed plugin gives: an effect's input, or null for silence (an
 * instrument). Where the gain is 1 the plugin's sound is left as it is, and where it is 0 the
 * dry sound is given to the bit, whatever the plugin made (a sample that is not a number
 * included). Between, the two are mixed, so equal sounds stay equal.
 */
inline void blend (const Fader::Ramp& ramp, float* wet, const float* dry, int numSamples) noexcept
{
    auto mixed = [dry, wet] (int i, float gain)
    {
        const float d = dry != nullptr ? dry[i] : 0.0f;
        return gain <= 0.0f ? d : wet[i] * gain + d * (1.0f - gain);
    };
    const int moving = std::min (ramp.moving(), numSamples);
    for (int i = 0; i < moving; ++i)
        if (const float gain = ramp.at (i); gain < 1.0f)
            wet[i] = mixed (i, gain);
    if (ramp.end < 1.0f)
        for (int i = moving; i < numSamples; ++i)
            wet[i] = mixed (i, ramp.end);
}

} // namespace pangea::bypass
