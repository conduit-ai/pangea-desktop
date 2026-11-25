// The native tests for Two Hands (README.md, "How it was verified"). Plain clang++, no JUCE:
//
//   clang++ -std=c++17 -O2 -I shell/platform -I shell/examples/two-hands \
//           shell/examples/two-hands/test/harness.cpp -o harness
//   ./harness                  the checks; the exit status is the number of failures
//   ./harness --dump <file>    also writes scripts and events for test/wasm-compare.mjs
//
// The rules the velocities are checked against are written out again here, from pp_core.cpp,
// rather than taken from the module, so a slip in the port shows.
#include "runner.h"
#include <cmath>
#include <cstdarg>
#include <cstring>
#include <map>
#include <set>

using namespace runner;
using namespace twohands;

static int failures = 0;
static int checks = 0;

static void expect (bool ok, const char* format, ...)
{
    ++checks;
    if (ok) return;
    ++failures;
    std::printf ("  FAIL ");
    va_list args;
    va_start (args, format);
    std::vprintf (format, args);
    va_end (args);
    std::printf ("\n");
}

static const long long kBar = 96000;    // 4 beats at 120 BPM and 48 kHz
static const long long kStep = 6000;    // a 1/16

static Script script (const char* name, double bars, int block = 128)
{
    Script s;
    s.name = name;
    s.block = block;
    s.length = (long long) std::llround (bars * (double) kBar);
    return s;
}

static Script withBlock (Script s, int block)
{
    s.block = block;
    s.name += "-b" + std::to_string (block);
    return s;
}

// ------------------------------------------------------------------ note bookkeeping

struct Note
{
    long long on, off;
    int channel, pitch, velocity;
};

// Every note on with its note off; problems are reported under `label`.
static std::vector<Note> pairNotes (const std::vector<Ev>& events, const char* label, bool report = true)
{
    std::vector<Note> notes;
    std::map<int, size_t> sounding;   // channel * 128 + pitch -> index in notes
    long long lastTime = -1;
    int lastOffset = -1;
    int lastRank = -1;
    for (const Ev& e : events)
    {
        if (report)
        {
            expect (e.offset >= 0 && e.offset < e.blockSize, "%s: an event at offset %d in a block of %d", label, e.offset, e.blockSize);
            const int rank = e.control == LOGIC_NOTE_OFF ? 0 : (e.control == LOGIC_DISPLAY ? 1 : 2);
            if (e.time == lastTime && e.offset == lastOffset)
                expect (rank >= lastRank, "%s: at %lld a note off comes after a note on", label, e.time);
            expect (e.time >= lastTime, "%s: events out of order at %lld", label, e.time);
            lastTime = e.time;
            lastOffset = e.offset;
            lastRank = rank;
        }
        if (e.control != LOGIC_NOTE_ON && e.control != LOGIC_NOTE_OFF) continue;
        const int key = e.channel * 128 + e.note;
        if (e.control == LOGIC_NOTE_ON)
        {
            if (report)
            {
                expect (e.playing, "%s: a note starts while stopped, at %lld", label, e.time);
                expect (e.channel == 1 || e.channel == 2, "%s: channel %d", label, e.channel);
                expect (e.note >= 0 && e.note <= 127, "%s: note %d", label, e.note);
                expect (e.value >= 1.0f && e.value <= 127.0f && e.value == std::floor (e.value), "%s: velocity %g", label, (double) e.value);
                expect (sounding.count (key) == 0, "%s: note %d on channel %d starts again before it ends, at %lld", label, e.note, e.channel, e.time);
            }
            sounding[key] = notes.size();
            notes.push_back ({ e.time, -1, e.channel, e.note, (int) e.value });
        }
        else
        {
            auto it = sounding.find (key);
            if (report) expect (it != sounding.end(), "%s: a note off for %d on channel %d that is not sounding, at %lld", label, e.note, e.channel, e.time);
            if (it == sounding.end()) continue;
            notes[it->second].off = e.time;
            sounding.erase (it);
        }
    }
    if (report) expect (sounding.empty(), "%s: %zu notes still sound after the stop", label, sounding.size());
    return notes;
}

