// The Felt look derives from Pattern Player by Lukasz Furman: licensed as shell/platform/web/FELT-LICENSE.md says. Also offered under AGPL-3.0-or-later, at your choice (25 November 2026).
// Harmony for the panel's chord map and notes (docs/felt-look.md): what chord the notes a
// MIDI tool plays make, the chords of its key on Pattern Player's ring, and the key itself when the
// tool does not say. Plain JavaScript, no dependencies, so the builder's page on the site and the
// plugin's own page (both from shell/platform/web) read the notes the same way. Pure: pitch classes
// in, names out. The words for a chord's job are Pattern Player's: home, departure, tension.
//
// Ported from the Felt lab's src/lib/music/harmony.ts and src/components/felt/notes-book.ts (14
// November 2026), which are gone: this file is the one copy.

/** The steps of each mode, from its tonic. */
export const MODE_STEPS = {
  major: [0, 2, 4, 5, 7, 9, 11],
  minor: [0, 2, 3, 5, 7, 8, 10],
  dorian: [0, 2, 3, 5, 7, 9, 10],
  phrygian: [0, 1, 3, 5, 7, 8, 10],
  lydian: [0, 2, 4, 6, 7, 9, 11],
  mixolydian: [0, 2, 4, 5, 7, 9, 10],
  locrian: [0, 1, 3, 5, 6, 8, 10],
  "harmonic minor": [0, 2, 3, 5, 7, 8, 11],
  "melodic minor": [0, 2, 3, 5, 7, 9, 11],
  "phrygian dominant": [0, 1, 4, 5, 7, 8, 10],
};

const SHARPS = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
const FLATS = ["C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"];

const pc = (n) => ((n % 12) + 12) % 12;

/**
 * Whether a key is written with sharps: G, D, A, E, B and F# major and their relatives (Db, not C#).
 * @param {{ tonic: number, mode: string } | null} key
 */
export function usesSharps(key) {
  if (!key) return false;
  // The major key with the same signature: a minor-like mode's relative major.
  const steps = MODE_STEPS[key.mode] || MODE_STEPS.major;
  const third = steps[2] ?? 4;
  const major = third === 3 ? pc(key.tonic + 3) : key.tonic;
  return [7, 2, 9, 4, 11, 6].includes(major);
}

/** @param {number} pitch @param {{ tonic: number, mode: string } | null} key */
export function pitchName(pitch, key) {
  return (usesSharps(key) ? SHARPS : FLATS)[pc(pitch)] ?? "?";
}

/** A MIDI note's name with its octave, C4 = 60. @param {number} note @param {{ tonic: number, mode: string } | null} key */
export function noteName(note, key) {
  return `${pitchName(note, key)}${Math.floor(note / 12) - 1}`;
}

