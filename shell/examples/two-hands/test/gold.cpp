// The gold test (README.md, "How it was verified"): Two Hands against the Pattern Player
// engine itself, built from its own folder and never copied in. test/run.sh builds it as
//
//   clang++ -std=c++17 -O2 -I shell/platform -I shell/examples/two-hands -I <pattern-player>/core \
//           test/gold.cpp <pattern-player>/core/pp_core.cpp <pattern-player>/core/pp_theory.cpp
//
// The Pattern Player side is set up as PluginProcessor::buildPattern does, with breath, echo,
// the phrase arc, articulation, harmony from held notes and the chord map all off. A new
// variation waits for the next bar line as PluginProcessor::processBlock does it, splitting
// the block there. Exit status: the number of failures.
#include "runner.h"
#include "pp_core.h"
#include "pp_presets.h"
#include "pp_theory.h"
#include <cmath>
#include <cstdarg>

using namespace runner;
using namespace twohands;

static int failures = 0;

static void expect (bool ok, const char* format, ...)
{
    if (ok) return;
    ++failures;
    std::printf ("  FAIL ");
    va_list args;
    va_start (args, format);
    std::vprintf (format, args);
    va_end (args);
    std::printf ("\n");
}

static const long long kBar = 96000;

static Script script (const char* name, double bars, int block = 128)
{
    Script s;
    s.name = name;
    s.block = block;
    s.length = (long long) std::llround (bars * (double) kBar);
    return s;
}

// ------------------------------------------------------------------ Pattern Player as a host

static int choice (float value, int count)
{
    const long v = std::lround (value);
    return (int) (v < 0 ? 0 : (v > count - 1 ? count - 1 : v));
}

struct PatternPlayerHost
{
    bool presetAsIs;        // preset 0 itself (the right hand as literal notes), any key ignored
    bool keepLean;          // apply the Lean moves of a variation, as Pattern Player does
    float params[64] {};
    pp::Engine engine;
    pp::Variation playing {}, incoming {};
    bool hasIncoming = false, started = false;
    unsigned long long seed = 0;
    float amount = 0.0f;
    pp::Event buf[1024];

    PatternPlayerHost (bool asIs, bool lean) : presetAsIs (asIs), keepLean (lean)
    {
        engine.reset();
        for (int i = 0; i < kNumLogicParams; ++i) params[i] = kLogicParams[i].init;
    }

    void setParam (int index, double value) { params[index] = (float) value; }

    // PluginProcessor::buildPattern with the Two Hands controls.
    pp::Pattern base() const
    {
        pp::Pattern p = pp::preset (0);
        if (! presetAsIs)
        {
            pp::setKeyMode (p, choice (params[kParamKey], 12), choice (params[kParamMode], 2) == 0 ? 1 : 0);
            pp::setProgression (p, 0);
            pp::applyMaterial (p, pp::materialPreset (0));
        }
        const double rates[5] = { pp::rateBeats (10), pp::rateBeats (7), pp::rateBeats (4), pp::rateBeats (2), pp::rateBeats (6) };
        p.timing = pp::TimingMode::SharedPulse;
        p.pulseBeats = rates[choice (params[kParamRate], 5)];
        for (int h = 0; h < pp::kHands; ++h)
        {
            pp::Hand& hand = p.hands[h];
            for (int i = 0; i < hand.accentCount; ++i)
            {
                const int accent = choice (params[(h == 0 ? kParamLeftAccent : kParamRightAccent) + i], 128);
                hand.accents[i] = (uint8_t) (accent < 1 ? 1 : accent);   // applyMaterial keeps 1 to 127
            }
            hand.gate = params[kParamLength];
        }
        p.tension = params[kParamTension];
        p.balance = params[kParamBalance];
        p.breath = 0.0f;
        p.echoSource = 0;
        p.arcShape = pp::ArcShape::Flat;
        p.articulation = 0.0f;
        p.liveHarmony = false;
        return p;
    }

    pp::Variation variationFor (unsigned long long s, float a, const pp::Pattern& p) const
    {
        pp::Variation v = pp::generateVariation (s, p, a, false);
        if (keepLean) return v;
        pp::Variation kept = v;
        kept.count = 0;
        for (int i = 0; i < v.count; ++i)
            if (v.ops[i].kind != pp::OpKind::Lean) kept.ops[kept.count++] = v.ops[i];
        return kept;
    }

