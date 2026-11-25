// Pangea native shell: what becomes of the notes a logic module sends (logic_api.h, version 2).
//
// A module owns its notes (docs/midi-tools.md, "The contract"), and the check before saving
// fails one that leaves a note on. The platform still holds a safety net, here, because a
// stuck note in someone's session is the worst thing a MIDI tool can do:
//
//   - only LOGIC_NOTE_ON and LOGIC_NOTE_OFF are notes; everything else is left to the caller
//   - offsets are clamped into the block; notes to 0..127, channels to 1..16 and velocities to
//     1..127 (a velocity that is not a number drops the note on)
//   - events are sorted again by offset, offs before ons at the same offset, otherwise in the
//     order the module gave them
//   - a note on for a note already sounding is preceded by its off, and an off for a note that
//     is not sounding is dropped, so the output never holds two ons or a stray off
//   - while the transport is stopped, note ons are dropped
//   - when asked (playback stopped, the transport jumped, a reset, a release, a bypass), every
//     note still sounding gets its off first, at offset 0, before the module's own events
//
// Plain C++ and std only, nothing allocates, so it runs on the audio thread and
// tools/test-notes.sh can test it with a bare compiler.
#pragma once

#include "logic_api.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace pangea::notes
{

/** One note message out of the platform: an on or an off at a sample of this block. */
struct Note
{
    int offset = 0;
    bool on = false;
    int channel = 1;    // 1..16
    int note = 0;       // 0..127
    int velocity = 0;   // 1..127 for an on, 0 for an off
};

/** Which notes are sounding, per MIDI channel and note number. */
class Tracker
{
public:
    bool isOn (int channel, int note) const { return valid (channel, note) && on[index (channel, note)] != 0; }

    void set (int channel, int note, bool sounding)
    {
        if (! valid (channel, note))
            return;
        auto& slot = on[index (channel, note)];
        if ((slot != 0) == sounding)
            return;
        slot = sounding ? 1 : 0;
        numOn += sounding ? 1 : -1;
    }

    int count() const { return numOn; }

    /** Calls fn (channel, note) for every sounding note, lowest channel and note first. */
    template <typename Fn>
    void forEachOn (Fn&& fn) const
    {
        if (numOn == 0)
            return;
        for (int i = 0; i < (int) on.size(); ++i)
            if (on[(size_t) i] != 0)
                fn (i / 128 + 1, i % 128);
    }

    void clear()
    {
        on.fill (0);
        numOn = 0;
    }

private:
    std::array<std::uint8_t, 16 * 128> on {};
    int numOn = 0;

    static bool valid (int channel, int note) { return channel >= 1 && channel <= 16 && note >= 0 && note <= 127; }
    static size_t index (int channel, int note) { return (size_t) ((channel - 1) * 128 + note); }
};

inline int clampInt (int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

/** The most events one block can carry (the platform asks a module for at most this many). */
constexpr int maxEvents = 1024;

/**
 * Turns one block's module events into well-formed notes, updating `tracker`. `endAllFirst`
 * ends every sounding note at offset 0 before anything else. Returns how many notes were
 * written to `out`, at most `maxOut`. Events that are not notes are ignored here.
 */
inline int route (const LogicEvent* events, int numEvents, int numSamples, bool playing, bool endAllFirst,
                  Tracker& tracker, Note* out, int maxOut)
{
    int written = 0;
    auto put = [&] (const Note& n)
    {
        if (written < maxOut)
            out[written++] = n;
    };

    if (endAllFirst)
    {
        tracker.forEachOn ([&] (int channel, int note) { put ({ 0, false, channel, note, 0 }); });
        tracker.clear();
    }

    // The note events, by index, sorted by offset with offs first at the same offset. An
    // insertion sort: stable, allocation free, and linear for the sorted lists modules return.
    numEvents = clampInt (numEvents, 0, maxEvents);
    const int last = numSamples > 0 ? numSamples - 1 : 0;
    std::array<int, maxEvents> order {};
    std::array<int, maxEvents> offsets {};
    int count = 0;
    for (int i = 0; i < numEvents; ++i)
    {
        const auto& e = events[i];
        if (e.control != LOGIC_NOTE_ON && e.control != LOGIC_NOTE_OFF)
            continue;
        offsets[(size_t) i] = clampInt (e.offset, 0, last);
        order[(size_t) count++] = i;
    }
    auto before = [&] (int a, int b)   // does event a sort before event b?
    {
        const int oa = offsets[(size_t) a], ob = offsets[(size_t) b];
        if (oa != ob)
            return oa < ob;
        return events[a].control == LOGIC_NOTE_OFF && events[b].control == LOGIC_NOTE_ON;
    };
    for (int i = 1; i < count; ++i)
    {
        const int item = order[(size_t) i];
        int j = i - 1;
        while (j >= 0 && before (item, order[(size_t) j]))
        {
            order[(size_t) (j + 1)] = order[(size_t) j];
            --j;
        }
        order[(size_t) (j + 1)] = item;
    }

    for (int k = 0; k < count; ++k)
    {
        const auto& e = events[order[(size_t) k]];
        const int offset = offsets[(size_t) order[(size_t) k]];
        const int channel = clampInt (e.channel, 1, 16);
        const int note = clampInt (e.note, 0, 127);
        if (e.control == LOGIC_NOTE_OFF)
        {
            if (! tracker.isOn (channel, note))
                continue;   // a stray off: nothing of this module's is sounding there
            put ({ offset, false, channel, note, 0 });
            tracker.set (channel, note, false);
            continue;
        }
        if (! playing || ! std::isfinite (e.value))
            continue;
        const int velocity = clampInt ((int) std::lround (e.value < -1000.0f ? -1000.0f : (e.value > 1000.0f ? 1000.0f : e.value)), 1, 127);
        if (tracker.isOn (channel, note))
            put ({ offset, false, channel, note, 0 });   // sounding already: end it before it starts again
        put ({ offset, true, channel, note, velocity });
        tracker.set (channel, note, true);
    }
    return written;
}

/**
 * Follows the host's transport from block to block and says when it jumped: a loop, a seek, a
 * locate. The next block should start where this one ends; one that starts more than a little
 * way off (64 samples, plus a sixteenth of the block for tempo ramps across a long block) has
 * jumped. Starting, stopping and the first block are not jumps.
 */
struct TransportFollower
{
    bool have = false, playing = false;
    double ppq = 0.0, bpm = 120.0;
    int samples = 0;

    bool jumped (double ppqNow, double bpmNow, int numSamples, double sampleRate, bool playingNow)
    {
        bool jump = false;
        if (have && playing && playingNow && sampleRate > 0.0 && bpmNow > 0.0)
        {
            const double expected = ppq + (double) samples * bpm / (60.0 * sampleRate);
            const double samplesPerBeat = sampleRate * 60.0 / bpmNow;
            const double tolerance = 64.0 + (double) samples / 16.0;
            jump = std::abs (ppqNow - expected) * samplesPerBeat > tolerance;
        }
        have = true;
        playing = playingNow;
        ppq = ppqNow;
        bpm = bpmNow;
        samples = numSamples;
        return jump;
    }

    void forget() { have = false; }
};

/**
 * The notes a MIDI tool sends out, for its panel's chord map and notes (docs/felt-look.md):
 * the audio thread puts each on and off it sends in a ring, and the web editor's timer takes what
 * came since it last looked, at most 30 times a second, and hands it to the page as a "notes"
 * event. One writer (the audio thread) and one reader (the message thread), no lock and no
 * allocation, and nothing is kept while no panel listens. A reader that has fallen further behind
 * than the ring holds goes on from the oldest note it still holds; a note the writer laps while it
 * is read may be read as the newer one. A view of the notes loses nothing it needs by either.
 */
struct Heard
{
    static constexpr int capacity = 2048;

    /** How many open panels listen: the audio thread keeps the notes only while one does. */
    std::atomic<int> listeners { 0 };

    void push (const Note& n) noexcept
    {
        if (listeners.load (std::memory_order_relaxed) <= 0)
            return;
        const auto at = written.load (std::memory_order_relaxed);
        ring[(size_t) (at % (std::uint64_t) capacity)].store (pack (n), std::memory_order_relaxed);
        written.store (at + 1, std::memory_order_release);
    }

    /** Where the notes written so far end: a new reader starts here. */
    std::uint64_t end() const noexcept { return written.load (std::memory_order_acquire); }

    /** Calls `each (on, note, velocity, channel)` for every note after `from`; answers where the next read starts. */
    template <typename Each>
    std::uint64_t read (std::uint64_t from, Each&& each) const
    {
        const auto last = written.load (std::memory_order_acquire);
        if (last - from > (std::uint64_t) capacity)
            from = last - (std::uint64_t) capacity;
        for (auto at = from; at < last; ++at)
        {
            const auto v = ring[(size_t) (at % (std::uint64_t) capacity)].load (std::memory_order_relaxed);
            each ((int) ((v >> 24) & 1u), (int) ((v >> 16) & 0x7fu), (int) ((v >> 8) & 0x7fu), (int) (v & 0x1fu));
        }
        return last;
    }

private:
    static std::uint32_t pack (const Note& n) noexcept
    {
        return ((std::uint32_t) (n.on ? 1 : 0) << 24) | ((std::uint32_t) clampInt (n.note, 0, 127) << 16)
             | ((std::uint32_t) clampInt (n.velocity, 0, 127) << 8) | (std::uint32_t) clampInt (n.channel, 1, 16);
    }

    std::array<std::atomic<std::uint32_t>, capacity> ring {};
    std::atomic<std::uint64_t> written { 0 };
};

/** What the module last showed in each display slot (LOGIC_DISPLAY), for the editor. */
struct Display
{
    static constexpr int numSlots = 8;
    std::array<std::atomic<float>, numSlots> slots;
    std::atomic<bool> playing { false };
    /** Why the module was stopped (pangea::logic::Stop in PangeaLogicGuard.h), or 0 while it runs. */
    std::atomic<int> stopped { 0 };

    Display()
    {
        for (auto& s : slots)
            s.store (-1.0f);
    }

    /** Takes a block's display events (slot `note`, value `value`); anything out of range is ignored. */
    void take (const LogicEvent* events, int numEvents)
    {
        numEvents = clampInt (numEvents, 0, maxEvents);
        for (int i = 0; i < numEvents; ++i)
        {
            const auto& e = events[i];
            if (e.control == LOGIC_DISPLAY && e.note >= 0 && e.note < numSlots && std::isfinite (e.value))
                slots[(size_t) e.note].store (e.value, std::memory_order_relaxed);
        }
    }

    /** The whole number a slot shows, or -1 when it shows nothing. */
    int position (int slot) const
    {
        if (slot < 0 || slot >= numSlots)
            return -1;
        const float v = slots[(size_t) slot].load (std::memory_order_relaxed);
        return v < 0.0f ? -1 : (int) std::lround (v);
    }
};

} // namespace pangea::notes
