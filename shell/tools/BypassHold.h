// What the checks of Pangea's bypass hold a render to (platform/PangeaBypass.h; decision 75), for
// both tools that check it: pangea-render's bypass-check (tools/render/BypassChecks.cpp, a plugin
// built for one project) and pangea-host-check (tools/host-check/Main.cpp, the Pangea plugin).
//
// A bypassed plugin's output is known to the sample. With the plugin's own render of the same
// run (`usual`) and what a bypassed plugin gives (`dry`: an effect's input, or silence), every
// sample of a run with the bypass switched must be the usual render where the gain is 1, the dry
// sound where it is 0, and the mix of the two between. The gain is worked out here with the
// plugin's own fader, asked as the processors ask it, once a block.
//
// Plain C++ and std only.
#pragma once

#include "PangeaBypass.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace pangea::bypasscheck
{

/** How close a sample inside a fade must be to the mix worked out here. */
constexpr float kMixed = 2.0e-6f;

/** The sound fed to an effect: two steady tones that differ, one of them swelling, with no clicks
    of their own, so that a step in the output is the plugin's or the bypass's. */
inline float inputAt (int channel, long long sample, double rate)
{
    const double t = (double) sample / rate;
    const double twoPi = 6.283185307179586;
    if (channel == 0)
        return (float) (0.4 * std::sin (twoPi * 220.0 * t) * (0.6 + 0.4 * std::sin (twoPi * 1.3 * t)));
    return (float) (0.3 * std::sin (twoPi * 331.0 * t + 0.7));
}

/** How many samples a fade takes at a rate. */
inline int fadeSamples (double rate)
{
    pangea::bypass::Fader fader;
    fader.prepare (rate);
    return fader.lengthInSamples();
}

/**
 * The gain the plugin's fader gives each sample of a run of `blocks` blocks in which the bypass
 * is moved before the blocks named in `switches` ({ block, on }). `fromStart` is where the bypass
 * stands before the first block, which the fader takes at once.
 */
inline std::vector<float> gainsFor (double rate, int blockSize, int blocks, bool fromStart, const std::vector<std::pair<int, bool>>& switches)
{
    pangea::bypass::Fader fader;
    fader.prepare (rate);
    bool on = fromStart;
    std::vector<float> gains;
    gains.reserve ((size_t) blocks * (size_t) blockSize);
    for (int b = 0; b < blocks; ++b)
    {
        for (const auto& s : switches)
            if (s.first == b)
                on = s.second;
        fader.setTarget (on);
        const auto ramp = fader.advance (blockSize);
        for (int i = 0; i < blockSize; ++i)
            gains.push_back (ramp.at (i));
    }
    return gains;
}

/** How a run stands against what the fader makes of the plugin's own render and the dry sound. */
struct Held
{
    float apartAtRest = 0.0f;     // the furthest a sample is from the render (gain 1) or the dry sound (gain 0)
    float apartInFades = 0.0f;    // the furthest a sample inside a fade is from the mix of the two
    float largestStep = 0.0f;     // the largest step between two samples inside the fades
    float overAllowed = 0.0f;     // how far any step inside a fade is over what the fade allows (0: none is)
    float ownStep = 0.0f;         // the largest step the two sounds have of their own in the same places
    size_t fadeSamples = 0;       // samples where the gain is between 0 and 1
    size_t bypassedSamples = 0;   // samples where the gain is 0

    /** Takes the worse of this and another channel's. */
    void worse (const Held& other)
    {
        apartAtRest = std::max (apartAtRest, other.apartAtRest);
        apartInFades = std::max (apartInFades, other.apartInFades);
        largestStep = std::max (largestStep, other.largestStep);
        overAllowed = std::max (overAllowed, other.overAllowed);
        ownStep = std::max (ownStep, other.ownStep);
        fadeSamples = other.fadeSamples;
        bypassedSamples = other.bypassedSamples;
    }

    /** True when the run is the render or the dry sound to the bit at rest, the mix inside the
        fades, and has no step there that the fade does not allow. */
    bool holds() const { return apartAtRest <= 0.0f && apartInFades <= kMixed && overAllowed <= 0.0f; }
};

/**
 * One channel of a run (`out`) against the plugin's own render of it (`usual`) and what a
 * bypassed plugin gives (`dry`, empty for silence) under `gains`. With `wetKnown` false the
 * plugin's sound once the bypass is asked for is not the usual render's (its logic module is run
 * as stopped), so only the samples before the first switch and the fully bypassed ones are held.
 *
 * What a fade allows between two samples: the larger of the two sounds' own steps there, and one
 * step of the gain across the distance between the two sounds.
 */
inline Held holdTo (const std::vector<float>& out, const std::vector<float>& usual, const std::vector<float>& dry,
                    const std::vector<float>& gains, double rate, bool wetKnown = true)
{
    Held h;
    const float gainStep = 1.0f / (float) fadeSamples (rate);
    auto inFade = [] (float gain) { return gain > 0.0f && gain < 1.0f; };
    bool switched = false;
    for (size_t s = 0; s < out.size() && s < gains.size(); ++s)
    {
        const float g = gains[s];
        const float d = dry.empty() ? 0.0f : dry[s];
        switched = switched || g < 1.0f;
        h.fadeSamples += inFade (g) ? 1 : 0;
        if (g <= 0.0f)
        {
            ++h.bypassedSamples;
            h.apartAtRest = std::max (h.apartAtRest, std::abs (out[s] - d));
        }
        else if (wetKnown || ! switched)
        {
            if (g >= 1.0f)
                h.apartAtRest = std::max (h.apartAtRest, std::abs (out[s] - usual[s]));
            else
                h.apartInFades = std::max (h.apartInFades, std::abs (out[s] - (usual[s] * g + d * (1.0f - g))));
        }
        // The step into this sample, wherever it or the one before it is inside a fade: the first
        // sample of a fade and the first after it are steps of the fade too.
        if (s == 0 || ! wetKnown || ! (inFade (g) || inFade (gains[s - 1])))
            continue;
        const float dBefore = dry.empty() ? 0.0f : dry[s - 1];
        const float own = std::max (std::abs (usual[s] - usual[s - 1]), std::abs (d - dBefore));
        const float allowed = own + std::abs (usual[s - 1] - dBefore) * gainStep + kMixed;
        const float stepHere = std::abs (out[s] - out[s - 1]);
        h.largestStep = std::max (h.largestStep, stepHere);
        h.ownStep = std::max (h.ownStep, own);
        h.overAllowed = std::max (h.overAllowed, stepHere - allowed);
    }
    return h;
}

// ---- A MIDI tool -------------------------------------------------------------------------------

/** One MIDI message out of a plugin, at the sample of the whole run it fell on. */
struct Message
{
    enum class Kind { noteOff, noteOn, controller, other };
    long long sample = 0;
    Kind kind = Kind::other;
    int channel = 1;
    int number = 0;   // the note, or the controller
    int value = 0;    // the velocity, or the controller's value

    bool operator== (const Message& o) const
    {
        return sample == o.sample && kind == o.kind && channel == o.channel && number == o.number && (kind == Kind::noteOff || value == o.value);
    }
};

/** The note and the controller a check sends a bypassed MIDI tool, which must pass through. */
constexpr int kPlayedChannel = 5, kPlayedNote = 40, kPlayedVelocity = 99, kPlayedController = 1, kPlayedControllerValue = 64;

/**
 * The block before which to bypass a MIDI tool: the first block that starts inside one of the
 * tool's own notes, looking from `fromBlock` on in its usual run, so that the bypass has a note
 * to end. `midNote` says whether there is one; without, `fromBlock` is answered.
 */
inline int blockInsideANote (const std::vector<Message>& usual, int blockSize, int fromBlock, bool& midNote)
{
    midNote = false;
    for (size_t i = 0; i < usual.size(); ++i)
    {
        const auto& on = usual[i];
        if (on.kind != Message::Kind::noteOn || on.sample < (long long) fromBlock * blockSize)
            continue;
        const int candidate = (int) (on.sample / blockSize) + 1;
        for (size_t k = i + 1; k < usual.size(); ++k)
        {
            const auto& later = usual[k];
            if (later.kind == Message::Kind::noteOff && later.channel == on.channel && later.number == on.number)
            {
                if (later.sample > (long long) candidate * blockSize)
                {
                    midNote = true;
                    return candidate;
                }
                break;
            }
        }
    }
    return fromBlock;
}

/** What a bypassed MIDI tool did, against its usual run. */
struct ToolHeld
{
    bool sameBefore = true;         // up to the bypass, message for message what it always sends
    int wereOn = 0;                 // how many of its own notes were on where the bypass began
    bool endedOwn = false;          // each of those has its off on the bypass's first sample
    int ownOnsWhile = 0;            // notes of its own started while bypassed
    bool passedNote = false;        // the note sent in came out on its own sample, unchanged
    bool passedController = false;  // and so did the controller
    bool endedPassed = false;       // the note that passed has its off on the first sample after the bypass
    int onsAfter = 0;               // notes it starts after the bypass
    int leftOn = 0;                 // notes still on at the end of the run
    int strayOffs = 0;              // offs for notes that were not on
    bool backInTime = false;        // the notes it starts after the bypass are the ones it always starts there

    /** True when the tool was transparent while bypassed and whole again after. */
    bool holds() const
    {
        return sameBefore && endedOwn && ownOnsWhile == 0 && passedNote && passedController && endedPassed
            && onsAfter > 0 && leftOn == 0 && strayOffs == 0;
    }
};

/**
 * `take` is a run in which the tool was bypassed from `onSample` to `offSample` and was sent the
 * note at `playedAt` and the controller at `controllerAt` in between. `usual` is the same run
 * with no bypass and nothing sent.
 */
inline ToolHeld holdTool (const std::vector<Message>& usual, const std::vector<Message>& take,
                          long long onSample, long long offSample, long long playedAt, long long controllerAt)
{
    using Kind = Message::Kind;
    ToolHeld h;
    std::vector<std::pair<int, int>> sounding;   // channel and note
    auto find = [&sounding] (int channel, int note)
    {
        return std::find (sounding.begin(), sounding.end(), std::make_pair (channel, note));
    };
    size_t index = 0;
    for (; index < take.size() && take[index].sample < onSample; ++index)
    {
        const auto& m = take[index];
        h.sameBefore = h.sameBefore && index < usual.size() && usual[index] == m;
        if (m.kind == Kind::noteOn)
            sounding.emplace_back (m.channel, m.number);
        else if (m.kind == Kind::noteOff && find (m.channel, m.number) != sounding.end())
            sounding.erase (find (m.channel, m.number));
    }
    const auto wereOn = sounding;
    h.wereOn = (int) wereOn.size();
    int endedAtBypass = 0;
    for (; index < take.size(); ++index)
    {
        const auto& m = take[index];
        const bool isPlayed = m.channel == kPlayedChannel && m.number == kPlayedNote;
        if (m.kind == Kind::noteOn)
        {
            if (m.sample >= offSample)
                ++h.onsAfter;
            else if (isPlayed && m.sample == playedAt && m.value == kPlayedVelocity)
                h.passedNote = true;
            else
                ++h.ownOnsWhile;
            sounding.emplace_back (m.channel, m.number);
        }
        else if (m.kind == Kind::noteOff)
        {
            if (m.sample == onSample && std::find (wereOn.begin(), wereOn.end(), std::make_pair (m.channel, m.number)) != wereOn.end())
                ++endedAtBypass;
            if (isPlayed && m.sample == offSample)
                h.endedPassed = true;
            if (find (m.channel, m.number) != sounding.end())
                sounding.erase (find (m.channel, m.number));
            else
                ++h.strayOffs;
        }
        else if (m.kind == Kind::controller && m.sample == controllerAt && m.channel == kPlayedChannel
                 && m.number == kPlayedController && m.value == kPlayedControllerValue)
            h.passedController = true;
    }
    h.endedOwn = endedAtBypass == h.wereOn;
    h.leftOn = (int) sounding.size();

    std::vector<Message> now, always;
    for (const auto& m : take)
        if (m.kind == Kind::noteOn && m.sample >= offSample)
            now.push_back (m);
    for (const auto& m : usual)
        if (m.kind == Kind::noteOn && m.sample >= offSample)
            always.push_back (m);
    h.backInTime = now == always;
    return h;
}

} // namespace pangea::bypasscheck
