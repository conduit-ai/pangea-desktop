// Ostinato Hands: two piano hands in polymeter, three against four by default, over chords in D
// dorian, built on Keys from the MIDI shelf. The MIDI shelf example for keys
// (docs/midi-shelf.md).
#include "logic_api.h"
// ---- MIDI components, filled in by Pangea: kit, keys (from bass-hand 1af3fcb, drum-hand e297ba1, pattern-player 7f99aa5, check 8d4f3051b9d8c31d). Change your own code, not this block.
// Copyright (c) 2026 Lukasz Furman. Made for Pangea. PolyForm Noncommercial 1.0.0 with a music-making exception, see library/midi/LICENSE.md in Pangea.
namespace shelf {
enum { Major, Minor, Dorian, Phrygian, Lydian, Mixolydian, Locrian, HarmonicMinor, MelodicMinor, PhrygianDominant, MajorPentatonic, MinorPentatonic };
enum { Triad, Seven, Add9, Nine, Sus2, Sus4 };
enum { FillNever, Every2, Every4, Every8, PhraseEnds, BeforeChange };
struct Key { int root; int scale[7]; int size; };
struct Chord { int tones[6]; int count; };
struct ChordAt { Chord now, next; bool hasNext; double nextAt; };
static const signed char kShelfModes[12][8] = { { 7, 0, 2, 4, 5, 7, 9, 11 }, { 7, 0, 2, 3, 5, 7, 8, 10 }, { 7, 0, 2, 3, 5, 7, 9, 10 },
{ 7, 0, 1, 3, 5, 7, 8, 10 }, { 7, 0, 2, 4, 6, 7, 9, 11 }, { 7, 0, 2, 4, 5, 7, 9, 10 }, { 7, 0, 1, 3, 5, 6, 8, 10 },
{ 7, 0, 2, 3, 5, 7, 8, 11 }, { 7, 0, 2, 3, 5, 7, 9, 11 }, { 7, 0, 1, 4, 5, 7, 8, 10 }, { 5, 0, 2, 4, 7, 9 }, { 5, 0, 3, 5, 7, 10 } };
static const signed char kShelfProgressions[13][9] = { { 4, 0, Add9, 5, Seven, 3, Nine, 0, Add9 }, { 1, 0, Add9 },
{ 4, 0, Add9, 6, Triad, 5, Seven, 6, Sus2 }, { 4, 0, Seven, 3, Seven, 4, Seven, 0, Add9 }, { 4, 0, Add9, 2, Seven, 6, Triad, 5, Seven },
{ 4, 0, Add9, 4, Sus4, 5, Seven, 3, Seven }, { 2, 0, Seven, 1, Seven }, { 4, 5, Seven, 3, Add9, 0, Add9, 4, Sus4 }, { 2, 0, Nine, 3, Nine },
{ 3, 0, Triad, 3, Triad, 2, Triad }, { 4, 0, Triad, 0, Triad, 3, Triad, 2, Triad }, { 4, 0, Triad, 5, Triad, 3, Triad, 2, Triad },
{ 4, 0, Nine, 2, Seven, 5, Nine, 3, Nine } };
static const signed char kShelfColours[6][5] = { { 0, 2, 4, -1 }, { 0, 2, 4, 6, -1 }, { 0, 2, 4, 8, -1 }, { 0, 2, 4, 6, 8 }, { 0, 1, 4, -1 }, { 0, 3, 4, -1 } };
inline long long floorDiv (long long a, long long b) { return a / b - ((a % b != 0) && ((a < 0) != (b < 0)) ? 1 : 0); }
inline int keySize (const Key& k) { return k.size >= 1 && k.size <= 7 ? k.size : 7; }
inline Key makeKey (int root, int mode) {
const signed char* m = kShelfModes[logic_mod (mode, 12)];
Key k;
k.root = (int) logic_mod (root, 12);
k.size = m[0];
for (int i = 0; i < 7; ++i) k.scale[i] = i < k.size ? m[1 + i] : m[k.size];
return k;
}
inline int degreeOf (const Key& k, int pitchClass) {
for (int d = 0; d < keySize (k); ++d) if (logic_mod (k.root + k.scale[d], 12) == logic_mod (pitchClass, 12)) return d;
return -1;
}
inline bool inKey (const Key& k, int pitchClass) { return degreeOf (k, pitchClass) >= 0; }
inline int scaleMove (const Key& k, int pitch, int steps) {
const int n = keySize (k), d = degreeOf (k, pitch);
return d < 0 ? pitch + steps : pitch - k.scale[d] + 12 * (int) floorDiv (d + steps, n) + k.scale[logic_mod (d + steps, n)];
}
inline Chord chordOf (const Key& k, int degree, int colour) {
Chord c;
c.count = 0;
for (int i = 0; i < 5 && kShelfColours[logic_mod (colour, 6)][i] >= 0; ++i) {
const int pc = (int) logic_mod (k.root + k.scale[logic_mod (degree + kShelfColours[logic_mod (colour, 6)][i], keySize (k))], 12);
bool twice = false;
for (int t = 0; t < c.count; ++t) twice = twice || c.tones[t] == pc;
if (! twice) c.tones[c.count++] = pc;
}
return c;
}
inline int sortEvents (LogicEvent* out, int n) {
for (int i = 1; i < n; ++i)
for (int k = i; k > 0; --k) {
const LogicEvent x = out[k], y = out[k - 1];
const int rx = x.control == LOGIC_NOTE_OFF ? 0 : (x.control == LOGIC_DISPLAY ? 1 : 2), ry = y.control == LOGIC_NOTE_OFF ? 0 : (y.control == LOGIC_DISPLAY ? 1 : 2);
if (! (x.offset < y.offset || (x.offset == y.offset && rx < ry))) break;
out[k] = y;
out[k - 1] = x;
}
return n;
}
struct Harmony {
Key key;
Chord chords[8], now, next;
int count, barsPerChord;
bool external, hasNext;
double nextAt;
void reset() { set (0, Minor, 0, 2); }
void set (int root, int mode, int progression, int bars) {
const signed char* p = kShelfProgressions[logic_mod (progression, 13)];
int degrees[8], colours[8];
for (int i = 0; i < p[0]; ++i) { degrees[i] = p[1 + 2 * i]; colours[i] = p[2 + 2 * i]; }
setChords (root, mode, degrees, colours, p[0], bars);
}
void setChords (int root, int mode, const int* degrees, const int* colours, int n, int bars) {
key = makeKey (root, mode);
count = logic_clamp (n, 1, 8);
for (int i = 0; i < count; ++i) chords[i] = i < n ? chordOf (key, degrees[i], colours[i]) : chordOf (key, 0, Triad);
barsPerChord = logic_clamp (bars, 1, 8);
external = hasNext = false;
nextAt = 0.0;
now.count = next.count = 0;
}
void follow (const Chord& nowChord, const Chord& nextChord, bool knowsNext, double nextChordAt) {
external = true; now = nowChord; next = nextChord; hasNext = knowsNext; nextAt = nextChordAt;
}
ChordAt at (double beat, double beatsPerBar) const {
ChordAt out;
out.now.count = out.next.count = 0;
out.hasNext = false;
out.nextAt = 0.0;
if (external) {
out.now = hasNext && beat >= nextAt - 1e-7 ? next : now;
out.next = next;
out.hasNext = hasNext && beat < nextAt - 1e-7;
out.nextAt = nextAt;
} else {
const double span = (beatsPerBar > 0 ? beatsPerBar : 4.0) * (barsPerChord > 0 ? barsPerChord : 1);
const long long index = logic_floor (beat / span + 1e-7);
if (count > 0) { out.now = chords[logic_mod (index, logic_clamp (count, 1, 8))]; out.next = chords[logic_mod (index + 1, logic_clamp (count, 1, 8))]; }
out.hasNext = count > 1;
out.nextAt = (double) (index + 1) * span;
}
if (out.now.count == 0) { out.now.count = 1; out.now.tones[0] = key.root; }
if (out.next.count == 0) out.next = out.now;
return out;
}
};
struct Feel {
float level, density, depth, tension, fatigue, lateMs, swing8, swing16, human, humanMs, humanVel, intensity;
unsigned int seed;
void reset() {
level = 0.0f; density = 1.0f; depth = 1.0f; tension = 0.5f; fatigue = 1.5f; lateMs = 0.0f; swing8 = swing16 = 0.5f;
human = 0.2f; humanMs = 7.0f; humanVel = 7.0f; intensity = 0.5f; seed = 1;
}
static double posInBar (double beat, double beatsPerBar) {
const double bpb = beatsPerBar > 0 ? beatsPerBar : 4.0, pos = beat - (double) logic_floor (beat / bpb + 1e-7) * bpb;
return pos < 0 ? 0 : pos;
}
static double barWeight (double pos, double beatsPerBar) {
if (pos < 1e-7 || beatsPerBar - pos < 1e-7) return 1.0;
if (logic_wrap (beatsPerBar, 2.0) < 1e-7 && logic_abs (pos - beatsPerBar * 0.5) < 1e-7) return 0.6;
return logic_wrap (pos + 1e-7, 1.0) < 2e-7 ? 0.35 : (logic_wrap (pos + 1e-7, 0.5) < 2e-7 ? 0.15 : 0.0);
}
static bool swingable (double stepBeats) {
for (double g = 0.125; g <= 4.0 + 1e-9; g *= 2.0) if (logic_abs (stepBeats - g) < 1e-9) return true;
return false;
}
static double warp (double u, double m) { return u < 0.5 ? u * (m / 0.5) : m + (u - 0.5) * ((1.0 - m) / 0.5); }
static double swung (double beat, double swing8, double swing16) {
const double m8 = logic_clamp (swing8, 0.5, 0.75), m16 = logic_clamp (swing16, 0.5, 0.75);
if (beat < 0 || (m8 == 0.5 && m16 == 0.5)) return beat;
const double base = (double) logic_floor (beat + 1e-9);
double t = logic_clamp (beat - base, 0.0, 1.0);
if (m16 != 0.5) {
const double half = t >= 0.5 - 1e-9 ? 0.5 : 0.0;
t = half + 0.5 * warp (logic_clamp ((t - half) / 0.5, 0.0, 1.0), m16);
}
return base + (m8 != 0.5 ? warp (t, m8) : t);
}
static unsigned long long nextSeed (unsigned long long seed) {
unsigned long long z = seed + 0x9E3779B97F4A7C15ull;
z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
z = z ^ (z >> 31);
return z ? z : 1;
}
static void micro (unsigned int seed, long long step, double& u1, double& u2) {
const unsigned long long z = nextSeed (((unsigned long long) seed << 32) ^ 0xBA55000000000000ull ^ (unsigned long long) step), y = nextSeed (z);
u1 = (double) (z >> 40) / (double) (1ull << 23) - 1.0;
u2 = (double) (y >> 40) / (double) (1ull << 23) - 1.0;
}
static void lanePair (unsigned int seed, int lane, long long j, double& u1, double& u2) {
unsigned long long z = nextSeed (((unsigned long long) seed << 32) ^ ((unsigned long long) (lane + 1) << 56) ^ (unsigned long long) j);
u1 = (double) (z >> 40) / (double) (1ull << 23) - 1.0;
z = nextSeed (z);
u2 = (double) (z >> 40) / (double) (1ull << 23) - 1.0;
}
static double chance (unsigned int seed, long long step) {
return (double) (nextSeed (((unsigned long long) seed << 32) ^ 0x0C4A9CE000000000ull ^ (unsigned long long) step) >> 11) * (1.0 / 9007199254740992.0);
}
};
}
namespace shelf {
struct KeysHand {
int notes[16], accents[16], count, accentCount, center, octave, channel, walk;
unsigned char skip[16];
bool chordTones;
double rate;
float gate, level, density;
int set (const char* noteText, const char* accentText) {
for (int k = 0; k < 16; ++k) { notes[k] = 0; accents[k] = 80; skip[k] = 0; }
count = numbers (noteText, notes, skip);
accentCount = numbers (accentText, accents, nullptr);
if (accentCount < 1) accentCount = count > 0 ? count : 1;
return count;
}
static int numbers (const char* t, int* out, unsigned char* chance) {
int n = 0, i = 0;
while (t && t[i] && n < 16) {
int sign = 1, v = 0, digits = 0;
if (t[i] == 45) { sign = -1; ++i; }
for (; t[i] >= 48 && t[i] <= 57; ++i, ++digits) v = logic_min (999, v * 10 + t[i] - 48);
if (digits > 0) {
if (chance) chance[n] = t[i] == 63 ? 64 : 0;
out[n++] = sign * v;
}
while (t[i] && t[i] != 32 && t[i] != 44 && t[i] != 124) ++i;
while (t[i] == 32 || t[i] == 44 || t[i] == 124) ++i;
}
return n;
}
};
struct KeysScene {
KeysHand hands[2];
float depth, tension, meet, balance, amount;
unsigned int seed;
int variation, display;
void reset() {
for (int i = 0; i < 2; ++i) {
KeysHand& k = hands[i];
k.set (i == 0 ? "0 1 2" : "4 2 1 2", i == 0 ? "68 45 53" : "76 50 61 46");
k.chordTones = i == 0; k.center = i == 0 ? 48 : 60; k.octave = 0; k.channel = i + 1; k.walk = 0;
k.rate = 0.25; k.gate = 0.9f; k.level = 0.0f; k.density = 1.0f;
}
depth = 1.0f; tension = 0.5f; meet = 0.3f; balance = 0.0f; amount = 0.5f; seed = 1; variation = 0; display = -1;
}
};
struct Keys {
struct Active { bool used; int channel, note; double onPpq, offPpq; };
struct Pending { long long s; double onPpq, length; unsigned char channel, pitch, velocity, hand; };
struct Step { double onBeat, length; int pitch, velocity, accentPos; bool sounds; };
struct Move { int kind, hand, a; float x; };
struct Variation { unsigned long long seed; int count; Move moves[8]; };
bool ready = false, wasPlaying, adopted;
double expectedPpq, bpb;
unsigned long long playingSeed;
float playingAmount;
int count, maxOut, shows;
Variation playing;
KeysScene p;
const Harmony* h;
LogicEvent* dest;
Active active[64];
Pending pending[256];
LogicEvent show[64];
void reset() {
ready = true; wasPlaying = adopted = false; expectedPpq = 0.0; bpb = 4.0; playingSeed = 0; playingAmount = 0.0f;
playing.seed = 0; playing.count = 0; count = maxOut = shows = 0; h = nullptr; dest = nullptr;
for (int a = 0; a < 64; ++a) active[a].used = false;
p.reset();
}
int process (const LogicBlock& b, const KeysScene& scene, const Harmony& harmony, LogicEvent* out, int maxEvents) {
if (! ready) reset();
dest = out; maxOut = maxEvents; count = 0; shows = 0; h = &harmony;
bpb = b.beatsPerBar > 0 ? b.beatsPerBar : 4.0;
KeysScene base = scene;
for (int i = 0; i < 2; ++i) {
KeysHand& k = base.hands[i];
k.count = logic_clamp (k.count, 0, 16); k.accentCount = logic_clamp (k.accentCount, 1, 16); k.walk = logic_clamp (k.walk, 0, 255);
for (int s = 0; s < 16; ++s) { k.accents[s] = logic_clamp (k.accents[s], 1, 127); if (k.skip[s] > 127) k.skip[s] = 127; }
}
const unsigned long long seed = (unsigned long long) logic_clamp (base.variation, 0, 999999);
const float amount = seed == 0 ? 0.0f : (float) logic_clamp ((double) base.amount, 0.0, 1.0);
if (! adopted) adopt (seed, amount, base);
const bool incoming = seed != playingSeed || amount != playingAmount;
if (! b.playing || ! (b.bpm > 0.0 && b.sampleRate > 0.0 && b.ppqStart > -1e12 && b.ppqStart < 1e12)) {
if (incoming) adopt (seed, amount, base);
if (wasPlaying) flush();
wasPlaying = false;
return finish();
}
const double spb = b.sampleRate * 60.0 / b.bpm, ppqEnd = b.ppqStart + (double) b.numSamples / spb;
const long long start = logic_round (b.ppqStart * spb), end = start + b.numSamples;
if (wasPlaying && logic_abs (b.ppqStart - expectedPpq) > 1e-3) flush();
long long split = end;
if (incoming) {
const double bar = (double) logic_ceil (b.ppqStart / bpb - 1e-9) * bpb;
if (bar < ppqEnd) split = logic_clamp (logic_round (bar * spb), start, end);
}
if (incoming && split < end) {
vary (base);
play (start, start, split, b.ppqStart, spb);
adopt (seed, amount, base);
}
vary (base);
play (start, split < end ? split : start, end, b.ppqStart, spb);
expectedPpq = ppqEnd;
wasPlaying = true;
return finish();
}
double interval (int hand) const { return p.hands[hand].rate >= 1.0 / 64.0 ? p.hands[hand].rate : 0.5; }
static int rankOf (const KeysHand& k, int a) {
int rank = 0;
for (int i = 0; i < k.accentCount; ++i) rank += k.accents[i] > k.accents[a] || (k.accents[i] == k.accents[a] && i < a);
return rank;
}
bool startsAt (int hand, double beat) const {
const double jf = beat / interval (hand), jr = (double) logic_floor (jf + 0.5);
return logic_abs (jf - jr) <= 1e-6 && jr >= 0 && p.hands[hand].count > 0 && logic_mod ((long long) jr, p.hands[hand].count) == 0;
}
int pitchOf (int hand, const Chord& c, int token) const {
const KeysHand& k = p.hands[hand];
int pitch = token;
if (k.chordTones && c.count > 0) {
const int n = logic_min (c.count, 6);
int stack[6];
stack[0] = k.center - 7 + (int) logic_mod (c.tones[0] - (k.center - 7), 12);
for (int i = 1; i < n; ++i) {
const int step = (int) logic_mod (c.tones[i] - stack[i - 1], 12);
stack[i] = stack[i - 1] + (step == 0 ? 12 : step);
}
pitch = stack[logic_mod (token, n)] + 12 * (int) floorDiv (token, n);
} else if (! k.chordTones) {
const int tonic = k.center - 6 + (int) logic_mod (h->key.root - (k.center - 6), 12), n = keySize (h->key);
pitch = tonic + h->key.scale[logic_mod (token, n)] + 12 * (int) floorDiv (token, n);
}
return logic_clamp (pitch + 12 * k.octave, 0, 127);
}
static double wrap (double x, double y) {
double r = x - (double) logic_floor (x / y) * y;
if (r < 0.0) r += y;
if (r >= y) r -= y;
return r;
}
Step attackAt (int hand, long long j) const {
const KeysHand& k = p.hands[hand];
const double I = interval (hand);
const int P = k.count, A = k.accentCount, at = (int) logic_mod (j, P);
const long long cycle = floorDiv (j, P), walked = k.walk > 0 ? floorDiv (cycle, k.walk) : 0;
Step a;
a.onBeat = (double) j * I;
a.length = logic_clamp ((double) k.gate, 0.05, 1.5) * I;
if (a.length < 1.0 / 64.0) a.length = 1.0 / 64.0;
a.accentPos = (int) logic_mod (j - walked, A);
const bool first = at == 0, meet = first && startsAt (1 - hand, a.onBeat);
a.pitch = pitchOf (hand, h->at (a.onBeat, bpb).now, k.notes[at]);
const int keep = logic_clamp ((int) logic_ceil (logic_clamp ((double) k.density, 0.0, 1.0) * A - 1e-9), 1, A);
a.sounds = first || meet || rankOf (k, a.accentPos) < keep;
if (k.skip[at] > 0) {
double u1, u2;
Feel::lanePair (p.seed ^ 0x9E3779B9u, hand, j, u1, u2);
if (0.5 * (u1 + 1.0) < (double) k.skip[at] / 127.0) a.sounds = false;
}
double mean = 0.0;
for (int i = 0; i < A; ++i) mean += k.accents[i];
mean /= A;
const double front = hand == 1 ? p.balance : -p.balance, contrast = p.depth * (1.0 + 0.6 * front);
const double pos = wrap (a.onBeat + 1e-7, bpb) - 1e-7;
double v = mean + k.level + 10.0 * front;
v += contrast * ((double) k.accents[a.accentPos] - mean);
v += p.tension * 14.0 * (Feel::barWeight (pos < 0 ? 0 : pos, bpb) - 0.3);
if (meet) v += p.meet * 14.0;
a.velocity = logic_clamp ((int) logic_round (v), 1, 127);
return a;
}
void adopt (unsigned long long seed, float amount, const KeysScene& base) {
playing.seed = seed;
playing.count = 0;
playingSeed = seed;
playingAmount = amount;
adopted = true;
if (seed == 0) return;
unsigned long long r = seed;
const double amt = logic_clamp ((double) amount, 0.0, 1.0);
r = Feel::nextSeed (r);
const int moves = 1 + (int) logic_floor (amt * 3.0 + (double) (r >> 11) * (1.0 / 9007199254740992.0) * 0.999);
static const int kinds[8] = { 0, 1, 2, 2, 3, 4, 5, 6 };
for (int i = 0; i < moves && playing.count < 8; ++i) {
Move m = { 0, 0, 0, 0.0f };
for (int tries = 0; tries < 8; ++tries) {
r = Feel::nextSeed (r); m.kind = kinds[r % 8u];
r = Feel::nextSeed (r); m.hand = (int) (r % 2u);
bool twice = false;
for (int k = 0; k < playing.count; ++k) twice = twice || (playing.moves[k].kind == m.kind && playing.moves[k].hand == m.hand);
if (! twice) break;
}
const KeysHand& k = base.hands[m.hand];
const int A = k.accentCount > 0 ? k.accentCount : 1;
double sign = 1.0, unit = 0.0;
if (m.kind == 0) { r = Feel::nextSeed (r); m.a = (r & 1) ? 1 : -1; }
else if (m.kind == 1 || m.kind == 2) { r = Feel::nextSeed (r); m.a = (int) (r % (unsigned long long) A); }
else if (m.kind == 6) { r = Feel::nextSeed (r); m.a = 1 + (int) (r % 4u); }
if (m.kind == 2 || m.kind == 4 || m.kind == 5) { r = Feel::nextSeed (r); sign = (r & 1) ? 1.0 : -1.0; }
if (m.kind >= 2 && m.kind <= 5) { r = Feel::nextSeed (r); unit = (double) (r >> 11) * (1.0 / 9007199254740992.0); }
if (m.kind == 2) m.x = (float) (sign * (6.0 + 16.0 * amt * unit));
else if (m.kind == 3) m.x = (float) (-(0.1 + 0.3 * amt * unit));
else if (m.kind == 4) m.x = (float) (sign * (3.0 + 12.0 * amt * unit));
else if (m.kind == 5) m.x = (float) (sign * (0.1 + 0.4 * amt * unit));
playing.moves[playing.count++] = m;
}
}
void vary (const KeysScene& base) {
p = base;
if (playing.seed != 0) p.seed = (unsigned int) (playing.seed ^ (playing.seed >> 32));
for (int i = 0; i < playing.count; ++i) {
const Move& m = playing.moves[i];
KeysHand& k = p.hands[m.hand];
const int A = k.accentCount > 0 ? k.accentCount : 1;
if (m.kind == 0) {
int moved[16];
for (int s = 0; s < A; ++s) moved[logic_mod (s + m.a, A)] = k.accents[s];
for (int s = 0; s < A; ++s) k.accents[s] = moved[s];
} else if (m.kind == 1) {
const int a = (int) logic_mod (m.a, A), c = (int) logic_mod (m.a + 1, A), t = k.accents[a];
k.accents[a] = k.accents[c];
k.accents[c] = t;
} else if (m.kind == 2) {
const int a = (int) logic_mod (m.a, A);
const float sum = (float) k.accents[a] + m.x;
k.accents[a] = logic_clamp ((int) logic_round ((double) sum), 1, 127);
} else if (m.kind == 3) k.density = (float) logic_clamp ((double) (k.density + m.x), 0.0, 1.0);
else if (m.kind == 5) k.gate = (float) logic_clamp ((double) (k.gate + m.x), 0.05, 1.5);
else if (m.kind == 6) k.walk = m.a;
}
}
void off (Active& a, long long at) {
if (count >= maxOut) return;
dest[count++] = { (int) at, LOGIC_NOTE_OFF, 0.0f, a.note, a.channel };
a.used = false;
}
void flush() { for (int a = 0; a < 64; ++a) if (active[a].used) off (active[a], 0); }
void startNote (const Pending& q, long long blockStart, long long from, double spb) {
if (count >= maxOut - 1) return;
double offPpq = q.onPpq + q.length;
bool merged = false;
for (int i = 0; i < 64; ++i) {
Active& a = active[i];
if (! a.used || a.channel != q.channel || a.note != q.pitch) continue;
const long long on = logic_round (a.onPpq * spb);
if (on == q.s) { if (offPpq > a.offPpq) a.offPpq = offPpq; merged = true; }
else if (on > q.s) { if (a.onPpq < offPpq) offPpq = a.onPpq; }
else off (a, logic_clamp (logic_round (a.offPpq * spb), from, q.s) - blockStart);
}
if (merged || count >= maxOut) return;
int slot = -1;
for (int i = 0; i < 64 && slot < 0; ++i) if (! active[i].used) slot = i;
if (slot < 0) return;
dest[count++] = { (int) (q.s - blockStart), LOGIC_NOTE_ON, (float) q.velocity, q.pitch, q.channel };
active[slot] = { true, q.channel, q.pitch, q.onPpq, offPpq };
}
void play (long long blockStart, long long from, long long to, double ppqStart, double spb) {
if (to <= from) return;
const double ppqFrom = ppqStart + (double) (from - blockStart) / spb, ppqTo = ppqStart + (double) (to - blockStart) / spb;
int np = 0;
for (int hand = 0; hand < 2; ++hand) {
const KeysHand& k = p.hands[hand];
if (k.count <= 0) continue;
const double I = interval (hand);
const long long last = logic_ceil (ppqTo / I) + 2;
for (long long j = logic_max (0LL, logic_floor (ppqFrom / I) - 2); j <= last; ++j) {
const Step a = attackAt (hand, j);
const long long s = logic_round (a.onBeat * spb);
if (s < from || s >= to) continue;
if (p.display >= 0 && p.display + hand <= 7 && shows < 64) show[shows++] = { (int) (s - blockStart), LOGIC_DISPLAY, (float) a.accentPos, p.display + hand, 1 };
if (a.sounds && np < 256)
pending[np++] = { s, a.onBeat, a.length, (unsigned char) logic_clamp (k.channel, 1, 16), (unsigned char) a.pitch, (unsigned char) a.velocity, (unsigned char) hand };
}
}
for (int i = 1; i < np; ++i)
for (int k = i; k > 0 && (pending[k].s < pending[k - 1].s || (pending[k].s == pending[k - 1].s && pending[k].hand < pending[k - 1].hand)); --k) {
const Pending t = pending[k];
pending[k] = pending[k - 1];
pending[k - 1] = t;
}
for (int i = 0; i < np; ++i) startNote (pending[i], blockStart, from, spb);
for (int a = 0; a < 64; ++a) {
const long long s = logic_round (active[a].offPpq * spb);
if (active[a].used && s < to) off (active[a], logic_clamp (s, from, to - 1) - blockStart);
}
}
int finish() {
for (int i = 0; i < shows && count < maxOut; ++i) dest[count++] = show[i];
return sortEvents (dest, count);
}
};
}
// ---- End of MIDI components
// The copyright holder also offers this whole file under the GNU Affero General Public License,
// version 3 or later, at your choice (25 November 2026): see shell/platform/web/FELT-LICENSE.md.

