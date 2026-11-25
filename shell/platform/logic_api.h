// Platform-owned interface for a logic module (sequencers, arpeggiators, MIDI tools).
// The model writes a struct Logic that fills this contract. Rules for logic code:
//   - plain C++ only: no #includes except this file, no allocation, no threads, no files, no network
//   - everything it needs arrives in LogicBlock; everything it does leaves as LogicEvents
//   - no array sized at run time, under 16 KB of locals in any one function, and no global that
//     needs code to make or end it: refused when it compiles (logic_native.flags, decision 46)
//   - in the native plugin every call runs within a step budget, and a module that runs too
//     long or calls itself too deeply is stopped for good (PangeaLogicGuard.h)
// That keeps it portable (native plugin and browser WebAssembly from the same file)
// and easy to check before signing.
//
// Version 2 (7 November 2026, the MIDI tools experiment, docs/midi-tools.md) adds notes out,
// the bar length, display hints for the controls and the maths a module may not include.
// Every version 1 module still compiles: the new fields sit at the end of each struct, so
// `{ offset, control, value }` and `{ name, min, max, init, step }` keep meaning the same.
#pragma once

#define PANGEA_LOGIC_API 2

struct LogicParam   // a user-facing control owned by the logic layer
{
    const char* name;
    float min, max, init, step;
    // Display hints as [key:value] pairs, or nullptr. docs/midi-tools.md, "Controls":
    //   [style:knob]  [style:menu{1/16;1/8;dotted 1/8}]  [style:bars]  [style:generate]
    //   [group:Left hand]  [unit:ms]  [playhead:0]
    // A menu lists its labels for min, min + 1, ... with no quotes (a module may not use an apostrophe).
    const char* meta;
};

// What an event does. A value of 0 or more is the index of a sound control (version 1).
enum
{
    LOGIC_NOTE_ON = -1,    // note `note` (0..127) on MIDI channel `channel` (1..16), velocity `value` (1..127)
    LOGIC_NOTE_OFF = -2,   // note `note` on channel `channel` off; `value` is ignored
    LOGIC_DISPLAY = -3     // display slot `note` (0..7) shows `value`, e.g. the step playing now
};

struct LogicEvent   // "at sample `offset` in this block, do `control`"
{
    int offset;
    int control;     // a sound control index, or one of the LOGIC_ values above
    float value;
    int note;        // LOGIC_NOTE_ON, LOGIC_NOTE_OFF: the note; LOGIC_DISPLAY: the slot
    int channel;     // LOGIC_NOTE_ON, LOGIC_NOTE_OFF: the MIDI channel, 1..16
};

struct LogicBlock   // everything the host knows about this block
{
    double sampleRate;
    double bpm;
    double ppqStart;     // musical position (in beats) of the block's first sample
    int numSamples;
    bool playing;
    const float* params; // current values of the logic's own controls, in declaration order
    double beatsPerBar;  // version 2: quarter notes per bar from the host's time signature (4 in 4/4)
};

// Events must be returned sorted by offset. maxEvents is always at least 256.
// A MIDI tool owns its notes: every LOGIC_NOTE_ON needs a LOGIC_NOTE_OFF, and when `playing`
// turns false the module sends the offs for everything still sounding in that same block.
// The platform also silences anything left over, but the check before saving fails a module
// that leaves a note on (docs/midi-tools.md, "The check").

// ------------------------------------------------------------------ maths
// A module may not include <cmath>, so the platform gives it these. Plain C++, no library,
// the same results natively and in WebAssembly (to well under a cent of a semitone).

static inline double logic_abs (double x) { return x < 0.0 ? -x : x; }
static inline int logic_abs (int x) { return x < 0 ? -x : x; }
static inline double logic_min (double a, double b) { return a < b ? a : b; }
static inline double logic_max (double a, double b) { return a > b ? a : b; }
static inline int logic_min (int a, int b) { return a < b ? a : b; }
static inline int logic_max (int a, int b) { return a > b ? a : b; }
static inline long long logic_abs (long long x) { return x < 0 ? -x : x; }
static inline long long logic_min (long long a, long long b) { return a < b ? a : b; }
static inline long long logic_max (long long a, long long b) { return a > b ? a : b; }
static inline double logic_clamp (double x, double lo, double hi) { return x < lo ? lo : (x > hi ? hi : x); }
static inline int logic_clamp (int x, int lo, int hi) { return x < lo ? lo : (x > hi ? hi : x); }
static inline long long logic_clamp (long long x, long long lo, long long hi) { return x < lo ? lo : (x > hi ? hi : x); }

