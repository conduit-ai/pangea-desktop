// Two Hands: the first MIDI tool, a small Pattern Player (docs/midi-tools.md, "Two Hands").
//
// Two hands over one musical clock, 3 against 4 on one shared rate:
//   - the left hand plays the root, 3rd and 5th of the chord playing now, around C3, on
//     MIDI channel 1;
//   - the right hand plays a figure in scale degrees, the 5th, 3rd, 2nd and 3rd above the
//     tonic near C4 (G4 Eb4 D4 Eb4 in C minor), on channel 2.
// One progression by degree, 1(add9) 6(7) 4(9) 1(add9), three bars per chord, built from the
// mode. Accents come from rules, and a variation number reshapes them from the next bar line.
//
// Ported by hand from the Pattern Player engine by Lukasz Furman (pattern-player/core:
// pp_core.cpp, pp_theory.cpp, pp_presets.h), keeping its sauce and nothing more. Each part
// below names the function it comes from. test/gold.cpp checks it against that engine note
// for note; README.md lists what is left out and every intentional difference.
// The copyright holder also offers this whole file under the GNU Affero General Public License,
// version 3 or later, at your choice (25 November 2026): see shell/platform/web/FELT-LICENSE.md.
//
// The arithmetic is written one product per statement, so that no compiler fuses a multiply
// and an add: the native plugin and the browser then agree to the bit.
#include "logic_api.h"

// ------------------------------------------------------------------ the controls

// A MIDI tool drives no sound controls.
static const char* const kLogicControls[1] = { "" };
static const int kNumLogicControls = 0;

static const LogicParam kLogicParams[] = {
    { "Key",       0.0f, 11.0f,   0.0f,  1.0f,  "[style:menu{C;C#;D;Eb;E;F;F#;G;Ab;A;Bb;B}]" },
    { "Mode",      0.0f, 1.0f,    0.0f,  1.0f,  "[style:menu{Natural minor;Major}]" },
    { "Rate",      0.0f, 4.0f,    1.0f,  1.0f,  "[style:menu{1/32;1/16;1/8;dotted 1/8;triplet 1/8}]" },
    { "Accent 1",  0.0f, 127.0f,  68.0f, 1.0f,  "[style:bars][group:Left hand][playhead:0]" },
    { "Accent 2",  0.0f, 127.0f,  45.0f, 1.0f,  "[style:bars][group:Left hand][playhead:0]" },
    { "Accent 3",  0.0f, 127.0f,  53.0f, 1.0f,  "[style:bars][group:Left hand][playhead:0]" },
    { "Accent 1",  0.0f, 127.0f,  76.0f, 1.0f,  "[style:bars][group:Right hand][playhead:1]" },
    { "Accent 2",  0.0f, 127.0f,  50.0f, 1.0f,  "[style:bars][group:Right hand][playhead:1]" },
    { "Accent 3",  0.0f, 127.0f,  61.0f, 1.0f,  "[style:bars][group:Right hand][playhead:1]" },
    { "Accent 4",  0.0f, 127.0f,  46.0f, 1.0f,  "[style:bars][group:Right hand][playhead:1]" },
    { "Tension",   0.0f, 1.0f,    0.5f,  0.01f, "[style:knob]" },
    { "Balance",  -1.0f, 1.0f,    0.0f,  0.01f, "[style:knob]" },
    { "Length",    0.2f, 1.5f,    0.9f,  0.01f, "[style:knob]" },
    { "Variation", 0.0f, 9999.0f, 0.0f,  1.0f,  "[style:generate]" },
    { "Amount",    0.0f, 1.0f,    0.5f,  0.01f, "[style:knob]" },
};
static const int kNumLogicParams = 15;