// A MIDI tool drives no sound controls.
static const char* const kLogicControls[1] = { "" };
static const int kNumLogicControls = 0;

static const LogicParam kLogicParams[] = {
    { "Key",          0.0f, 11.0f,   2.0f, 1.0f,  "[style:menu{C;C#;D;Eb;E;F;F#;G;Ab;A;Bb;B}]" },
    { "Mode",         0.0f, 4.0f,    1.0f, 1.0f,  "[style:menu{Minor;Dorian;Major;Mixolydian;Phrygian}]" },
    { "Chords",       0.0f, 4.0f,    0.0f, 1.0f,  "[style:menu{1-6-4-1;1-4 drift;1-5-6-4;6-4-1-5;One chord}]" },
    { "Bars a chord", 0.0f, 2.0f,    1.0f, 1.0f,  "[style:menu{1;2;4}]" },
    { "Figure",       0.0f, 3.0f,    0.0f, 1.0f,  "[style:menu{3 against 4;5 against 4;4 against 3;6 against 4}]" },
    { "Rate",         0.0f, 3.0f,    1.0f, 1.0f,  "[style:menu{1/8;1/16;1/8 triplet;dotted 1/8}]" },
    { "Tension",      0.0f, 1.0f,    0.5f, 0.01f, "[style:knob]" },
    { "Balance",     -1.0f, 1.0f,    0.0f, 0.01f, "[style:knob]" },
    { "Length",       0.2f, 1.5f,    0.9f, 0.01f, "[style:knob]" },
    { "Variation",    0.0f, 9999.0f, 0.0f, 1.0f,  "[style:generate]" },
    { "Amount",       0.0f, 1.0f,    0.5f, 0.01f, "[style:knob]" },
};
static const int kNumLogicParams = 11;