static bool sameEvents (const std::vector<Ev>& a, const std::vector<Ev>& b, std::string& why)
{
    if (a.size() != b.size()) { why = std::to_string (a.size()) + " against " + std::to_string (b.size()) + " events"; return false; }
    for (size_t i = 0; i < a.size(); ++i)
        if (! sameEvent (a[i], b[i]))
        {
            why = "event " + std::to_string (i) + " at " + std::to_string (a[i].time) + " against " + std::to_string (b[i].time);
            return false;
        }
    return true;
}

// The display value of each slot at each time.
static std::map<long long, int> displayOf (const std::vector<Ev>& events, int slot)
{
    std::map<long long, int> shown;
    for (const Ev& e : events)
        if (e.control == LOGIC_DISPLAY && e.note == slot) shown[e.time] = (int) e.value;
    return shown;
}

// ------------------------------------------------------------------ the rules, written out again

static const int kDefaultAccents[2][4] = { { 68, 45, 53, 0 }, { 76, 50, 61, 46 } };

// pp_core.cpp barWeight on the 1/16 grid: step k of a 4/4 bar.
static double weightOfStep (long long j)
{
    const int k = (int) (j % 16);
    if (k == 0) return 1.0;
    if (k == 8) return 0.6;
    if (k % 4 == 0) return 0.35;
    if (k % 2 == 0) return 0.15;
    return 0.0;
}

// The velocity rawAttack gives step j of a hand at 1/16, without a variation.
static int expectedVelocity (int hand, long long j, float tension, float balance, const int accents[2][4])
{
    const int A = hand == 0 ? 3 : 4;
    double mean = 0;
    for (int i = 0; i < A; ++i) mean += accents[hand][i];
    mean /= A;
    // One product per statement, as in the module, so no multiply and add are fused.
    const double fg = hand == 1 ? (double) balance : -(double) balance;
    const double lean = 0.6 * fg;
    const double contrast = 1.0 + lean;
    const double front = 10.0 * fg;
    double v = mean + front;
    const double own = contrast * ((double) accents[hand][j % A] - mean);
    v += own;
    const double scale = (double) tension * 14.0;
    const double bar = scale * (weightOfStep (j) - 0.3);
    v += bar;
    const double meet = (double) 0.3f * 14.0;
    if (j % 12 == 0) v += meet;   // both cycles restart: 3 x 4
    const long long r = v < 0 ? -(long long) std::floor (-v + 0.5) : (long long) std::floor (v + 0.5);
    return (int) (r < 1 ? 1 : (r > 127 ? 127 : r));
}

// ------------------------------------------------------------------ the checks

static std::vector<Script> dumped;   // scripts for the WebAssembly comparison

static void blockSizes()
{
    std::printf ("Block sizes: the same events at 37, 64, 128, 480 and 1024 samples\n");
    std::vector<Script> scripts;
    scripts.push_back (script ("defaults", 24));
    Script busy = script ("busy", 24);
    busy.param (0, kParamKey, 9).param (0, kParamMode, 1).param (0, kParamRate, 4).param (0, kParamTension, 0.8)
        .param (0, kParamBalance, -0.4).param (0, kParamLength, 1.3).param (0, kParamVariation, 42).param (0, kParamAmount, 0.9)
        .param (0, kParamLeftAccent, 100);
    scripts.push_back (busy);
    // 568320 samples is a whole number of blocks at every size, so a change lands at the same
    // sample everywhere. A variation may change at any time: it waits for the next bar line.
    Script changes = script ("changes", 24);
    changes.param (568320, kParamRate, 2).param (568320, kParamKey, 5)
        .param (573320, kParamVariation, 77).param (573320, kParamAmount, 0.7)
        .param (1136640, kParamLength, 1.5).param (1136640, kParamRate, 3)
        .param (1143640, kParamVariation, 3);
    scripts.push_back (changes);

    const int blocks[5] = { 37, 64, 128, 480, 1024 };
    for (const Script& s : scripts)
    {
        const std::vector<Ev> reference = run (withBlock (s, 128));
        int notes = (int) onsOnly (reference).size();
        for (int block : blocks)
        {
            const Script sized = withBlock (s, block);
            const std::vector<Ev> got = run (sized);
            std::string why;
            const bool same = sameEvents (got, reference, why);
            expect (same, "%s: block %d differs from block 128 (%s)", s.name.c_str(), block, why.c_str());
            pairNotes (got, sized.name.c_str());
            dumped.push_back (sized);
        }
        std::printf ("  %-9s %5d notes, %6zu events, identical at all five sizes\n", s.name.c_str(), notes, reference.size());
    }
}

