#pragma once
// The song as Pangea gives it to the sound code before every block (decisions 123 and 124), worked
// out in one place for the built plugin (HybridShell.cpp, pushHost), the Pangea plugin
// (PangeaHost.h, pushTransport) and the transport check (tools/transport-check), so all three agree.
//
// [pangea:playing] holds 1 while the song plays and 0 while it is stopped. [pangea:beat] holds the
// song position in beats (quarter notes, bar 1 at 0) at the start of the block, and
// [pangea:beatfrac] that position's fraction of a beat, 0 to 1. A control is a single float, whose
// steps are about a millisecond apart by beat 20,000; the fraction is exact, so the step clock
// (library/custom/step-clock.lib) takes the whole beats from the position and the rest from the
// fraction. Plain C++ and no JUCE, so the transport check builds it without the shell.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace pangea
{

struct SongValues
{
    double playing = 1.0, beat = 0.0, fraction = 0.0;
};

/**
 * What the song's controls hold for a block, from the play head at its start. Without a song
 * position in beats (`havePosition` false, or not a number) the host counts as playing, whatever
 * it says of its transport, and the position is the beats counted since the plugin was prepared:
 * `countedSamples` at `bpm`, 120 BPM when that is not a tempo. JUCE's AU wrapper says "stopped"
 * for a host that lacks the transport callback, and its VST3 wrapper for a block with no process
 * context, so taking their word would stop a pattern for good where its own timer used to run.
 * With a position, `playing` is false only when the host says its transport is stopped.
 */
inline SongValues songValues (bool playing, bool havePosition, double ppq, std::int64_t countedSamples,
                              double bpm, double sampleRate)
{
    SongValues v;
    const bool known = havePosition && std::isfinite (ppq);
    v.playing = playing || ! known ? 1.0 : 0.0;
    if (! known)
    {
        const double rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        const double tempo = std::isfinite (bpm) && bpm >= 1.0 ? std::min (bpm, 999.0) : 120.0;
        ppq = (double) countedSamples * tempo / (60.0 * rate);
    }
    v.beat = ppq;
    v.fraction = ppq - std::floor (ppq);
    return v;
}

} // namespace pangea
