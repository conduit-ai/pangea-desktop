// Rolling Line: a rolling bass that follows the chords and walks into each new one, built on Line
// from the MIDI shelf. The MIDI shelf example (docs/midi-shelf.md).
#include "logic_api.h"
// ---- MIDI components, filled in by Pangea: kit, line (from bass-hand 1af3fcb, drum-hand e297ba1, check 08b210052b85914b). Change your own code, not this block.
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
enum { Rest, Root, Third, Fifth, Seventh, Octave, Ninth, Below, Above, Ghost, Scale1, Scale2, Scale3, Scale4, Scale5, Scale6, Scale7 };
enum { Staccato, Short, Medium, Full, Tied };
enum { WalkUp, WalkDown, OctaveJump, Pickup, SlideIn, Silence };
static const double kShelfShares[5] = { 0.25, 0.5, 0.75, 1.0, 1.0 };
static const unsigned char kShelfLayouts[6][8] = { { 0, 0, 0, 1, 0, 0, 0, 1 }, { 0, 0, 0, 0, 0, 0, 0, 1 }, { 0, 1, 0, 1, 0, 1, 0, 1 },
{ 0, 0, 0, 1, 0, 0, 1, 1 }, { 0, 0, 0, 1, 0, 1, 0, 1 }, { 0, 0, 1, 0, 0, 0, 0, 1 } };
static const long long kShelfNone = -9223372036854775807LL - 1;
struct LineStep { int degree, octave, accent, skip, length; bool slide; };
struct LineScene {
LineStep steps[16];
int count, octave, low, high, channel, ghostLow, ghostHigh, fillKind, fillWhen, display;
double rate, fillRate;
bool chromatic;
float ghosts, slideMs;
Feel feel;
void reset() {
for (int i = 0; i < 16; ++i) steps[i] = { i < 8 ? Root : Rest, 0, i == 0 ? 104 : (i == 4 ? 92 : (i < 8 ? 76 : 84)), 0, Medium, false };
count = 8; octave = 0; low = 28; high = 55; channel = 1; ghostLow = 16; ghostHigh = 40;
fillKind = WalkUp; fillWhen = FillNever; display = -1; rate = 0.5; fillRate = 0.5;
chromatic = false; ghosts = 0.0f; slideMs = 12.0f;
feel.reset();
}
int parse (const char* t) {
int n = 0, i = 0;
for (int k = 0; k < 16; ++k) steps[k] = { Rest, 0, 84, 0, Medium, false };
while (t && t[i] && n < 16) {
while (t[i] == 32 || t[i] == 124) ++i;
if (! t[i]) break;
LineStep s = { Rest, 0, 80, 0, Medium, false };
for (int d = 0; d < 9; ++d) if (t[i] == "R35789<>x"[d]) s.degree = Root + d;
if (t[i] == 115 && t[i + 1] >= 49 && t[i + 1] <= 55) s.degree = Scale1 + t[++i] - 49;
for (++i; t[i] && t[i] != 32 && t[i] != 124; ++i) {
const int m = t[i];
if (m == 94 || m == 118) s.octave = m == 94 ? 1 : -1;
else if (m == 126) s.slide = true;
else if (m == 39 || m == 59) s.length = Staccato;
else if (m == 58 || m == 61 || m == 95) s.length = m == 58 ? Short : (m == 61 ? Full : Tied);
else if (m == 33 || m == 43 || m == 45 || m == 44) s.accent = m == 33 ? 112 : (m == 43 ? 96 : (m == 45 ? 62 : 48));
else if (m == 63) s.skip = s.skip ? 95 : 64;
else if (m == 64) {
int a = 0, d = 0;
for (; d < 3 && t[i + 1] >= 48 && t[i + 1] <= 57; ++d) a = a * 10 + t[++i] - 48;
if (d) s.accent = logic_clamp (a, 1, 127);
}
}
steps[n++] = s;
}
return count = n > 0 ? n : 1;
}
};
struct Line {
struct Active { bool used; int note, channel; double onPpq, offPpq; bool untilNext; long long offSample; };
struct Pending { long long s; double onPpq, length; unsigned char pitch, velocity, kind; bool slide, tied; };
struct Note { double onBeat, length; int step, velocity, pitch; bool sounds, slide, ghost, tied, added; };
bool ready = false, wasPlaying;
double expectedPpq, lastBeat, bpb, bpm;
Active cur, tail;
LineScene p;
const Harmony* h;
Pending pending[48];
void reset() {
ready = true; wasPlaying = false; expectedPpq = 0.0; lastBeat = -1.0e300; bpb = 4.0; bpm = 120.0;
cur.used = tail.used = false; p.reset(); h = nullptr;
}
int process (const LogicBlock& b, const LineScene& scene, const Harmony& harmony, LogicEvent* out, int maxEvents) {
if (! ready) reset();
if (b.numSamples <= 0 && b.playing) return 0;
h = &harmony;
bpb = b.beatsPerBar > 0 ? b.beatsPerBar : 4.0;
bpm = b.bpm > 0 ? b.bpm : 120.0;
p = scene;
p.count = logic_clamp (p.count, 1, 16);
for (int i = 0; i < 16; ++i) {
LineStep& s = p.steps[i];
if (s.degree < Rest || s.degree > Scale7) s.degree = Rest;
if (s.length < Staccato || s.length > Tied) s.length = Medium;
s.octave = logic_clamp (s.octave, -1, 1); s.accent = logic_clamp (s.accent, 1, 127); s.skip = logic_clamp (s.skip, 0, 127);
}
if (p.fillRate > 0.0 && p.fillRate < 1.0 / 64.0) p.fillRate = 1.0 / 64.0;
const double x = 2.0 * (logic_clamp ((double) p.feel.intensity, 0.0, 1.0) - 0.5);
if (logic_abs (x) >= 1e-9) {
p.feel.level += (float) (18.0 * x);
p.feel.depth *= (float) (1.0 + 0.5 * x);
if (x < 0) p.feel.density *= (float) (1.0 + 0.6 * x);
else for (int s = 0; s < 16; ++s) p.steps[s].skip = (int) logic_round (p.steps[s].skip * (1.0 - 0.5 * x));
}
return play (b, out, maxEvents);
}
double interval() const { return p.rate >= 1.0 / 64.0 ? p.rate : 0.5; }
const LineStep& at (long long j) const { return p.steps[logic_mod (j, p.count)]; }
static bool isNote (const LineStep& s) { return s.degree != Rest && s.degree != Ghost; }
static int weightOf (const LineStep& s) { return s.degree == Ghost ? s.accent / 4 : 32 + s.accent; }
int pull (int s) const { return isNote (at (s + 1)) ? 1 + at (s + 1).accent : 0; }
int writtenCount() const { int n = 0; for (int s = 0; s < p.count; ++s) n += p.steps[s].degree != Rest; return n; }
bool writtenSounds (long long j) const {
const int step = (int) logic_mod (j, p.count);
const LineStep& s = p.steps[step];
if (s.degree == Rest) return false;
const double d = logic_clamp ((double) p.feel.density, 0.0, 1.0);
if (d < 1.0 - 1e-9) {
int keep = (int) logic_ceil (d * writtenCount() - 1e-9), rank = 0;
for (int o = 0; o < p.count; ++o) {
const int w = weightOf (p.steps[o]), mine = weightOf (s);
if (o != step && p.steps[o].degree != Rest && (w > mine || (w == mine && o < step))) ++rank;
}
if (rank >= (keep < 1 ? 1 : keep)) return false;
}
return ! (s.skip > 0 && Feel::chance (p.feel.seed, j) < (double) s.skip / 127.0);
}
bool addedGhost (int step) const {
const double a = logic_clamp ((double) p.ghosts, 0.0, 1.0);
if (a <= 0.0 || p.steps[step].degree != Rest || writtenCount() == 0) return false;
int empty = 0, rank = 0;
for (int s = 0; s < p.count; ++s) {
if (p.steps[s].degree != Rest) continue;
++empty;
if (s != step && (pull (s) > pull (step) || (pull (s) == pull (step) && s < step))) ++rank;
}
return rank < (int) logic_ceil (a * empty - 1e-9);
}
bool plainSounds (long long j) const { return writtenSounds (j) || (j >= 0 && addedGhost ((int) logic_mod (j, p.count))); }
int rootOf (const Chord& c) const { return c.count > 0 ? c.tones[0] : h->key.root; }
int lowPitch (int pitchClass) const { return p.low + (int) logic_mod (pitchClass - p.low, 12); }
int move (int from, int steps) const { return p.chromatic ? from + steps : scaleMove (h->key, from, steps); }
int fold (int pitch) const {
for (int i = 0; i < 12 && pitch > p.high; ++i) pitch -= 12;
for (int i = 0; i < 12 && pitch < p.low; ++i) pitch += 12;
return logic_clamp (pitch, 0, 127);
}
static int degreeInterval (const Chord& c, const Key& key, int degree) {
if (c.count == 0) return 0;
const int root = c.tones[0];
bool has[12] = {};
for (int i = 0; i < c.count && i < 6; ++i) has[logic_mod (c.tones[i] - root, 12)] = true;
const bool seven = key.size == 7 && inKey (key, root);
const int third = (int) logic_mod (scaleMove (key, root, 2) - root, 12), seventh = (int) logic_mod (scaleMove (key, root, 6) - root, 12);
const int ninth = 12 + (int) logic_mod (scaleMove (key, root, 1) - root, 12);
if (degree == Third) return has[3] ? 3 : has[4] ? 4 : has[2] ? 2 : has[5] ? 5 : seven ? third : inKey (key, root + 3) ? 3 : 4;
if (degree == Fifth) return has[7] ? 7 : has[6] ? 6 : has[8] ? 8 : 7;
if (degree == Seventh) return has[10] ? 10 : has[11] ? 11 : has[9] ? 9 : seven ? seventh : 10;
if (degree == Ninth) return has[2] ? 14 : has[1] ? 13 : seven ? ninth : 14;
return degree == Octave ? 12 : 0;
}
int rawPitch (long long j) const {
const LineStep& s = at (j);
const ChordAt ch = h->at ((double) j * interval(), bpb);
int pitch = lowPitch (rootOf (ch.now));
if (s.degree >= Third && s.degree <= Ninth) pitch += degreeInterval (ch.now, h->key, s.degree);
else if (s.degree == Below || s.degree == Above) pitch = move (lowPitch (rootOf (ch.hasNext ? ch.next : ch.now)), s.degree == Below ? -1 : 1);
else if (s.degree >= Scale1) {
const int k = s.degree - Scale1;
pitch = lowPitch (h->key.root) + h->key.scale[k % keySize (h->key)] + 12 * (k / keySize (h->key));
}
return pitch + 12 * (s.octave + logic_clamp (p.octave, -2, 2));
}
int pitchOf (long long j) const {
const int raw = rawPitch (j);
int pitch = fold (raw);
for (int k = 1; k < p.count; ++k) {
if (at (j - k).degree == Rest) continue;
const int rawBefore = rawPitch (j - k), before = fold (rawBefore), moved = pitch + (before > pitch ? 12 : -12);
if (logic_abs (pitch - before) > 12 && logic_abs (raw - rawBefore) <= 12 && moved >= p.low && moved <= p.high) pitch = moved;
break;
}
return pitch;
}
double offsetOf (long long j) const {
const Feel& f = p.feel;
const double I = interval(), on = (double) j * I, human = logic_clamp ((double) f.human, 0.0, 1.0);
double ms = 0.0, u1 = 0.0, u2 = 0.0, here = on, before = on - I, after = on + I;
if (human > 0.0) { Feel::micro (f.seed, j, u1, u2); ms += human * f.humanMs * u2; }
if (Feel::swingable (I)) {
here = Feel::swung (on, f.swing8, f.swing16);
before = Feel::swung (on - I, f.swing8, f.swing16);
after = Feel::swung (on + I, f.swing8, f.swing16);
}
const double room = logic_min (here - before, after - here);
const double lean = logic_clamp (f.lateMs * bpm / 60000.0, -0.4 * room, 0.4 * room);
return (here - on) + logic_clamp (lean + ms * bpm / 60000.0, -0.45 * room, 0.45 * room);
}
int fillPlaces() const {
if (p.fillKind == Pickup || p.fillKind == SlideIn) return p.fillKind == Pickup ? 2 : 1;
const double thirds = p.fillRate * 3.0, quarters = p.fillRate * 4.0;
return logic_abs (thirds - (double) logic_round (thirds)) < 1e-6 && logic_abs (quarters - (double) logic_round (quarters)) > 1e-6 ? 3 : 4;
}
bool fillOn (double beat, double& endsAt) const {
if (p.fillRate <= 0.0) return false;
const double span = logic_min (fillPlaces() * p.fillRate, bpb);
if (p.fillWhen == BeforeChange && h->external && h->hasNext && rootOf (h->now) != rootOf (h->next)
&& beat >= h->nextAt - span - 1e-7 && beat < h->nextAt - 1e-7) { endsAt = h->nextAt; return true; }
if (beat < -1e-7) return false;
const long long bar = logic_floor (beat / bpb + 1e-7);
if ((beat / bpb - (double) bar) * bpb < bpb - span - 1e-7) return false;
endsAt = (double) (bar + 1) * bpb;
if (p.fillWhen >= Every2 && p.fillWhen <= Every8) return logic_mod (bar + 1, 1LL << p.fillWhen) == 0;
if (p.fillWhen == PhraseEnds) return kShelfLayouts[(((p.feel.seed ^ 0x51ED270Bu) * 2654435761u) >> 16) % 6u][logic_mod (bar, 8)] != 0;
if (p.fillWhen != BeforeChange || h->external || h->count < 2) return false;
const int per = h->barsPerChord > 0 ? h->barsPerChord : 1;
const long long index = floorDiv (bar, per);
return logic_mod (bar + 1, per) == 0 && rootOf (h->chords[logic_mod (index, h->count)]) != rootOf (h->chords[logic_mod (index + 1, h->count)]);
}
Note fillAt (long long k) const {
Note f;
f.sounds = false;
f.onBeat = (double) k * p.fillRate;
double end = 0.0;
if (! fillOn (f.onBeat, end)) return f;
const int places = fillPlaces(), left = (int) logic_round ((end - f.onBeat) / p.fillRate), octaves = 12 * logic_clamp (p.octave, -2, 2);
if (left < 1 || left > places) return f;
f.step = places - left;
const int target = fold (lowPitch (rootOf (h->at (end + 1e-6, bpb).now)) + octaves);
const int here = fold (lowPitch (rootOf (h->at (f.onBeat, bpb).now)) + octaves);
const int under = move (target, -1) >= p.low ? move (target, -1) : move (target, 1);
int pitch = -1;
f.length = 0.92 * p.fillRate;
if (p.fillKind == WalkUp || p.fillKind == WalkDown) {
bool up = p.fillKind == WalkUp;
if (up && move (target, -places) < p.low) up = false;
else if (! up && move (target, places) > p.high) up = true;
pitch = move (target, up ? -left : left);
} else if (p.fillKind == OctaveJump) {
pitch = f.step % 2 == 0 ? here : (here + 12 <= p.high ? here + 12 : here - 12);
f.length = 0.6 * p.fillRate;
} else if (p.fillKind == Pickup) pitch = left == 2 ? (target - 5 >= p.low ? target - 5 : target + 7) : under;
else if (p.fillKind == SlideIn) { pitch = under; f.length = p.fillRate; }
if (pitch < 0) return f;
f.pitch = fold (pitch);
f.sounds = true;
f.velocity = logic_clamp ((int) logic_round (72.0 + 26.0 * ((double) (f.step + 1) / places) + p.feel.level), 1, 127);
return f;
}
Note noteAt (long long j) const {
Note note;
const Feel& f = p.feel;
const double I = interval();
const LineStep& s = at (j);
note.onBeat = (double) j * I;
note.step = (int) logic_mod (j, p.count);
note.added = note.slide = note.tied = false;
note.sounds = s.degree != Rest ? writtenSounds (j) : false;
if (s.degree == Rest && j >= 0 && addedGhost (note.step)) note.sounds = note.added = true;
note.ghost = note.added || s.degree == Ghost;
double end = 0.0;
if (note.sounds && fillOn (note.onBeat, end)) note.sounds = false;
if (! note.sounds) return note;
double mean = 0.0, v = 0.0, u1 = 0.0, u2 = 0.0;
int notes = 0;
for (int k = 0; k < p.count; ++k) if (isNote (p.steps[k])) { mean += p.steps[k].accent; ++notes; }
mean = notes > 0 ? (double) (int) mean / notes : 84.0;
const double pos = Feel::posInBar (note.onBeat, bpb), weight = Feel::barWeight (pos, bpb), human = logic_clamp ((double) f.human, 0.0, 1.0);
if (human > 0.0) Feel::micro (f.seed, j, u1, u2);
note.pitch = pitchOf (j);
if (note.ghost) {
const double t = note.added ? 0.4 : (double) s.accent / 127.0;
v += p.ghostLow + (p.ghostHigh - p.ghostLow) * t + f.tension * 6.0 * (weight - 0.3) + human * 0.5 * f.humanVel * u1;
v = logic_clamp (v, (double) p.ghostLow, (double) (p.ghostHigh > p.ghostLow ? p.ghostHigh : p.ghostLow));
} else {
v += mean + f.level + f.depth * ((double) s.accent - mean);
v += f.tension * 16.0 * (weight - 0.3);
if (f.fatigue > 0.0f) {
int run = 0;
for (int k = 1; k <= 6 && note.onBeat - k * I >= note.onBeat - pos - 1e-7 && writtenSounds (j - k); ++k) ++run;
v -= f.fatigue * run;
}
v += human * f.humanVel * u1;
}
note.velocity = logic_clamp ((int) logic_round (v), 1, 127);
note.slide = ! note.ghost && s.slide;
end = 0.0;
if (! note.ghost && p.fillKind == SlideIn && p.fillRate > 0.0 && fillOn (note.onBeat - 0.5 * p.fillRate, end) && logic_abs (end - note.onBeat) < 1e-6)
note.slide = true;
note.length = kShelfShares[s.length] * I;
if (note.ghost) note.length = logic_min (0.25 * I, 0.045 * bpm / 60.0);
else if (s.length == Tied) {
int k = 1;
while (k < p.count && ! plainSounds (j + k)) ++k;
note.length = k * I;
note.tied = true;
}
if (! note.ghost && ! note.tied && plainSounds (j + 1) && at (j + 1).slide && isNote (at (j + 1))) {
note.tied = true;
note.length = logic_max (note.length, I);
}
return note;
}
long long release (const Active& a, double spb) const { return a.offSample != kShelfNone ? a.offSample : logic_round (a.offPpq * spb); }
static int off (Active& a, long long at, int numSamples, LogicEvent* out, int n, int maxEvents) {
if (n < maxEvents) out[n++] = { (int) logic_clamp (at, 0LL, (long long) (numSamples > 0 ? numSamples - 1 : 0)), LOGIC_NOTE_OFF, 0.0f, a.note, a.channel };
a.used = false;
return n;
}
int flush (LogicEvent* out, int maxEvents) {
int n = tail.used ? off (tail, 0, 1, out, 0, maxEvents) : 0;
return cur.used ? off (cur, 0, 1, out, n, maxEvents) : n;
}
static int rank (const LogicEvent& e) { return e.control == LOGIC_NOTE_OFF ? 0 : (e.control == LOGIC_NOTE_ON ? 2 : 1); }
int play (const LogicBlock& b, LogicEvent* out, int maxEvents) {
int n = 0, np = 0;
if (! b.playing || b.bpm <= 0 || b.sampleRate <= 0) {
if (wasPlaying || cur.used || tail.used) n = flush (out, maxEvents);
wasPlaying = false;
lastBeat = -1.0e300;
return n;
}
const double spb = b.sampleRate * 60.0 / b.bpm, ppqEnd = b.ppqStart + b.numSamples / spb, I = interval();
const long long start = logic_round (b.ppqStart * spb), end = start + b.numSamples;
if (wasPlaying && logic_abs (b.ppqStart - expectedPpq) > 1e-3) {
n = flush (out, maxEvents);
lastBeat = -1.0e300;
}
for (long long j = logic_max (0LL, logic_floor (b.ppqStart / I) - 2); j <= logic_ceil (ppqEnd / I) + 2; ++j) {
const double onBeat = (double) j * I, onPpq = onBeat + (onBeat > lastBeat + 1e-7 ? offsetOf (j) : 0.0);
const long long s = logic_round (onPpq * spb);
if (onBeat <= lastBeat + 1e-7 || s < start || s >= end) continue;
lastBeat = onBeat;
if (p.display >= 0 && p.display <= 7 && n < maxEvents) out[n++] = { (int) (s - start), LOGIC_DISPLAY, (float) logic_mod (j, p.count), p.display, 1 };
const Note q = noteAt (j);
if (q.sounds && np < 48) pending[np++] = { s, onPpq, q.length, (unsigned char) q.pitch, (unsigned char) q.velocity, 0, q.slide, q.tied };
}
if (p.fillRate > 0.0 && p.fillWhen != FillNever)
for (long long k = logic_max (0LL, logic_floor (b.ppqStart / p.fillRate) - 1); k <= logic_ceil (ppqEnd / p.fillRate) + 1; ++k) {
const long long s = logic_round ((double) k * p.fillRate * spb);
const Note f = s >= start && s < end ? fillAt (k) : Note {};
if (f.sounds && np < 48) pending[np++] = { s, f.onBeat, f.length, (unsigned char) f.pitch, (unsigned char) f.velocity, 1, false, p.fillKind == SlideIn };
}
for (int i = 1; i < np; ++i)
for (int k = i; k > 0 && (pending[k].s < pending[k - 1].s || (pending[k].s == pending[k - 1].s && pending[k].kind > pending[k - 1].kind)); --k) {
const Pending t = pending[k];
pending[k] = pending[k - 1];
pending[k - 1] = t;
}
int startedAt = -1;
long long startedSample = kShelfNone;
const long long overlap = (long long) logic_max (1.0, logic_min (logic_clamp ((double) p.slideMs, 1.0, 60.0) * b.sampleRate / 1000.0, 0.4 * I * spb));
const int ch = logic_clamp (p.channel, 1, 16);
for (int i = 0; i < np && n < maxEvents - 3; ++i) {
const Pending& q = pending[i];
if (startedSample == q.s) {
if (out[startedAt].value < (float) q.velocity) out[startedAt].value = (float) q.velocity;
continue;
}
if (tail.used) n = off (tail, logic_min (release (tail, spb), q.s) - start, b.numSamples, out, n, maxEvents);
if (cur.used) {
const long long own = release (cur, spb);
if (q.slide && own > q.s && cur.note != q.pitch) {
tail = cur;
tail.offSample = tail.untilNext ? q.s + overlap : logic_min (own, q.s + overlap);
cur.used = false;
} else n = off (cur, logic_min (own, q.s) - start, b.numSamples, out, n, maxEvents);
}
startedAt = n;
startedSample = q.s;
out[n++] = { (int) (q.s - start), LOGIC_NOTE_ON, (float) q.velocity, q.pitch, ch };
cur = { true, q.pitch, ch, q.onPpq, q.onPpq + q.length + (q.tied ? 0.5 * I : 0.0), q.tied, kShelfNone };
}
if (tail.used && release (tail, spb) < end) n = off (tail, release (tail, spb) - start, b.numSamples, out, n, maxEvents);
if (cur.used && release (cur, spb) < end) n = off (cur, release (cur, spb) - start, b.numSamples, out, n, maxEvents);
for (int i = 1; i < n; ++i)
for (int k = i; k > 0; --k) {
const LogicEvent x = out[k], y = out[k - 1];
if (x.offset > y.offset || (x.offset == y.offset && (rank (x) > rank (y) || (rank (x) == rank (y) && x.note >= y.note)))) break;
out[k] = y;
out[k - 1] = x;
}
expectedPpq = ppqEnd;
wasPlaying = true;
return n;
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
    { "Key",          0.0f, 11.0f,  4.0f, 1.0f,  "[style:menu{C;C#;D;Eb;E;F;F#;G;Ab;A;Bb;B}]" },
    { "Mode",         0.0f, 4.0f,   3.0f, 1.0f,  "[style:menu{Minor;Dorian;Phrygian;Phrygian dominant;Harmonic minor}]" },
    { "Chords",       0.0f, 4.0f,   0.0f, 1.0f,  "[style:menu{1-2 vamp;1-6-4-1;1-7-6-7;6-4-1-5;One chord}]" },
    { "Bars a chord", 0.0f, 2.0f,   1.0f, 1.0f,  "[style:menu{1;2;4}]" },
    { "Line",         0.0f, 4.0f,   0.0f, 1.0f,  "[style:menu{Roller;Gallop;Octaves;Ghost funk;Offbeat}]" },
    { "Ghosts",       0.0f, 1.0f,   0.0f, 0.01f, "[style:knob]" },
    { "Walk",         0.0f, 4.0f,   1.0f, 1.0f,  "[style:menu{Off;Walk up;Walk down;Pickup;Slide in}]" },
    { "Swing",        0.0f, 1.0f,   0.0f, 0.01f, "[style:knob]" },
    { "Intensity",    0.0f, 1.0f,   0.5f, 0.01f, "[style:knob]" },
    { "Variation",    0.0f, 999.0f, 0.0f, 1.0f,  "[style:generate]" },
};
static const int kNumLogicParams = 10;

// The menus, in the shelf terms: modes, the kit progressions, and lines with their rates from the
// scenes Roller, Gallop, Rolling octaves, Sixteenths with ghosts and Offbeat bass of Bass Hand by
// Lukasz Furman (furmanlukasz/bass-hand at 1af3fcb, core/bs_scenes.cpp), by permission.
static const int kModes[5] = { shelf::Minor, shelf::Dorian, shelf::Phrygian, shelf::PhrygianDominant, shelf::HarmonicMinor };
static const int kChords[5] = { 6, 0, 2, 7, 1 };
static const int kBars[3] = { 1, 2, 4 };
static const char* const kLines[5] = { ". R:+ R: R:-", "R! . R:- R:", "R! R- 8: R- R+ 8:- R 8:,",
                                       "R! x R- x R+ x R- 5 R! x R- x 8+ x 5- <", ". R:+ . R: . R:+ . 8:-" };
static const double kRates[5] = { 0.25, 0.25, 0.25, 0.25, 0.5 };
static const int kWalks[5] = { shelf::WalkUp, shelf::WalkUp, shelf::WalkDown, shelf::Pickup, shelf::SlideIn };

struct Logic
{
    shelf::Line line;
    shelf::LineScene scene;
    shelf::Harmony harmony;