// Events of a render from `from` on, without the note offs of notes started before it.
static std::vector<Ev> fromSample (const std::vector<Ev>& events, long long from)
{
    std::vector<Ev> kept;
    std::set<int> before;
    for (const Ev& e : events)
    {
        const int key = e.channel * 128 + e.note;
        if (e.time < from)
        {
            if (e.control == LOGIC_NOTE_ON) before.insert (key);
            if (e.control == LOGIC_NOTE_OFF) before.erase (key);
            continue;
        }
        if (e.control == LOGIC_NOTE_OFF && before.count (key)) { before.erase (key); continue; }
        kept.push_back (e);
    }
    return kept;
}

static void songPosition()
{
    std::printf ("Song position: starting at bar 5 sounds the same as arriving there\n");
    struct Case { const char* name; double rate, variation; };
    const Case cases[3] = { { "defaults", 1, 0 }, { "dotted 1/8, variation 42", 3, 42 }, { "triplet 1/8, variation 9", 4, 9 } };
    for (const Case& c : cases)
    {
        Script whole = script ("from-start", 8);
        whole.param (0, kParamRate, c.rate).param (0, kParamVariation, c.variation);
        Script late = script ("from-bar-5", 4);
        late.startPpq = 16.0;
        late.param (0, kParamRate, c.rate).param (0, kParamVariation, c.variation);
        const std::vector<Ev> a = fromSample (run (whole), 4 * kBar);
        std::vector<Ev> b = run (late);
        for (Ev& e : b) e.time += 4 * kBar;
        b = fromSample (b, 4 * kBar);
        std::string why;
        const bool same = sameEvents (a, b, why);
        expect (same, "%s: playing from bar 5 differs (%s)", c.name, why.c_str());
        std::printf ("  %-26s %4zu notes from bar 5 on, %s\n", c.name, onsOnly (b).size(), same ? "identical" : "DIFFERENT");
        dumped.push_back (late);
    }
}