    void process (double sr, double bpm, double ppq, int n, bool isPlaying, double bpb, long long pos, std::vector<Ev>& events)
    {
        const pp::Pattern b0 = base();
        const unsigned long long wantSeed = (unsigned long long) choice (params[kParamVariation], 10000);
        const float wantAmount = wantSeed == 0 ? 0.0f : params[kParamAmount];
        if (! started)
        {
            playing = variationFor (wantSeed, wantAmount, b0);   // as a saved state comes back
            seed = wantSeed;
            amount = wantAmount;
            started = true;
        }
        else if (wantSeed != seed || wantAmount != amount)
        {
            incoming = variationFor (wantSeed, wantAmount, b0);
            seed = wantSeed;
            amount = wantAmount;
            hasIncoming = true;
        }

        const pp::Block b { sr, bpm, ppq, bpb, n, isPlaying };
        int split = 0;
        if (hasIncoming)
        {
            const double spb = sr * 60.0 / std::fmax (1.0, bpm);
            const double ppqEnd = ppq + n / spb;
            const double bar = std::ceil (ppq / bpb - 1e-9) * bpb;
            if (! isPlaying) split = 0;
            else if (bar < ppqEnd)
            {
                const long long s = std::llround (bar * spb) - std::llround (ppq * spb);
                split = (int) (s < 0 ? 0 : (s > n ? n : s));
            }
            else split = n;
        }
        int count = 0;
        auto part = [&] (const pp::Variation& v, int from, int length)
        {
            if (length <= 0) return;
            pp::Pattern pattern = b0;
            pp::applyVariation (pattern, v);
            pp::Block piece = b;
            piece.numSamples = length;
            if (from > 0) piece.ppqStart = ppq + from / (sr * 60.0 / std::fmax (1.0, bpm));
            const int got = engine.process (pattern, piece, false, buf + count, 1024 - count);
            for (int i = count; i < count + got; ++i) buf[i].offset += from;
            count += got;
        };
        if (hasIncoming && split < n)
        {
            part (playing, 0, split);
            playing = incoming;
            hasIncoming = false;
            part (playing, split, n - split);
        }
        else part (playing, 0, n);

        for (int i = 0; i < count; ++i)
        {
            const pp::Event& e = buf[i];
            const bool on = e.status == 0x90;
            events.push_back ({ pos + e.offset, on ? LOGIC_NOTE_ON : LOGIC_NOTE_OFF, e.note, e.channel, on ? (float) e.velocity : 0.0f,
                                e.offset, n, isPlaying });
        }
    }
};

static std::vector<Ev> runPatternPlayer (const Script& s, bool asIs = false, bool keepLean = false)
{
    static PatternPlayerHost* host = nullptr;
    if (! host) host = new PatternPlayerHost (asIs, keepLean);
    *host = PatternPlayerHost (asIs, keepLean);
    return runWith (s, *host);
}

// With --fused: Pattern Player was compiled with fused multiply-add (clang on Apple Silicon by
// default), which rounds a velocity that falls exactly on a half the other way now and then.
// Such a note may differ by 1 in velocity, and nothing else; each one is counted.
static bool fused = false;
static int halves = 0;

// Note ons and offs, compared in order: time, on or off, channel, pitch and velocity.
static bool compare (const Script& s, bool asIs, bool keepLean, int& notes, std::string& why)
{
    const std::vector<Ev> theirs = runPatternPlayer (s, asIs, keepLean);
    const std::vector<Ev> ours = notesOnly (run (s));
    notes = (int) onsOnly (theirs).size();
    if (theirs.size() != ours.size())
    {
        why = std::to_string (ours.size()) + " events against " + std::to_string (theirs.size());
        return false;
    }
    int tolerated = 0;
    for (size_t i = 0; i < ours.size(); ++i)
    {
        Ev mine = ours[i];
        if (fused && mine.control == LOGIC_NOTE_ON && std::fabs (mine.value - theirs[i].value) == 1.0f)
        {
            mine.value = theirs[i].value;
            if (sameEvent (mine, theirs[i])) { ++tolerated; continue; }
        }
        if (! sameEvent (ours[i], theirs[i]))
        {
            char text[200];
            std::snprintf (text, sizeof text, "event %zu: ours %lld %s ch%d %d v%g, theirs %lld %s ch%d %d v%g", i,
                           ours[i].time, ours[i].control == LOGIC_NOTE_ON ? "on" : "off", ours[i].channel, ours[i].note, (double) ours[i].value,
                           theirs[i].time, theirs[i].control == LOGIC_NOTE_ON ? "on" : "off", theirs[i].channel, theirs[i].note, (double) theirs[i].value);
            why = text;
            return false;
        }
    }
    halves += tolerated;
    return true;
}

static bool hasLean (unsigned long long seed, float amount)
{
    const pp::Variation v = pp::generateVariation (seed, pp::preset (0), amount, false);
    for (int i = 0; i < v.count; ++i)
        if (v.ops[i].kind == pp::OpKind::Lean) return true;
    return false;
}