    void prepare (double) { line.reset(); scene.reset(); harmony.reset(); }

    int process (const LogicBlock& b, LogicEvent* out, int maxEvents)
    {
        const int pattern = logic_clamp ((int) b.params[4], 0, 4);
        const int walk = logic_clamp ((int) b.params[6], 0, 4);
        harmony.set (logic_clamp ((int) b.params[0], 0, 11), kModes[logic_clamp ((int) b.params[1], 0, 4)],
                     kChords[logic_clamp ((int) b.params[2], 0, 4)], kBars[logic_clamp ((int) b.params[3], 0, 2)]);
        scene.reset();
        scene.parse (kLines[pattern]);
        scene.rate = kRates[pattern];
        scene.ghosts = (float) logic_clamp ((double) b.params[5], 0.0, 1.0);
        scene.fillKind = kWalks[walk];
        scene.fillWhen = walk > 0 ? shelf::BeforeChange : shelf::FillNever;
        scene.fillRate = 0.25;
        scene.feel.swing16 = (float) (0.5 + 0.25 * logic_clamp ((double) b.params[7], 0.0, 1.0));
        scene.feel.intensity = (float) logic_clamp ((double) b.params[8], 0.0, 1.0);
        scene.feel.seed = 1u + (unsigned int) logic_clamp ((int) b.params[9], 0, 999);
        return line.process (b, scene, harmony, out, maxEvents);
    }
};