static void noStuckNotes()
{
    std::printf ("Notes: none stuck, none overlapping on a channel, all in range\n");
    int runs = 0;
    long long notes = 0;
    for (int rate = 0; rate < 5; ++rate)
        for (double length : { 0.2, 0.9, 1.5 })
            for (double variation : { 0.0, 7.0, 42.0, 9999.0 })
                for (int mode = 0; mode < 2; ++mode)
                {
                    Script s = script ("sweep", 12, 480);
                    s.param (0, kParamRate, rate).param (0, kParamLength, length).param (0, kParamVariation, variation)
                        .param (0, kParamAmount, 1.0).param (0, kParamMode, mode).param (0, kParamKey, (rate * 5 + mode * 7) % 12);
                    char label[96];
                    std::snprintf (label, sizeof label, "rate %d length %.1f variation %.0f mode %d", rate, length, variation, mode);
                    notes += (long long) pairNotes (run (s), label).size();
                    ++runs;
                }

    // A loop back mid-note, a jump to an odd place, tempo changes, a stop and start mid-note,
    // other sample rates and bar lengths, and controls that change all the time.
    std::vector<Script> hard;
    Script jumps = script ("jumps", 8);
    jumps.jump (2 * kBar + 50 * 128, 0.0).jump (4 * kBar, 22.3).jump (6 * kBar + 3000, 7.875);
    hard.push_back (jumps);
    Script tempo = script ("tempo", 8);
    tempo.tempo (2 * kBar, 87.3).tempo (4 * kBar + 777, 160.0).param (5 * kBar, kParamLength, 1.5).tempo (6 * kBar, 61.0);
    hard.push_back (tempo);
    Script halt = script ("halt", 6);
    halt.halt (3 * kBar + 1024, 3).param (4 * kBar + 2048, kParamVariation, 5).halt (4 * kBar + 2048, 2);
    hard.push_back (halt);
    Script flips = script ("flips", 12, 256);
    for (int bar = 0; bar < 12; ++bar)
    {
        flips.param (bar * kBar + 3 * 256, kParamRate, (bar * 3) % 5);
        flips.param (bar * kBar + 47000, kParamVariation, (bar * 37) % 50);
        flips.param (bar * kBar + 61000, kParamLength, 0.2 + 0.13 * bar);
        flips.param (bar * kBar + 77000, kParamKey, (bar * 5) % 12);
        flips.param (bar * kBar + 85000, kParamRightAccent + bar % 4, (bar * 41) % 128);
    }
    hard.push_back (flips);
    Script rate44 = script ("44k1-97bpm", 8, 37);
    rate44.sampleRate = 44100.0;
    rate44.bpm = 97.0;
    rate44.length = 8LL * 4 * 44100 * 60 / 97;
    rate44.param (0, kParamRate, 4).param (0, kParamVariation, 1234);
    hard.push_back (rate44);
    Script waltz = script ("three-four", 8);
    waltz.beatsPerBar = 3.0;
    waltz.param (0, kParamVariation, 21).param (0, kParamRate, 0);
    hard.push_back (waltz);
    Script seven = script ("seven-eight", 8, 333);
    seven.beatsPerBar = 3.5;
    seven.param (0, kParamRate, 3).param (0, kParamLength, 1.5);
    hard.push_back (seven);
    Script preroll = script ("pre-roll", 4);
    preroll.startPpq = -2.0;
    hard.push_back (preroll);

    for (const Script& s : hard)
    {
        const std::vector<Ev> events = run (s);
        notes += (long long) pairNotes (events, s.name.c_str()).size();
        ++runs;
        dumped.push_back (s);
    }

    // The jump and the stop end what is sounding at once, at the start of that block.
    {
        const std::vector<Ev> events = run (hard[0]);
        const long long at = 2 * kBar + 50 * 128;
        std::set<int> sounding, ended;
        for (const Ev& e : events)
        {
            if (e.time < at)
            {
                if (e.control == LOGIC_NOTE_ON) sounding.insert (e.channel * 128 + e.note);
                if (e.control == LOGIC_NOTE_OFF) sounding.erase (e.channel * 128 + e.note);
            }
            else if (e.time == at && e.offset == 0 && e.control == LOGIC_NOTE_OFF) ended.insert (e.channel * 128 + e.note);
        }
        expect (! sounding.empty() && sounding == ended, "jumps: the loop back did not end the %zu sounding notes at once", sounding.size());
        std::printf ("  a loop back mid-note ends the %zu sounding notes at offset 0 of that block\n", sounding.size());
    }
    {
        const std::vector<Ev> events = run (hard[2]);
        int offs = 0, others = 0;
        for (const Ev& e : events)
            if (! e.playing) (e.control == LOGIC_NOTE_OFF && e.offset == 0 ? offs : others) += 1;
        expect (offs > 0 && others == 0, "halt: stopped blocks held %d note offs at 0 and %d other events", offs, others);
        std::printf ("  stopping mid-note sends %d note offs at offset 0, and nothing else while stopped\n", offs);
    }
    // A broken host: a tempo or a position that is not a number counts as stopped.
    {
        TwoHandsHost host (48000.0);
        std::vector<Ev> events;
        for (int k = 0; k < 40; ++k) host.process (48000.0, 120.0, k * 128 / 24000.0, 128, true, 4.0, k * 128, events);
        const size_t before = events.size();
        host.process (48000.0, std::nan (""), 40 * 128 / 24000.0, 128, true, 4.0, 40 * 128, events);
        host.process (48000.0, 120.0, std::nan (""), 128, true, 4.0, 41 * 128, events);
        int offs = 0, others = 0;
        for (size_t i = before; i < events.size(); ++i)
            (events[i].control == LOGIC_NOTE_OFF && events[i].offset == 0 ? offs : others) += 1;
        expect (offs == 2 && others == 0, "a tempo that is not a number: %d note offs, %d other events", offs, others);
        std::printf ("  a tempo or song position that is not a number ends the %d sounding notes and starts none\n", offs);
    }
    std::printf ("  %d runs, %lld notes, every one ended once\n", runs, notes);
}