// The menus in the shelf terms: modes, the kit progressions, and four figures from the materials 3 vs
// 4 ostinato, 5 vs 4, 4 vs 3 turned and 6 vs 4 lilting of Pattern Player by Lukasz Furman
// (furmanlukasz/pattern-player at 7f99aa5, core/pp_theory.cpp), by permission: the left hand in chord
// tones, the right in steps of the key, each with its accents.
static const int kModes[5] = { shelf::Minor, shelf::Dorian, shelf::Major, shelf::Mixolydian, shelf::Phrygian };
static const int kChords[5] = { 0, 8, 5, 7, 1 };
static const int kBars[3] = { 1, 2, 4 };
static const char* const kLeft[4][2] = { { "0 1 2", "68 45 53" }, { "0 1 2 3 1", "70 44 56 50 47" }, { "0 2 1 2", "70 46 56 48" },
                                         { "0 1 2 3 2 1", "72 44 56 50 58 42" } };
static const char* const kRight[4][2] = { { "4 2 1 2", "76 50 61 46" }, { "7 4 2 4", "74 48 60 50" }, { "4 2 0", "72 52 58" },
                                          { "4 2 3 1", "70 50 62 46" } };
static const double kRates[4] = { 0.5, 0.25, 1.0 / 3.0, 0.75 };