/** The pitch class a key name such as "F#", "Bb" or "C minor" starts with, or null. @param {string} label */
export function parseTonic(label) {
  const match = /^\s*([A-Ga-g])\s*([#♯b♭]?)/.exec(String(label));
  if (!match) return null;
  const base = { c: 0, d: 2, e: 4, f: 5, g: 7, a: 9, b: 11 }[(match[1] ?? "c").toLowerCase()] ?? 0;
  const accidental = match[2] === "#" || match[2] === "♯" ? 1 : match[2] ? -1 : 0;
  return pc(base + accidental);
}

/** A mode named in a menu ("Minor", "Natural minor", "Dorian", "Phrygian dominant"), or null. @param {string} label */
export function parseMode(label) {
  const text = String(label).trim().toLowerCase();
  // The two-word modes first: "Phrygian dominant" is not phrygian, nor "Melodic minor" minor.
  if (text.includes("dominant")) return "phrygian dominant";
  if (text.includes("melodic")) return "melodic minor";
  if (text.includes("harmonic")) return "harmonic minor";
  for (const mode of ["dorian", "phrygian", "lydian", "mixolydian", "locrian", "major", "minor"]) {
    if (text.includes(mode)) return mode;
  }
  if (text.includes("ionian")) return "major";
  if (text.includes("aeolian")) return "minor";
  return null;
}

// ---- Naming a chord ----

const QUALITIES = [
  { quality: "", steps: [0, 4, 7] },
  { quality: "m", steps: [0, 3, 7] },
  { quality: "dim", steps: [0, 3, 6] },
  { quality: "aug", steps: [0, 4, 8] },
  { quality: "sus2", steps: [0, 2, 7] },
  { quality: "sus4", steps: [0, 5, 7] },
  { quality: "7", steps: [0, 4, 7, 10] },
  { quality: "maj7", steps: [0, 4, 7, 11] },
  { quality: "m7", steps: [0, 3, 7, 10] },
  { quality: "m7b5", steps: [0, 3, 6, 10] },
  { quality: "dim7", steps: [0, 3, 6, 9] },
  { quality: "mmaj7", steps: [0, 3, 7, 11] },
  { quality: "6", steps: [0, 4, 7, 9] },
  { quality: "m6", steps: [0, 3, 7, 9] },
  { quality: "add9", steps: [0, 2, 4, 7] },
  { quality: "madd9", steps: [0, 2, 3, 7] },
  { quality: "9", steps: [0, 2, 4, 7, 10] },
  { quality: "maj9", steps: [0, 2, 4, 7, 11] },
  { quality: "m9", steps: [0, 2, 3, 7, 10] },
  { quality: "5", steps: [0, 7] },
];

/**
 * @typedef {{ root: number, quality: string, name: string, pitches: number[] }} Chord
 * @typedef {{ tonic: number, mode: string }} Key
 * @typedef {"home" | "departure" | "tension"} ChordFunction
 * @typedef {{ number: number, chord: Chord, role: ChordFunction }} Degree
 */

/** @returns {Chord} */
function chordOf(root, quality, key) {
  const steps = (QUALITIES.find((q) => q.quality === quality) || { steps: [0, 4, 7] }).steps;
  return {
    root: pc(root),
    quality,
    // Cm(add9), as Pattern Player writes it; Cadd9 needs no brackets.
    name: `${pitchName(root, key)}${quality === "madd9" ? "m(add9)" : quality}`,
    pitches: steps.map((s) => pc(root + s)),
  };
}

/** How many of a chord's notes are in the key's scale. */
function fit(members, key) {
  const scale = new Set((MODE_STEPS[key.mode] || MODE_STEPS.major).map((s) => pc(key.tonic + s)));
  return [...members].filter((p) => scale.has(p)).length;
}

/**
 * The chord a set of weighted pitch classes makes: every root and quality scored by the weight of
 * its notes heard, less what is heard and not in it and what it has and is not heard, with a
 * little for the bass. Null for fewer than two pitch classes.
 * @param {Map<number, number>} weights @param {number | null} bass @param {Key | null} key
 * @returns {Chord | null}
 */
export function identifyChord(weights, bass, key) {
  const heard = [...weights.entries()].filter(([, w]) => w > 0);
  if (heard.length < 2) return null;
  const total = heard.reduce((sum, [, w]) => sum + w, 0);
  let best = null;
  for (let root = 0; root < 12; root++) {
    for (const { quality, steps } of QUALITIES) {
      const members = new Set(steps.map((s) => pc(root + s)));
      let inside = 0;
      for (const [p, w] of heard) if (members.has(p)) inside += w;
      const outside = total - inside;
      const missing = [...members].filter((p) => !weights.get(p)).length;
      const score =
        inside / total -
        0.6 * (outside / total) -
        0.18 * missing -
        0.02 * steps.length +
        (bass !== null && pc(bass) === root ? 0.12 : 0) +
        (key && (MODE_STEPS[key.mode] || []).includes(pc(root - key.tonic)) ? 0.02 : 0) +
        // Where the notes heard leave it open (no third: C G Bb), the chord of the key wins.
        (key ? (0.04 * (fit(members, key) * 2 - members.size)) / members.size : 0);
      if (!best || score > best.score + 1e-9) best = { score, root, quality };
    }
  }
  return best ? chordOf(best.root, best.quality, key) : null;
}

// ---- The chords of a key ----

/** Pattern Player's jobs for the seven degrees: 1, 3 and 6 home, 2 and 4 departure, 5 and 7 tension. */
const ROLES = ["home", "departure", "home", "departure", "tension", "home", "tension"];

/**
 * The seven chords of a key, sevenths stacked in its own scale, numbered from the tonic.
 * @param {Key} key @returns {Degree[]}
 */
export function chordsOfKey(key) {
  const steps = MODE_STEPS[key.mode] || MODE_STEPS.major;
  return steps.map((_, i) => {
    const notes = [0, 2, 4, 6].map((k) => {
      const index = i + k;
      return pc(key.tonic + (steps[index % 7] ?? 0) + 12 * Math.floor(index / 7));
    });
    const root = notes[0] ?? key.tonic;
    const shape = notes.map((n) => pc(n - root)).sort((a, b) => a - b);
    const found = QUALITIES.find((q) => q.steps.length === 4 && q.steps.every((s, j) => s === shape[j]));
    return { number: i + 1, chord: chordOf(root, found ? found.quality : "", key), role: ROLES[i] ?? "home" };
  });
}

/**
 * The chords of a key on Pattern Player's ring (pp_theory.cpp, ringDegree): in thirds, so that
 * neighbours share two notes and chords two places apart share one, with home at the top. Degree
 * numbers 1, 3, 5, 7, 2, 4, 6 for a key of seven.
 * @param {Key} key @returns {Degree[]}
 */
export function ringOfKey(key) {
  const degrees = chordsOfKey(key);
  return degrees.map((_, pos) => degrees[(2 * pos) % degrees.length]);
}

/**
 * The degree a chord heard stands on in a key: its root is the degree's, and they share two notes
 * or more. Null for a chord from outside the key.
 * @param {Chord} chord @param {Key} key @returns {Degree | null}
 */
export function degreeOf(chord, key) {
  return (
    chordsOfKey(key).find(
      (d) => d.chord.root === chord.root && chord.pitches.filter((p) => d.chord.pitches.includes(p)).length >= 2,
    ) ?? null
  );
}

// ---- The key, from the notes ----

// Krumhansl and Kessler's key profiles.
const MAJOR = [6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88];
const MINOR = [6.33, 2.68, 3.52, 5.38, 2.6, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17];

function correlation(a, b) {
  const mean = (v) => v.reduce((s, x) => s + x, 0) / v.length;
  const ma = mean(a);
  const mb = mean(b);
  let top = 0;
  let da = 0;
  let db = 0;
  for (let i = 0; i < a.length; i++) {
    const x = (a[i] ?? 0) - ma;
    const y = (b[i] ?? 0) - mb;
    top += x * y;
    da += x * x;
    db += y * y;
  }
  return da > 0 && db > 0 ? top / Math.sqrt(da * db) : 0;
}

/**
 * The key a histogram of pitch classes (12 weights) fits best, major or minor, or null when there
 * is too little to go on. With `tonic` given, only its mode is chosen.
 * @param {number[]} histogram @param {number | null} [tonic] @returns {Key | null}
 */
export function estimateKey(histogram, tonic = null) {
  if (histogram.filter((w) => w > 0).length < 3) return null;
  let best = null;
  for (let t = 0; t < 12; t++) {
    if (tonic !== null && t !== tonic) continue;
    for (const [mode, profile] of [["major", MAJOR], ["minor", MINOR]]) {
      const rotated = Array.from({ length: 12 }, (_, i) => profile[pc(i - t)] ?? 0);
      const r = correlation(histogram, rotated);
      if (!best || r > best.r) best = { r, key: { tonic: t, mode } };
    }
  }
  return best ? best.key : null;
}

/** @param {Key} key */
export function keyName(key) {
  return `${pitchName(key.tonic, key)} ${key.mode}`;
}

// ---- The notes heard ----

/** How long the book remembers a note that has ended, in seconds. */
export const MEMORY_SECONDS = 30;

/**
 * The notes a MIDI tool has played: what sounds, and what sounded lately, on one clock in seconds.
 * Fed with events as [on, note, velocity, channel] fours, each with the time it was heard.
 */
export class NotesBook {
  constructor() {
    /** @type {Map<number, { note: number, channel: number, velocity: number, start: number, end: number | null }>} */
    this.sounding = new Map();
    /** @type {Array<{ note: number, channel: number, velocity: number, start: number, end: number | null }>} */
    this.ended = [];
  }

  /**
   * One note on or off, heard at `time`. A velocity of 0 is an off.
   * @param {number} time @param {boolean} on @param {number} note @param {number} velocity @param {number} channel
   */
  hear(time, on, note, velocity, channel) {
    if (!Number.isFinite(time) || !Number.isInteger(note) || note < 0 || note > 127) return;
    const key = channel * 128 + note;
    const open = this.sounding.get(key);
    if (open) {
      open.end = time;
      this.ended.push(open);
      this.sounding.delete(key);
    }
    if (on && velocity > 0) this.sounding.set(key, { note, channel, velocity, start: time, end: null });
  }

  /**
   * A batch as the pages are sent it: [on, note, velocity, channel] fours, all heard at `time`.
   * @param {ArrayLike<number>} events @param {number} time
   */
  add(events, time) {
    for (let i = 0; i + 3 < events.length; i += 4)
      this.hear(time, events[i] === 1, Number(events[i + 1]), Number(events[i + 2]), Number(events[i + 3]));
  }

  /** Every note that sounded at some time in [from, to], started by `to`. */
  notesIn(from, to) {
    const out = [];
    for (const n of this.ended) if (n.start <= to && (n.end ?? to) >= from) out.push(n);
    for (const n of this.sounding.values()) if (n.start <= to) out.push(n);
    return out;
  }

  /** Forgets what ended before `time`. */
  prune(time) {
    this.ended = this.ended.filter((n) => (n.end ?? time) >= time);
  }

  /** Ends every note sounding at `time`: the tool stopped, or the panel was bypassed. */
  endAll(time) {
    for (const n of this.sounding.values()) {
      n.end = time;
      this.ended.push(n);
    }
    this.sounding.clear();
  }

  clear() {
    this.sounding.clear();
    this.ended = [];
  }

  /**
   * What the chord is made of now: each pitch class weighted by how loud and how long it sounded
   * in the last `window` seconds, and the lowest note sounding in them.
   */
  chordWeights(now, window) {
    const weights = new Map();
    let bass = null;
    for (const n of this.notesIn(now - window, now)) {
      const from = Math.max(n.start, now - window);
      const to = Math.min(n.end ?? now, now);
      const weight = (0.25 + n.velocity / 127) * Math.max(0.05, to - from);
      const p = n.note % 12;
      weights.set(p, (weights.get(p) ?? 0) + weight);
      if (bass === null || n.note < bass) bass = n.note;
    }
    return { weights, bass };
  }

  /** The pitch classes of the last `window` seconds, weighted by time, for the key. */
  histogram(now, window) {
    const out = Array.from({ length: 12 }, () => 0);
    for (const n of this.notesIn(now - window, now)) {
      const from = Math.max(n.start, now - window);
      const to = Math.min(n.end ?? now, now);
      out[n.note % 12] = (out[n.note % 12] ?? 0) + Math.max(0.05, to - from);
    }
    return out;
  }
}

// ---- What the chord map shows ----

/**
 * The key now, from the tool's own menus where it has them (a Key and a Mode or Scale menu, or a
 * Chord menu such as "Cm"), and from the notes of the last twelve seconds for what they leave open.
 * `chosen` answers the label of the item the tool's "key", "mode" or "chord" menu shows now, or null
 * where it has no such menu.
 * @param {(what: "key" | "mode" | "chord") => string | null} chosen @param {NotesBook} book @param {number} now
 * @returns {{ key: Key | null, from: "menu" | "notes" | null }}
 */
export function keyNow(chosen, book, now) {
  const keyLabel = chosen("key");
  const modeLabel = chosen("mode");
  let tonic = keyLabel ? parseTonic(keyLabel) : null;
  let mode = modeLabel ? parseMode(modeLabel) : null;
  // A tool that plays over one chord ("Cm", "Fmaj7") says its key by that chord's name.
  const chordLabel = tonic === null ? chosen("chord") : null;
  if (chordLabel && parseTonic(chordLabel) !== null) {
    tonic = parseTonic(chordLabel);
    mode = mode ?? (/^\s*[A-Ga-g][#b♯♭]?m(?!aj)/.test(chordLabel) ? "minor" : "major");
  }
  if (tonic !== null && mode) return { key: { tonic, mode }, from: "menu" };
  // Twelve seconds: long enough for a few chords, short enough to follow a tool that moves.
  const heard = estimateKey(book.histogram(now, 12), tonic);
  return { key: heard, from: heard ? (tonic !== null ? "menu" : "notes") : null };
}

/**
 * What the chord map shows now, from the notes heard and the key: the ring of the key's chords,
 * the chord heard in the last 1.2 seconds with its degree, and the degree before it.
 * `before` is what it showed last, for the degree before.
 */
export function harmonyNow(chosen, book, now, before) {
  const { key, from } = keyNow(chosen, book, now);
  const { weights, bass } = book.chordWeights(now, 1.2);
  const chord = identifyChord(weights, bass, key);
  const found = chord && key ? degreeOf(chord, key) : null;
  const degree = found ? found.number : null;
  const last = before || { degree: null, previous: null };
  const moved = degree !== null && degree !== last.degree;
  return {
    key,
    keyFrom: from,
    keyName: key ? keyName(key) : null,
    ring: key
      ? ringOfKey(key).map((d) => ({ number: d.number, name: d.chord.name, role: d.role, pitches: d.chord.pitches }))
      : [],
    chord: chord ? { name: chord.name, notes: chord.pitches.map((p) => pitchName(p, key)) } : null,
    role: found ? found.role : null,
    degree,
    previous: moved ? (last.degree ?? last.previous) : last.previous,
  };
}