namespace twohands
{

// Where each control sits in kLogicParams.
enum
{
    kParamKey = 0,
    kParamMode = 1,
    kParamRate = 2,
    kParamLeftAccent = 3,    // three bars
    kParamRightAccent = 6,   // four bars
    kParamTension = 10,
    kParamBalance = 11,
    kParamLength = 12,
    kParamVariation = 13,
    kParamAmount = 14
};

enum { kLeft = 0, kRight = 1, kHands = 2 };

const double kEps = 1e-7;   // pp_core.cpp

// ------------------------------------------------------------------ material
// Preset 0, "C minor - 3 vs 4 (brief)", as pp_theory.cpp defaultPattern() builds it:
// material 0, "3 vs 4 ostinato (brief)", and progression 0, "1-6-4-1 (brief)".

const int kLeftTokens[3] = { 0, 1, 2 };       // chord tones: root, 3rd, 5th
const int kRightTokens[4] = { 4, 2, 1, 2 };   // scale degrees: 5th, 3rd, 2nd, 3rd
const int kChordCenter = 48;                  // pp_theory.h: chord hands voice around C3,
const int kScaleCenter = 60;                  // scale hands count from the tonic near C4
const int kBarsPerChord = 3;                  // pp_presets.h: one chord per left-hand realignment
const float kContourDepth = 1.0f;             // pp_presets.h, fixed in Two Hands
const float kMeet = 0.3f;                     // pp_presets.h, fixed in Two Hands

// pp_theory.cpp kModes: the two kept, in the order of the Mode menu.
const int kModeSteps[2][7] = {
    { 0, 2, 3, 5, 7, 8, 10 },   // Natural minor
    { 0, 2, 4, 5, 7, 9, 11 },   // Major (Ionian)
};

// pp_theory.cpp kProgressions[0] and the colours of diatonicChord.
enum { kTriad = 0, kSeventh = 1, kAdd9 = 2, kNinth = 3 };
const int kProgressionDegrees[4] = { 0, 5, 3, 0 };
const int kProgressionColours[4] = { kAdd9, kSeventh, kNinth, kAdd9 };
const int kColourOffsets[4][5] = {   // scale steps above the degree, in the order chord tones count them
    { 0, 2, 4, -1, -1 },   // triad
    { 0, 2, 4, 6, -1 },    // seventh
    { 0, 2, 4, 8, -1 },    // add9: root, 3rd, 5th, 9th
    { 0, 2, 4, 6, 8 },     // ninth
};

// pp_theory.cpp rateBeats: the five kept, in the order of the Rate menu.
const double kRateBeats[5] = { 0.125, 0.25, 0.5, 0.75, 1.0 / 3.0 };

// ------------------------------------------------------------------ the pattern (pp_core.h)

struct Chord
{
    int tones[6];   // pitch classes, tones[0] = root
    int count;
};

struct Hand
{
    int tokens[4];        // chord-tone numbers (left) or scale degrees (right)
    int noteCount;
    bool chordTone;       // PitchMode::ChordTone with Voicing::Root, else PitchMode::ScaleDegree
    int center;           // register centre
    int channel;          // MIDI channel
    int accents[4];       // the accent contour, its own cycle
    int accentCount;
    float gate;           // note length as a fraction of a step
    float density;        // 0..1: the share of contour positions that sound
    int walkEvery;        // accent walk: the contour moves one place every N cycles (0 = never)
};

struct Pattern
{
    Hand hands[kHands];
    double pulseBeats;    // TimingMode::SharedPulse: the step for both hands, in beats
    Chord chords[4];
    int chordCount;
    int keyRoot;          // pitch class of the key
    int scale[7];         // semitones of the mode above the key
    float tension;        // weight of the bar against the free-floating cycles, 0..1
    float balance;        // -1 left in front .. +1 right in front
};

struct Attack
{
    double onBeat;        // song position in beats
    double lengthBeats;
    int pitch;
    int velocity;
    int accentPos;        // position in the accent contour, after the walk
    bool sounds;          // false when thinned by density
};

// ------------------------------------------------------------------ small maths (pp_core.cpp)

static long long floorDiv (long long a, long long b)
{
    long long q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
}

static int mod (long long a, long long b) { return (int) (a - floorDiv (a, b) * b); }

static float clampFloat (float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// The remainder of x over y for x >= 0 and y > 0, exact, as the fmod pp_core uses there.
static double fmodPositive (double x, double y)
{
    const double whole = (double) logic_floor (x / y);
    const double below = whole * y;
    double r = x - below;
    if (r < 0.0) r += y;
    if (r >= y) r -= y;
    return r;
}

// A control value in its range; anything that is not a number reads as the minimum.
static float clampParam (float x, float lo, float hi) { return x >= lo ? (x <= hi ? x : hi) : lo; }

static int wholeParam (float x, int lo, int hi)
{
    if (! (x >= (float) lo)) return lo;
    if (x > (float) hi) return hi;
    return logic_clamp ((int) logic_round ((double) x), lo, hi);
}

// ------------------------------------------------------------------ attacks (pp_core.cpp)

// placeNear: the pitch of class pc in the window [center - 7, center + 4].
static int placeNear (int pc, int center)
{
    const int base = center - 7;
    return base + mod (pc - base, 12);
}

// barWeight: the accent of a place in the bar. Downbeat 1, half bar 0.6, beats 0.35,
// 8ths 0.15, the rest 0.
static bool onGrid (double posInBar, double grid)
{
    const double r = fmodPositive (posInBar + kEps, grid);
    return r < 2.0 * kEps;
}

static double barWeight (double posInBar, double beatsPerBar)
{
    if (posInBar < kEps || beatsPerBar - posInBar < kEps) return 1.0;
    const double half = beatsPerBar * 0.5;
    if (fmodPositive (beatsPerBar, 2.0) < kEps && logic_abs (posInBar - half) < kEps) return 0.6;
    if (onGrid (posInBar, 1.0)) return 0.35;
    if (onGrid (posInBar, 0.5)) return 0.15;
    return 0.0;
}

// contourRank: the rank of contour position a, 0 = strongest; ties go to the earlier position.
static int contourRank (const Hand& h, int a)
{
    int rank = 0;
    for (int i = 0; i < h.accentCount; ++i)
        if (h.accents[i] > h.accents[a] || (h.accents[i] == h.accents[a] && i < a)) ++rank;
    return rank;
}

// isCycleStartAt: whether a hand starts its note cycle at this beat (the anchor is 0).
static bool isCycleStartAt (const Pattern& p, int hand, double beat)
{
    const double jf = beat / p.pulseBeats;
    const double jr = (double) logic_floor (jf + 0.5);
    if (logic_abs (jf - jr) > 1e-6 || jr < 0.0) return false;
    const Hand& h = p.hands[hand];
    return h.noteCount > 0 && mod ((long long) jr, h.noteCount) == 0;
}

// chordAt: the progression, barsPerChord bars per chord, counted from bar 1 at song position 0.
static const Chord& chordAt (const Pattern& p, double beat, double beatsPerBar)
{
    const long long bar = logic_floor (beat / beatsPerBar + kEps);
    return p.chords[mod (floorDiv (bar, kBarsPerChord), p.chordCount)];
}

// resolvePitch: a chord-tone number or a scale degree as a MIDI note (the octave shift is 0).
static int resolvePitch (const Pattern& p, const Chord& chord, int hand, int token)
{
    const Hand& h = p.hands[hand];
    int pitch = token;
    if (h.chordTone && chord.count > 0)
    {
        // Voicing::Root: the root near the centre, each further tone the next one up.
        int stack[6];
        const int n = chord.count;
        stack[0] = placeNear (chord.tones[0], h.center);
        for (int i = 1; i < n; ++i)
        {
            const int step = mod (chord.tones[i] - stack[i - 1], 12);
            stack[i] = stack[i - 1] + (step == 0 ? 12 : step);
        }
        pitch = stack[mod (token, n)] + 12 * (int) floorDiv (token, n);
    }
    else if (! h.chordTone)
    {
        const int tonic = h.center - 6 + mod (p.keyRoot - (h.center - 6), 12);   // the tonic nearest the centre
        pitch = tonic + p.scale[mod (token, 7)] + 12 * (int) floorDiv (token, 7);
    }
    return logic_clamp (pitch, 0, 127);
}

// rawAttack and attackAt: everything about step j of a hand, a pure function of the pattern
// and j, so the song position alone decides what plays. Breath, the phrase arc, octave moves,
// the per-hand level and lean, and articulation are all at rest (0 or off) in Two Hands.
static Attack attackAt (const Pattern& p, int hand, long long j, double beatsPerBar)
{
    const Hand& h = p.hands[hand];
    const int other = 1 - hand;
    Attack a {};
    const double I = p.pulseBeats;
    a.onBeat = (double) j * I;
    a.lengthBeats = logic_clamp ((double) h.gate, 0.05, 1.5) * I;

    const int P = h.noteCount;
    const int A = h.accentCount;
    const int notePos = mod (j, P);
    const long long cycle = floorDiv (j, P);
    const long long walk = h.walkEvery > 0 ? floorDiv (cycle, h.walkEvery) : 0;
    a.accentPos = mod (j - walk, A);
    const bool cycleStart = notePos == 0;
    const bool meet = cycleStart && isCycleStartAt (p, other, a.onBeat);   // both hands restart here

    a.pitch = resolvePitch (p, chordAt (p, a.onBeat, beatsPerBar), hand, h.tokens[notePos]);

    // Density keeps the strongest contour positions; cycle starts and meeting points always stay.
    const double kept = logic_clamp ((double) h.density, 0.0, 1.0) * A;
    const int keep = logic_clamp ((int) logic_ceil (kept - 1e-9), 1, A);
    a.sounds = cycleStart || meet || contourRank (h, a.accentPos) < keep;

    // Velocity: the named rule layers.
    double mean = 0;
    for (int i = 0; i < A; ++i) mean += h.accents[i];
    mean /= A;
    const double fg = hand == kRight ? p.balance : -p.balance;           // +1 = this hand in front
    const double spread = 0.6 * fg;
    const double contrast = (double) kContourDepth * (1.0 + spread);
    const double posInBar = fmodPositive (a.onBeat + kEps, beatsPerBar) - kEps;
    const double front = 10.0 * fg;
    double v = mean + front;
    const double own = contrast * ((double) h.accents[a.accentPos] - mean);   // the hand on its own cycle
    v += own;
    const double weight = barWeight (posInBar < 0 ? 0 : posInBar, beatsPerBar);
    const double barScale = (double) p.tension * 14.0;
    const double bar = barScale * (weight - 0.3);                             // the bar
    v += bar;
    if (meet)
    {
        const double together = (double) kMeet * 14.0;                        // the meeting points
        v += together;
    }

    if (a.lengthBeats < 1.0 / 64.0) a.lengthBeats = 1.0 / 64.0;
    a.velocity = logic_clamp ((int) logic_round (v), 1, 127);
    return a;
}

// ------------------------------------------------------------------ pattern from the controls
// pp_theory.cpp setKeyMode, diatonicChord, setProgression and applyMaterial, and the
// expression controls of PluginProcessor::buildPattern.

static Chord diatonicChord (const Pattern& p, int degree, int colour)
{
    Chord c {};
    for (int k = 0; k < 5 && c.count < 6; ++k)
    {
        const int off = kColourOffsets[colour][k];
        if (off < 0) break;
        const int pc = mod (p.keyRoot + p.scale[mod (degree + off, 7)], 12);
        bool dup = false;
        for (int i = 0; i < c.count; ++i)
            if (c.tones[i] == pc) dup = true;
        if (! dup) c.tones[c.count++] = pc;
    }
    return c;
}

static Pattern buildPattern (const float* params)
{
    Pattern p {};
    p.pulseBeats = kRateBeats[wholeParam (params[kParamRate], 0, 4)];
    p.keyRoot = wholeParam (params[kParamKey], 0, 11);
    const int mode = wholeParam (params[kParamMode], 0, 1);
    for (int i = 0; i < 7; ++i) p.scale[i] = kModeSteps[mode][i];
    p.chordCount = 4;
    for (int i = 0; i < 4; ++i) p.chords[i] = diatonicChord (p, kProgressionDegrees[i], kProgressionColours[i]);

    const float gate = clampParam (params[kParamLength], 0.2f, 1.5f);
    for (int hand = 0; hand < kHands; ++hand)
    {
        Hand& h = p.hands[hand];
        const bool left = hand == kLeft;
        h.noteCount = left ? 3 : 4;
        h.accentCount = h.noteCount;
        h.chordTone = left;
        h.center = left ? kChordCenter : kScaleCenter;
        h.channel = left ? 1 : 2;
        for (int i = 0; i < h.noteCount; ++i)
        {
            h.tokens[i] = left ? kLeftTokens[i] : kRightTokens[i];
            // An accent of 0 counts as 1, as applyMaterial clamps it.
            h.accents[i] = wholeParam (params[(left ? kParamLeftAccent : kParamRightAccent) + i], 1, 127);
        }
        h.gate = gate;
        h.density = 1.0f;
        h.walkEvery = 0;
    }
    p.tension = clampParam (params[kParamTension], 0.0f, 1.0f);
    p.balance = clampParam (params[kParamBalance], -1.0f, 1.0f);
    return p;
}

// ------------------------------------------------------------------ variations (pp_core.cpp)
// A variation is a short list of bounded edits to the expression, fully decided by its seed.
// Variation N here is seed N of Pattern Player, generated exactly as there with notes locked.
// Its Lean moves are generated but never applied: they would move a hand early or late.

enum { kRotateContour = 0, kSwapNeighbours, kEmphasize, kDensity, kLean, kGate, kWalk, kAlterNote };

struct Op
{
    int kind;
    int hand;
    int a;      // position or step
    float x;    // amount
};

struct Variation
{
    unsigned long long seed;   // 0 = no variation
    int count;
    Op ops[8];
};

// nextSeed: splitmix64.
static unsigned long long nextSeed (unsigned long long seed)
{
    seed += 0x9E3779B97F4A7C15ull;
    unsigned long long z = seed;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z = z ^ (z >> 31);
    return z ? z : 1;
}

struct Rng
{
    unsigned long long s;
    unsigned long long next() { s = nextSeed (s); return s; }
    double unit() { return (double) (next() >> 11) * (1.0 / 9007199254740992.0); }
    int below (int n) { return n > 0 ? (int) (next() % (unsigned long long) n) : 0; }
    double sign() { return (next() & 1) ? 1.0 : -1.0; }
};

// generateVariation with allowNotes off. Where pp_core draws two numbers in one expression,
// the sign comes first, as clang evaluates it there.
static Variation generateVariation (unsigned long long seed, const Pattern& base, float amount)
{
    Variation v {};
    v.seed = seed;
    if (seed == 0) return v;
    Rng r { seed };
    const double amt = logic_clamp ((double) amount, 0.0, 1.0);
    const double scaled = amt * 3.0;
    const double jitter = r.unit() * 0.999;
    const int count = 1 + (int) logic_floor (scaled + jitter);
    // Accent moves weigh most; the two AlterNote entries are only drawn when notes are unlocked.
    const int kinds[10] = { kRotateContour, kSwapNeighbours, kEmphasize, kEmphasize, kDensity,
                            kLean, kGate, kWalk, kAlterNote, kAlterNote };
    const int choices = 8;
    for (int i = 0; i < count && v.count < 8; ++i)
    {
        Op op {};
        for (int tries = 0; tries < 8; ++tries)   // one move per kind and hand
        {
            op.kind = kinds[r.below (choices)];
            op.hand = r.below (2);
            bool dup = false;
            for (int k = 0; k < v.count; ++k)
                if (v.ops[k].kind == op.kind && v.ops[k].hand == op.hand) dup = true;
            if (! dup) break;
        }
        const Hand& h = base.hands[op.hand];
        if (op.kind == kRotateContour)
        {
            op.a = r.sign() > 0 ? 1 : -1;
        }
        else if (op.kind == kSwapNeighbours)
        {
            op.a = r.below (h.accentCount > 1 ? h.accentCount : 1);
        }
        else if (op.kind == kEmphasize)
        {
            op.a = r.below (h.accentCount > 0 ? h.accentCount : 1);
            const double sign = r.sign();
            const double size = 16.0 * amt * r.unit();
            op.x = (float) (sign * (6.0 + size));
        }
        else if (op.kind == kDensity)   // thinning only: the structure stays
        {
            const double size = 0.3 * amt * r.unit();
            op.x = (float) (-(0.1 + size));
        }
        else if (op.kind == kLean)
        {
            const double sign = r.sign();
            const double size = 12.0 * amt * r.unit();
            op.x = (float) (sign * (3.0 + size));
        }
        else if (op.kind == kGate)
        {
            const double sign = r.sign();
            const double size = 0.4 * amt * r.unit();
            op.x = (float) (sign * (0.1 + size));
        }
        else if (op.kind == kWalk)
        {
            op.a = 1 + r.below (4);
        }
        v.ops[v.count++] = op;
    }
    return v;
}

// applyVariation, without Lean (timing) and AlterNote (notes, never generated here).
static void applyVariation (Pattern& p, const Variation& v)
{
    for (int i = 0; i < v.count; ++i)
    {
        const Op& op = v.ops[i];
        Hand& h = p.hands[op.hand % kHands];
        const int A = h.accentCount > 0 ? h.accentCount : 1;
        if (op.kind == kRotateContour)
        {
            int moved[4];
            for (int k = 0; k < A; ++k) moved[mod (k + op.a, A)] = h.accents[k];
            for (int k = 0; k < A; ++k) h.accents[k] = moved[k];
        }
        else if (op.kind == kSwapNeighbours)
        {
            const int a = mod (op.a, A);
            const int b = mod (op.a + 1, A);
            const int t = h.accents[a];
            h.accents[a] = h.accents[b];
            h.accents[b] = t;
        }
        else if (op.kind == kEmphasize)
        {
            const int a = mod (op.a, A);
            const float sum = (float) h.accents[a] + op.x;   // in float, as pp_core adds them
            h.accents[a] = logic_clamp ((int) logic_round ((double) sum), 1, 127);
        }
        else if (op.kind == kDensity)
        {
            h.density = clampFloat (h.density + op.x, 0.0f, 1.0f);
        }
        else if (op.kind == kGate)
        {
            h.gate = clampFloat (h.gate + op.x, 0.05f, 1.5f);
        }
        else if (op.kind == kWalk)
        {
            h.walkEvery = op.a;
        }
    }
}

// ------------------------------------------------------------------ the engine (pp_core.cpp Engine)

struct Engine
{
    enum { kMaxActive = 64, kMaxPending = 256, kMaxShows = 64 };

    // A note that sounds, and when it started and ends, in beats.
    struct Active
    {
        bool used;
        int channel, note, hand;
        double onPpq, offPpq;
    };

    // A note start waiting to be played in time order.
    struct Pending
    {
        long long s;   // absolute sample
        double onPpq, length;
        int channel, pitch, velocity, hand;
    };

    Active active[kMaxActive] {};
    Pending pending[kMaxPending] {};
    LogicEvent shows[kMaxShows] {};   // this block of display events, added after the notes
    bool wasPlaying = false;
    double expectedPpq = 0.0;

    // The variation playing now. A changed Variation or Amount waits for the next bar line,
    // as in PluginProcessor::processBlock.
    bool haveVariation = false;
    unsigned long long playingSeed = 0;
    float playingAmount = 0.0f;
    Variation playing {};

    // The block being written.
    LogicEvent* out = nullptr;
    int maxOut = 0;
    int count = 0;
    int numShows = 0;

    void prepare (double)
    {
        for (int i = 0; i < kMaxActive; ++i) active[i].used = false;
        wasPlaying = false;
        expectedPpq = 0.0;
        haveVariation = false;
        playingSeed = 0;
        playingAmount = 0.0f;
        playing = Variation {};
    }

    void adopt (unsigned long long seed, float amount, const Pattern& base)
    {
        playing = generateVariation (seed, base, amount);
        playingSeed = seed;
        playingAmount = amount;
        haveVariation = true;
    }

    void noteOff (Active& a, long long offset)
    {
        if (count >= maxOut) return;
        out[count++] = { (int) offset, LOGIC_NOTE_OFF, 0.0f, a.note, a.channel };
        a.used = false;
    }

    // flush: note offs for everything sounding.
    void flush (long long offset)
    {
        for (int i = 0; i < kMaxActive; ++i)
            if (active[i].used) noteOff (active[i], offset);
    }

    // startNote: a unison at the same instant on one channel merges into one note (the longer
    // length wins); a later start of a sounding pitch ends the old note first.
    void startNote (const Pending& q, long long blockStart, long long from, double spb)
    {
        if (count >= maxOut - 1) return;
        const long long s = q.s;
        double offPpq = q.onPpq + q.length;
        bool merged = false;
        for (int i = 0; i < kMaxActive; ++i)
        {
            Active& a = active[i];
            if (! a.used || a.channel != q.channel || a.note != q.pitch) continue;
            const long long aOn = logic_round (a.onPpq * spb);
            if (aOn == s)
            {
                if (offPpq > a.offPpq) a.offPpq = offPpq;
                merged = true;
            }
            else if (aOn > s)
            {
                if (a.onPpq < offPpq) offPpq = a.onPpq;
            }
            else
            {
                const long long aOff = logic_round (a.offPpq * spb);
                noteOff (a, logic_clamp (aOff, from, s) - blockStart);
            }
        }
        if (merged || count >= maxOut) return;
        int slot = -1;
        for (int i = 0; i < kMaxActive && slot < 0; ++i)
            if (! active[i].used) slot = i;
        if (slot < 0) return;   // the table is full: skip the note, never hang one
        out[count++] = { (int) (s - blockStart), LOGIC_NOTE_ON, (float) q.velocity, q.pitch, q.channel };
        active[slot] = { true, q.channel, q.pitch, q.hand, q.onPpq, offPpq };
    }

    // Engine::process from the hands to the note offs, for the samples [from, to) of a block
    // that starts at blockStart. Samples count from song position 0.
    void play (const Pattern& p, long long blockStart, long long from, long long to, double ppqStart, double spb, double bpb)
    {
        if (to <= from) return;
        const double ppqFrom = ppqStart + (double) (from - blockStart) / spb;
        const double ppqTo = ppqStart + (double) (to - blockStart) / spb;
        int np = 0;
        for (int hand = 0; hand < kHands; ++hand)
        {
            const Hand& h = p.hands[hand];
            const double I = p.pulseBeats;
            long long j = logic_floor (ppqFrom / I) - 2;
            const long long jEnd = logic_ceil (ppqTo / I) + 2;
            if (j < 0) j = 0;
            for (; j <= jEnd; ++j)
            {
                const Attack at = attackAt (p, hand, j, bpb);
                const double onPpq = at.onBeat;   // soundingBeat, with no lean and no breath
                const long long s = logic_round (onPpq * spb);
                if (s < from || s >= to) continue;
                // Display slot 0 is the left hand, slot 1 the right: the accent playing now.
                if (numShows < kMaxShows)
                    shows[numShows++] = { (int) (s - blockStart), LOGIC_DISPLAY, (float) at.accentPos, hand, h.channel };
                if (! at.sounds) continue;
                if (np < kMaxPending)
                    pending[np++] = { s, onPpq, at.lengthBeats, h.channel, at.pitch, at.velocity, hand };
            }
        }

        // In time order, the left hand first at one sample (a stable insertion sort).
        for (int i = 1; i < np; ++i)
            for (int k = i; k > 0 && (pending[k].s < pending[k - 1].s
                                      || (pending[k].s == pending[k - 1].s && pending[k].hand < pending[k - 1].hand)); --k)
            {
                const Pending t = pending[k];
                pending[k] = pending[k - 1];
                pending[k - 1] = t;
            }
        for (int i = 0; i < np; ++i) startNote (pending[i], blockStart, from, spb);

        // Note offs that fall here. A late one, after a tempo change, goes out at once.
        for (int i = 0; i < kMaxActive; ++i)
        {
            Active& a = active[i];
            if (! a.used) continue;
            const long long s = logic_round (a.offPpq * spb);
            if (s < to) noteOff (a, logic_clamp (s, from, to - 1) - blockStart);
        }
    }

    // Sorted by offset: note offs, then display events, then note ons at one sample.
    static int rankOf (const LogicEvent& e)
    {
        if (e.control == LOGIC_NOTE_OFF) return 0;
        return e.control == LOGIC_DISPLAY ? 1 : 2;
    }

    int finish()
    {
        for (int i = 0; i < numShows && count < maxOut; ++i) out[count++] = shows[i];
        for (int i = 1; i < count; ++i)
        {
            const LogicEvent e = out[i];
            int j = i - 1;
            while (j >= 0 && (out[j].offset > e.offset || (out[j].offset == e.offset && rankOf (out[j]) > rankOf (e))))
            {
                out[j + 1] = out[j];
                --j;
            }
            out[j + 1] = e;
        }
        return count;
    }

    int process (const LogicBlock& b, LogicEvent* events, int maxEvents)
    {
        out = events;
        maxOut = maxEvents;
        count = 0;
        numShows = 0;

        const Pattern base = buildPattern (b.params);
        // The variation the controls ask for. 0 is none, whatever the Amount.
        const unsigned long long seed = (unsigned long long) wholeParam (b.params[kParamVariation], 0, 9999);
        const float amount = seed == 0 ? 0.0f : clampParam (b.params[kParamAmount], 0.0f, 1.0f);
        if (! haveVariation) adopt (seed, amount, base);   // the first block: at once
        const bool incoming = seed != playingSeed || amount != playingAmount;

        // Stopped, or nothing to go by: a tempo, sample rate or position that is not a number
        // counts as stopped too, so a broken host can only end notes, never hang one.
        const bool sane = b.bpm > 0.0 && b.sampleRate > 0.0 && b.ppqStart > -1e12 && b.ppqStart < 1e12;
        if (! b.playing || ! sane)
        {
            if (incoming) adopt (seed, amount, base);       // stopped: at once
            if (wasPlaying) flush (0);
            wasPlaying = false;
            return finish();
        }

        const double spb = b.sampleRate * 60.0 / b.bpm;
        const double ppqEnd = b.ppqStart + (double) b.numSamples / spb;
        const long long blockStart = logic_round (b.ppqStart * spb);
        const long long blockEnd = blockStart + b.numSamples;
        const double bpb = b.beatsPerBar > 0.0 ? b.beatsPerBar : 4.0;

        if (wasPlaying && logic_abs (b.ppqStart - expectedPpq) > 1e-3)   // a loop or a jump: nothing may hang
            flush (0);

        // A waiting variation starts on the next bar line, splitting the block there.
        long long split = blockEnd;
        if (incoming)
        {
            const double bar = (double) logic_ceil (b.ppqStart / bpb - 1e-9) * bpb;
            if (bar < ppqEnd) split = logic_clamp (logic_round (bar * spb), blockStart, blockEnd);
        }
        if (incoming && split < blockEnd)
        {
            Pattern before = base;
            applyVariation (before, playing);
            play (before, blockStart, blockStart, split, b.ppqStart, spb, bpb);
            adopt (seed, amount, base);
            Pattern after = base;
            applyVariation (after, playing);
            play (after, blockStart, split, blockEnd, b.ppqStart, spb, bpb);
        }
        else
        {
            Pattern now = base;
            applyVariation (now, playing);
            play (now, blockStart, blockStart, blockEnd, b.ppqStart, spb, bpb);
        }

        expectedPpq = ppqEnd;
        wasPlaying = true;
        return finish();
    }
};

}   // namespace twohands

struct Logic : twohands::Engine
{
};