static void threeAgainstFour()
{
    std::printf ("3 against 4: the hands realign every 12 steps\n");
    Script s = script ("plain", 4);
    s.param (0, kParamTension, 0.0);
    const std::vector<Ev> events = run (s);
    const std::map<long long, int> left = displayOf (events, 0), right = displayOf (events, 1);
    std::map<long long, int> velocity[2];
    for (const Ev& e : onsOnly (events)) velocity[e.channel - 1][e.time] = (int) e.value;
    int meets = 0;
    for (long long j = 0; j < 64; ++j)
    {
        const long long t = j * kStep;
        expect (left.count (t) && left.at (t) == (int) (j % 3), "step %lld: the left hand shows %d", j, left.count (t) ? left.at (t) : -1);
        expect (right.count (t) && right.at (t) == (int) (j % 4), "step %lld: the right hand shows %d", j, right.count (t) ? right.at (t) : -1);
        const bool together = left.count (t) && right.count (t) && left.at (t) == 0 && right.at (t) == 0;
        expect (together == (j % 12 == 0), "step %lld: the cycles %s together", j, together ? "start" : "do not start");
        meets += together ? 1 : 0;
        // Tension 0, Balance 0: an accent as set, plus 0.3 x 14 where both cycles restart.
        if (j % 3 == 0) expect (velocity[0][t] == (j % 12 == 0 ? 72 : 68), "step %lld: left velocity %d", j, velocity[0][t]);
        if (j % 4 == 0) expect (velocity[1][t] == (j % 12 == 0 ? 80 : 76), "step %lld: right velocity %d", j, velocity[1][t]);
    }
    std::printf ("  64 steps: the cycles start together %d times, at steps 0, 12, 24, 36, 48 and 60, with +4.2 on the accent\n", meets);
}

static void tensionAndBalance()
{
    std::printf ("Tension and Balance: velocities follow the rules of rawAttack\n");
    int compared = 0;
    for (float tension : { 0.0f, 0.25f, 0.5f, 1.0f })
        for (float balance : { -1.0f, -0.5f, 0.0f, 0.3f, 1.0f })
        {
            Script s = script ("rules", 12, 480);
            s.param (0, kParamTension, tension).param (0, kParamBalance, balance);
            for (const Ev& e : onsOnly (run (s)))
            {
                const long long j = e.time / kStep;
                expect (e.time % kStep == 0, "a note at %lld is off the grid", e.time);
                const int want = expectedVelocity (e.channel - 1, j, tension, balance, kDefaultAccents);
                expect ((int) e.value == want, "tension %.2f balance %.2f step %lld hand %d: velocity %d, the rules say %d",
                        (double) tension, (double) balance, j, e.channel, (int) e.value, want);
                ++compared;
            }
        }
    // Which way each one moves.
    auto average = [] (float tension, float balance, int channel, bool downbeats)
    {
        Script s = script ("direction", 12, 480);
        s.param (0, kParamTension, tension).param (0, kParamBalance, balance);
        double sum = 0;
        int n = 0;
        for (const Ev& e : onsOnly (run (s)))
        {
            const long long j = e.time / kStep;
            if (e.channel != channel || (downbeats != (j % 16 == 0))) continue;
            sum += e.value;
            ++n;
        }
        return sum / n;
    };
    const double downLow = average (0.0f, 0.0f, 2, true), downHigh = average (1.0f, 0.0f, 2, true);
    const double offLow = average (0.0f, 0.0f, 2, false), offHigh = average (1.0f, 0.0f, 2, false);
    const double rightMid = average (0.5f, 0.0f, 2, false), rightFront = average (0.5f, 1.0f, 2, false);
    const double leftMid = average (0.5f, 0.0f, 1, false), leftBack = average (0.5f, 1.0f, 1, false);
    expect (downHigh > downLow && offHigh < offLow, "Tension: downbeats %.1f to %.1f, the rest %.1f to %.1f", downLow, downHigh, offLow, offHigh);
    expect (rightFront > rightMid && leftBack < leftMid, "Balance: right %.1f to %.1f, left %.1f to %.1f", rightMid, rightFront, leftMid, leftBack);
    std::printf ("  %d notes at 20 settings, every velocity as the rules say\n", compared);
    std::printf ("  Tension 0 to 1: right-hand downbeats %.1f to %.1f, its other notes %.1f to %.1f\n", downLow, downHigh, offLow, offHigh);
    std::printf ("  Balance 0 to +1: right hand %.1f to %.1f, left hand %.1f to %.1f\n", rightMid, rightFront, leftMid, leftBack);
}