// Largest whole number not above x, for |x| below 2^62.
static inline long long logic_floor (double x)
{
    long long i = (long long) x;
    return ((double) i > x) ? i - 1 : i;
}
static inline long long logic_ceil (double x) { return -logic_floor (-x); }
// Nearest whole number, halves away from zero.
static inline long long logic_round (double x) { return x < 0.0 ? -logic_floor (-x + 0.5) : logic_floor (x + 0.5); }
// x minus the largest multiple of y not above it: always in [0, y) for y > 0, unlike C's fmod.
static inline double logic_wrap (double x, double y)
{
    if (y <= 0.0) return 0.0;
    double r = x - (double) logic_floor (x / y) * y;
    return (r < 0.0) ? 0.0 : (r >= y ? 0.0 : r);
}
// Euclidean remainder for whole numbers: always in [0, n) for n > 0.
static inline long long logic_mod (long long a, long long n)
{
    if (n <= 0) return 0;
    long long r = a % n;
    return r < 0 ? r + n : r;
}

static const double LOGIC_PI = 3.14159265358979323846;

static inline double logic_sin (double x)
{
    x = logic_wrap (x + LOGIC_PI, 2.0 * LOGIC_PI) - LOGIC_PI;     // -pi .. pi
    if (x > 0.5 * LOGIC_PI) x = LOGIC_PI - x;                     // -pi/2 .. pi/2
    else if (x < -0.5 * LOGIC_PI) x = -LOGIC_PI - x;
    const double x2 = x * x;                                      // Taylor to x^15
    double term = x, sum = x;
    for (int k = 1; k <= 7; ++k)
    {
        term *= -x2 / (double) ((2 * k) * (2 * k + 1));
        sum += term;
    }
    return sum;
}
static inline double logic_cos (double x) { return logic_sin (x + 0.5 * LOGIC_PI); }

static inline double logic_exp (double x)
{
    if (x > 700.0) x = 700.0;
    if (x < -700.0) return 0.0;
    const double ln2 = 0.69314718055994530942;
    const long long k = logic_floor (x / ln2 + 0.5);              // x = k ln2 + r, |r| <= ln2/2
    const double r = x - (double) k * ln2;
    double term = 1.0, sum = 1.0;
    for (int n = 1; n <= 16; ++n)
    {
        term *= r / (double) n;
        sum += term;
    }
    double scale = 1.0;
    for (long long i = 0; i < (k < 0 ? -k : k); ++i) scale *= 2.0;
    return k < 0 ? sum / scale : sum * scale;
}
// Natural logarithm for x > 0 (0 for anything else).
static inline double logic_log (double x)
{
    if (x <= 0.0) return 0.0;
    const double ln2 = 0.69314718055994530942;
    int e = 0;
    while (x > 2.0) { x *= 0.5; ++e; }
    while (x < 1.0) { x *= 2.0; --e; }
    const double t = (x - 1.0) / (x + 1.0), t2 = t * t;           // ln x = 2 atanh t
    double term = t, sum = t;
    for (int k = 1; k <= 20; ++k)
    {
        term *= t2;
        sum += term / (double) (2 * k + 1);
    }
    return 2.0 * sum + (double) e * ln2;
}
static inline double logic_pow (double base, double exponent)
{
    if (base <= 0.0) return 0.0;
    return logic_exp (exponent * logic_log (base));
}

// A small deterministic random number generator (xorshift64*), so the same seed gives the
// same variation natively, in the browser and in the check.
struct LogicRandom
{
    unsigned long long state;
    explicit LogicRandom (unsigned long long seed) : state (seed ? seed : 0x9E3779B97F4A7C15ull) {}
    unsigned long long next()
    {
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;
        return state * 2685821657736338717ull;
    }
    double unit() { return (double) (next() >> 11) * (1.0 / 9007199254740992.0); }   // [0, 1)
    int below (int n) { return n > 0 ? (int) (next() % (unsigned long long) n) : 0; }
};