struct Logic
{
    shelf::Keys keys;
    shelf::KeysScene scene;
    shelf::Harmony harmony;

    void prepare (double) { keys.reset(); scene.reset(); harmony.reset(); }

    int process (const LogicBlock& b, LogicEvent* out, int maxEvents)
    {
        const int figure = logic_clamp ((int) b.params[4], 0, 3);
        const double rate = kRates[logic_clamp ((int) b.params[5], 0, 3)];
        harmony.set (logic_clamp ((int) b.params[0], 0, 11), kModes[logic_clamp ((int) b.params[1], 0, 4)],
                     kChords[logic_clamp ((int) b.params[2], 0, 4)], kBars[logic_clamp ((int) b.params[3], 0, 2)]);
        scene.reset();
        scene.hands[0].set (kLeft[figure][0], kLeft[figure][1]);
        scene.hands[1].set (kRight[figure][0], kRight[figure][1]);
        for (int hand = 0; hand < 2; ++hand) {
            scene.hands[hand].rate = rate;
            scene.hands[hand].gate = (float) logic_clamp ((double) b.params[8], 0.2, 1.5);
        }
        scene.tension = (float) logic_clamp ((double) b.params[6], 0.0, 1.0);
        scene.balance = (float) logic_clamp ((double) b.params[7], -1.0, 1.0);
        scene.variation = logic_clamp ((int) b.params[9], 0, 9999);
        scene.amount = (float) logic_clamp ((double) b.params[10], 0.0, 1.0);
        return keys.process (b, scene, harmony, out, maxEvents);
    }
};