static void variations()
{
    std::printf ("Variations: a seed gives the same accents every time, and never moves a note\n");
    for (double seed : { 42.0, 1234.0 })
    {
        Script s = script ("same-seed", 12);
        s.param (0, kParamVariation, seed);
        std::string why;
        expect (sameEvents (run (s), run (s), why), "seed %.0f twice differs (%s)", seed, why.c_str());
    }

    const std::vector<Note> plain = pairNotes (run (script ("plain", 12, 480)), "plain");
    std::map<std::pair<long long, int>, Note> plainAt;
    for (const Note& n : plain) plainAt[{ n.on, n.channel }] = n;
    int seeds = 0, changed = 0, velocityOnly = 0, lengths = 0, thinned = 0, extra = 0, moved = 0;
    for (double amount : { 0.5, 1.0 })
        for (int seed = 1; seed <= (amount == 0.5 ? 300 : 100); ++seed)
        {
            Script s = script ("seed", 12, 480);
            s.param (0, kParamVariation, seed).param (0, kParamAmount, amount);
            const std::vector<Note> notes = pairNotes (run (s), "seed");
            bool anyVelocity = false, anyLength = false;
            for (const Note& n : notes)
            {
                auto it = plainAt.find ({ n.on, n.channel });
                if (it == plainAt.end()) { ++extra; continue; }
                if (it->second.pitch != n.pitch) ++moved;
                if (it->second.velocity != n.velocity) anyVelocity = true;
                if (it->second.off != n.off) anyLength = true;
            }
            const bool fewer = notes.size() < plain.size();
            thinned += fewer ? 1 : 0;
            lengths += anyLength ? 1 : 0;
            velocityOnly += (anyVelocity && ! anyLength && ! fewer) ? 1 : 0;
            changed += (anyVelocity || anyLength || fewer) ? 1 : 0;
            ++seeds;
        }
    expect (extra == 0, "variations added %d notes at new times", extra);
    expect (moved == 0, "variations changed %d pitches", moved);
    expect (changed > seeds * 9 / 10, "only %d of %d seeds changed anything", changed, seeds);
    std::printf ("  %d seeds: %d change the playing, %d of them the velocities only, %d the lengths, %d thin the notes;\n"
                 "  no note on moved or changed pitch (%d new, %d moved)\n", seeds, changed, velocityOnly, lengths, thinned, extra, moved);

    // A new variation starts on the next bar line; a stopped transport takes it at once.
    Script plainRun = script ("plain", 8);
    Script full = script ("full", 8);
    full.param (0, kParamVariation, 42);
    Script mid = script ("mid-bar", 8);
    mid.param (2 * kBar + 48000, kParamVariation, 42);
    const std::vector<Ev> a = onsOnly (run (plainRun)), b = onsOnly (run (full)), c = onsOnly (run (mid));
    std::vector<Ev> want;
    for (const Ev& e : a) if (e.time < 3 * kBar) want.push_back (e);
    for (const Ev& e : b) if (e.time >= 3 * kBar) want.push_back (e);
    std::string why;
    expect (sameEvents (c, want, why), "a variation set in bar 3 does not start at bar 4 (%s)", why.c_str());
    dumped.push_back (mid);

    Script amountMid = script ("amount-mid-bar", 8);
    amountMid.param (0, kParamVariation, 42).param (4 * kBar + 30000, kParamAmount, 1.0);
    Script amountFull = script ("amount-full", 8);
    amountFull.param (0, kParamVariation, 42).param (0, kParamAmount, 1.0);
    const std::vector<Ev> d = onsOnly (run (amountMid)), f = onsOnly (run (amountFull));
    want.clear();
    for (const Ev& e : b) if (e.time < 5 * kBar) want.push_back (e);
    for (const Ev& e : f) if (e.time >= 5 * kBar) want.push_back (e);
    expect (sameEvents (d, want, why), "an Amount change in bar 5 does not start at bar 6 (%s)", why.c_str());

    Script stopped = script ("stopped", 8);
    stopped.param (2 * kBar + 48000, kParamVariation, 42).halt (2 * kBar + 48000, 2);
    const std::vector<Ev> g = onsOnly (run (stopped));
    want.clear();
    for (const Ev& e : a) if (e.time < 2 * kBar + 48000) want.push_back (e);
    for (const Ev& e : b) if (e.time >= 2 * kBar + 48000) want.push_back (e);
    expect (sameEvents (g, want, why), "a variation set while stopped does not apply at once (%s)", why.c_str());
    std::printf ("  set mid-bar while playing, a variation or an Amount starts at the next bar line; while stopped, at once\n");
}