int main (int argc, char** argv)
{
    fused = argc > 1 && std::string (argv[1]) == "--fused";
    long long totalNotes = 0;
    int totalRuns = 0;
    auto tally = [&] (const Script& s, bool asIs, bool keepLean, const char* label) -> bool
    {
        int notes = 0;
        std::string why;
        const bool same = compare (s, asIs, keepLean, notes, why);
        expect (same, "%s: %s", label, why.c_str());
        totalNotes += notes;
        ++totalRuns;
        return same;
    };

    std::printf ("Gold: Two Hands against the Pattern Player engine (pp_core, pp_theory)\n");

    // 1. Preset 0 as it is, at a shared 1/16 pulse.
    {
        int notes = 0;
        bool all = true;
        for (int block : { 128, 37, 1024 })
        {
            Script s = script ("preset-0", 24, block);
            all = tally (s, true, false, "preset 0") && all;
            notes = (int) onsOnly (runPatternPlayer (s, true)).size();
        }
        std::printf ("  preset 0, C minor - 3 vs 4 (brief), shared 1/16, 24 bars: %d notes, %s at blocks of 128, 37 and 1024\n",
                     notes, all ? "identical" : "DIFFERENT");
    }

    // 2. Every key, both modes, every rate, with Tension, Balance, Length and the accents moved.
    {
        int runs = 0, same = 0;
        long long notes = 0;
        for (int key = 0; key < 12; ++key)
            for (int mode = 0; mode < 2; ++mode)
                for (int rate = 0; rate < 5; ++rate)
                {
                    Script s = script ("sweep", 12, 256);
                    s.param (0, kParamKey, key).param (0, kParamMode, mode).param (0, kParamRate, rate)
                        .param (0, kParamTension, ((key * 7 + rate) % 11) / 10.0)
                        .param (0, kParamBalance, ((key * 3 + mode * 5 + rate) % 21 - 10) / 10.0)
                        .param (0, kParamLength, 0.2 + ((key + rate * 3) % 14) * 0.1);
                    for (int i = 0; i < 7; ++i) s.param (0, kParamLeftAccent + i, (key * 31 + i * 17 + rate * 5) % 128);
                    int n = 0;
                    std::string why;
                    const bool ok = compare (s, false, false, n, why);
                    expect (ok, "key %d mode %d rate %d: %s", key, mode, rate, why.c_str());
                    same += ok ? 1 : 0;
                    notes += n;
                    ++runs;
                }
        totalNotes += notes;
        totalRuns += runs;
        std::printf ("  12 keys x 2 modes x 5 rates, Tension, Balance, Length and accents moved: %d of %d runs identical, %lld notes\n",
                     same, runs, notes);
    }

    // 3. Variations: Two Hands variation N is Pattern Player seed N, with its Lean moves left out.
    {
        int runs = 0, same = 0, withLean = 0;
        long long notes = 0;
        for (float amount : { 0.0f, 0.25f, 0.5f, 1.0f })
            for (int seed = 1; seed <= (amount == 0.5f ? 300 : 100); ++seed)
            {
                Script s = script ("variation", 12, 480);
                s.param (0, kParamVariation, seed).param (0, kParamAmount, amount);
                if (seed % 3 == 0) s.param (0, kParamKey, 7).param (0, kParamMode, 1).param (0, kParamRate, 3);
                int n = 0;
                std::string why;
                const bool ok = compare (s, false, false, n, why);
                expect (ok, "seed %d amount %.2f: %s", seed, (double) amount, why.c_str());
                same += ok ? 1 : 0;
                withLean += hasLean ((unsigned long long) seed, amount) ? 1 : 0;
                notes += n;
                ++runs;
            }
        totalNotes += notes;
        totalRuns += runs;
        std::printf ("  seeds 1 to 300 at Amount 0.5 and 1 to 100 at 0, 0.25 and 1: %d of %d runs identical, %lld notes\n", same, runs, notes);
        std::printf ("  (%d of those variations have a Lean move, which Two Hands leaves out)\n", withLean);

        // With Lean kept, exactly the seeds that have one differ.
        int differ = 0, differWithLean = 0, sameWithLean = 0;
        for (int seed = 1; seed <= 300; ++seed)
        {
            Script s = script ("lean", 12, 480);
            s.param (0, kParamVariation, seed);
            int n = 0;
            std::string why;
            const bool ok = compare (s, false, true, n, why);
            const bool lean = hasLean ((unsigned long long) seed, 0.5f);
            differ += ok ? 0 : 1;
            differWithLean += (! ok && lean) ? 1 : 0;
            sameWithLean += (ok && lean) ? 1 : 0;
        }
        expect (differ == differWithLean, "%d seeds differ from Pattern Player with Lean kept, %d of them have a Lean move", differ, differWithLean);
        std::printf ("  against Pattern Player with Lean kept: %d of 300 seeds differ, all of them seeds with a Lean move\n", differ);
        if (sameWithLean > 0) std::printf ("  (%d seeds with a Lean move still match: their lean is too small to move a sample)\n", sameWithLean);

        std::printf ("  what three seeds do, at Amount 0.5:\n");
        for (unsigned long long seed : { 1ull, 42ull, 2026ull })
        {
            const pp::Variation v = pp::generateVariation (seed, pp::preset (0), 0.5f, false);
            std::printf ("    %4llu:", seed);
            for (int i = 0; i < v.count; ++i)
            {
                const pp::Op& op = v.ops[i];
                const char* hand = op.hand == pp::Left ? "left" : "right";
                std::printf ("%s ", i ? "," : "");
                if (op.kind == pp::OpKind::Emphasize) std::printf ("%s accent %d %+.1f", hand, op.a + 1, (double) op.x);
                else if (op.kind == pp::OpKind::Gate) std::printf ("%s length %+.2f", hand, (double) op.x);
                else if (op.kind == pp::OpKind::Density) std::printf ("%s density %+.2f", hand, (double) op.x);
                else if (op.kind == pp::OpKind::Lean) std::printf ("%s lean %+.1f ms (left out)", hand, (double) op.x);
                else if (op.kind == pp::OpKind::Walk) std::printf ("%s accents walk every %d cycles", hand, op.a);
                else if (op.kind == pp::OpKind::RotateContour) std::printf ("%s accents rotate %+d", hand, op.a);
                else if (op.kind == pp::OpKind::SwapNeighbours) std::printf ("%s accents %d and %d swap", hand, op.a + 1, op.a + 2);
                else std::printf ("%s %s", hand, pp::opName (op.kind));
            }
            std::printf ("\n");
        }
    }

    // 4. Changes while playing: a variation and an Amount set mid-bar wait for the bar line,
    //    one set while stopped applies at once; loops, jumps, tempo changes and stops.
    {
        std::vector<Script> scripts;
        Script switches = script ("switches", 16);
        switches.param (2 * kBar + 48000, kParamVariation, 42).param (5 * kBar + 1000, kParamAmount, 1.0)
            .param (7 * kBar + 95000, kParamVariation, 7).param (9 * kBar + 20000, kParamVariation, 0)
            .param (11 * kBar + 500, kParamVariation, 2026).halt (12 * kBar + 30000, 2)
            .param (13 * kBar + 128 * 7, kParamVariation, 99).halt (13 * kBar + 128 * 7, 1);
        scripts.push_back (switches);
        Script jumps = script ("jumps", 8);
        jumps.param (0, kParamVariation, 13).jump (2 * kBar + 50 * 128, 0.0).jump (4 * kBar, 22.3).jump (6 * kBar + 3000, 7.875);
        scripts.push_back (jumps);
        Script tempo = script ("tempo", 8);
        tempo.tempo (2 * kBar, 87.3).tempo (4 * kBar + 777, 160.0).param (5 * kBar, kParamLength, 1.5).tempo (6 * kBar, 61.0);
        scripts.push_back (tempo);
        Script live = script ("live", 12, 256);
        for (int bar = 0; bar < 12; ++bar)
        {
            live.param (bar * kBar + 3 * 256, kParamRate, (bar * 3) % 5);
            live.param (bar * kBar + 47000, kParamVariation, (bar * 37) % 50);
            live.param (bar * kBar + 61000, kParamLength, 0.2 + 0.13 * bar);
            live.param (bar * kBar + 77000, kParamKey, (bar * 5) % 12);
            live.param (bar * kBar + 85000, kParamRightAccent + bar % 4, (bar * 41) % 128);
            live.param (bar * kBar + 90000, kParamTension, (bar % 5) / 4.0);
        }
        scripts.push_back (live);
        Script other = script ("44k1-97bpm-3-4", 8, 37);
        other.sampleRate = 44100.0;
        other.bpm = 97.0;
        other.beatsPerBar = 3.0;
        other.length = 8LL * 3 * 44100 * 60 / 97;
        other.param (0, kParamRate, 4).param (0, kParamVariation, 1234).param (100000, kParamVariation, 77);
        scripts.push_back (other);
        int same = 0;
        for (const Script& s : scripts) same += tally (s, false, false, s.name.c_str()) ? 1 : 0;
        std::printf ("  changes while playing, loops and jumps, tempo changes, stops, 44.1 kHz in 3/4: %d of %zu identical\n",
                     same, scripts.size());
    }

    if (fused) std::printf ("  Pattern Player built with fused multiply-add: %d notes 1 apart in velocity, at an exact half\n", halves);
    std::printf ("%s: %d runs, %lld notes compared, %d failed\n", failures == 0 ? "PASS" : "FAIL", totalRuns, totalNotes, failures);
    return failures;
}