static void keysAndModes()
{
    std::printf ("Key and Mode: the pitches\n");
    // C natural minor, by hand: Cm(add9) bars 1 to 3, Abmaj7 4 to 6, Fm9 7 to 9, Cm(add9) 10 to 12.
    struct Bar { int bar; int left[3]; };
    const Bar minor[4] = { { 1, { 48, 51, 55 } }, { 4, { 44, 48, 51 } }, { 7, { 41, 44, 48 } }, { 10, { 48, 51, 55 } } };
    const Bar major[4] = { { 1, { 48, 52, 55 } }, { 4, { 45, 48, 52 } }, { 7, { 41, 45, 48 } }, { 10, { 48, 52, 55 } } };
    for (int mode = 0; mode < 2; ++mode)
    {
        Script s = script ("c", 12);
        s.param (0, kParamMode, mode);
        std::map<long long, int> pitch[2];
        for (const Ev& e : onsOnly (run (s))) pitch[e.channel - 1][e.time] = e.note;
        const Bar* bars = mode == 0 ? minor : major;
        for (int k = 0; k < 4; ++k)
            for (int i = 0; i < 3; ++i)
            {
                const long long t = (bars[k].bar - 1) * kBar + i * kStep;
                expect (pitch[0][t] == bars[k].left[i], "C %s bar %d step %d: left %d, want %d", mode ? "major" : "minor",
                        bars[k].bar, i + 1, pitch[0][t], bars[k].left[i]);
            }
        const int right[4] = { 67, mode == 0 ? 63 : 64, 62, mode == 0 ? 63 : 64 };
        for (long long j = 0; j < 192; ++j)
            expect (pitch[1][j * kStep] == right[j % 4], "C %s step %lld: right %d", mode ? "major" : "minor", j, pitch[1][j * kStep]);
    }
    std::printf ("  C minor: left C3 Eb3 G3, Ab2 C3 Eb3, F2 Ab2 C3, C3 Eb3 G3; right G4 Eb4 D4 Eb4 throughout\n");
    std::printf ("  C major: left C3 E3 G3, A2 C3 E3, F2 A2 C3, C3 E3 G3; right G4 E4 D4 E4 throughout\n");

    // Every key and both modes, against the theory written out again: the root of each chord
    // placed in F2 to E3, its 3rd and 5th the next ones up; the tonic in F#3 to F4.
    const int steps[2][7] = { { 0, 2, 3, 5, 7, 8, 10 }, { 0, 2, 4, 5, 7, 9, 11 } };
    const int degrees[4] = { 0, 5, 3, 0 };
    int compared = 0;
    for (int mode = 0; mode < 2; ++mode)
        for (int key = 0; key < 12; ++key)
        {
            Script s = script ("keys", 12, 480);
            s.param (0, kParamMode, mode).param (0, kParamKey, key);
            std::map<long long, int> pitch[2];
            for (const Ev& e : onsOnly (run (s))) pitch[e.channel - 1][e.time] = e.note;
            for (int chord = 0; chord < 4; ++chord)
            {
                const int d = degrees[chord];
                int want[3];
                want[0] = 41 + ((key + steps[mode][d]) - 41 + 1200) % 12;
                for (int i = 1; i < 3; ++i)
                {
                    const int pc = (key + steps[mode][(d + 2 * i) % 7]) % 12;
                    int up = want[i - 1] + 1;
                    while (up % 12 != pc) ++up;
                    want[i] = up;
                }
                for (int i = 0; i < 3; ++i)
                {
                    const long long t = chord * 3 * kBar + i * kStep;
                    expect (pitch[0][t] == want[i], "key %d mode %d chord %d: left %d, want %d", key, mode, chord, pitch[0][t], want[i]);
                    ++compared;
                }
            }
            const int tonic = 54 + (key - 54 + 1200) % 12;
            const int figure[4] = { 4, 2, 1, 2 };
            for (long long j = 0; j < 16; ++j)
            {
                const int want = tonic + steps[mode][figure[j % 4]];
                expect (pitch[1][j * kStep] == want, "key %d mode %d step %lld: right %d, want %d", key, mode, j, pitch[1][j * kStep], want);
                ++compared;
            }
        }
    std::printf ("  all 12 keys in both modes: %d pitches as the theory says\n", compared);
}

static void rates()
{
    std::printf ("Rate: the step length of each choice\n");
    const char* names[5] = { "1/32", "1/16", "1/8", "dotted 1/8", "triplet 1/8" };
    const double beats[5] = { 0.125, 0.25, 0.5, 0.75, 1.0 / 3.0 };
    for (int rate = 0; rate < 5; ++rate)
        for (int sr = 0; sr < 2; ++sr)
        {
            Script s = script ("rate", 4, 64);
            s.param (0, kParamRate, rate);
            if (sr == 1) { s.sampleRate = 44100.0; s.bpm = 97.0; s.length = 4LL * 4 * 44100 * 60 / 97; }
            const double spb = s.sampleRate * 60.0 / s.bpm;
            const double length = (double) kLogicParams[kParamLength].init;
            std::vector<Note> right;
            for (const Note& n : pairNotes (run (s), "rate"))
                if (n.channel == 2) right.push_back (n);
            long long expected = 0;
            while (std::llround ((double) expected * beats[rate] * spb) < s.length) ++expected;
            expect ((long long) right.size() == expected, "%s: %zu notes, want %lld", names[rate], right.size(), expected);
            for (size_t j = 0; j < right.size(); ++j)
            {
                const double on = (double) j * beats[rate];
                expect (right[j].on == std::llround (on * spb), "%s: step %zu at %lld", names[rate], j, right[j].on);
                const double off = on + length * beats[rate];
                if (right[j].off < s.length) expect (right[j].off == std::llround (off * spb), "%s: step %zu ends at %lld", names[rate], j, right[j].off);
            }
            if (sr == 0)
                std::printf ("  %-11s a step every %6.0f samples at 120 BPM and 48 kHz (%zu notes in 4 bars), lasting 0.9 of it\n",
                             names[rate], beats[rate] * spb, right.size());
        }
    std::printf ("  and to the rounded sample at 97 BPM and 44.1 kHz\n");
}

int main (int argc, char** argv)
{
    blockSizes();
    songPosition();
    noStuckNotes();
    threeAgainstFour();
    tensionAndBalance();
    variations();
    keysAndModes();
    rates();

    if (argc == 3 && std::strcmp (argv[1], "--dump") == 0)
    {
        std::FILE* f = std::fopen (argv[2], "w");
        if (! f) { std::printf ("cannot write %s\n", argv[2]); return 1; }
        for (const Script& s : dumped) dump (f, s, run (s));
        std::fclose (f);
        std::printf ("Wrote %zu scripts for the WebAssembly comparison\n", dumped.size());
    }
    std::printf ("%s: %d checks, %d failed\n", failures == 0 ? "PASS" : "FAIL", checks, failures);
    return failures;
}
