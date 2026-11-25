# Two Hands

The first MIDI tool (decisions 40 and 45, `docs/midi-tools.md`). It makes no sound: it plays two piano hands into another instrument, following the host's transport. It is a small version of Lukasz Furman's Pattern Player, keeping its sauce and nothing more, ported by hand from Pattern Player's `core` into one `logic.cpp` within the logic rules.

- **The left hand,** on MIDI channel 1, plays the root, 3rd and 5th of the chord playing now, in root position around C3.
- **The right hand,** on channel 2, plays a four-note figure in scale degrees: the 5th, 3rd, 2nd and 3rd above the tonic near C4. In C minor that is G4 Eb4 D4 Eb4.
- **Three against four:** both hands step on one shared rate, so their cycles meet every 12 steps.
- **One progression,** 1(add9), 6(7), 4(9), 1(add9), three bars per chord, built from the mode. In C minor: Cm(add9), Abmaj7, Fm9, Cm(add9).
- **Accents by rule,** and a variation number that reshapes them from the next bar line.

On a Mac it builds as two plugins, `Two Hands` (an instrument with MIDI out, for Ableton Live and Bitwig) and `Two Hands FX` (an AU MIDI effect, for Logic Pro), once the native shell knows `kind: "midi"`.

## The controls

| Control | Range | Default | What it does |
|---|---|---|---|
| Key | C to B | C | The key. Everything follows it. |
| Mode | Natural minor, Major | Natural minor | The scale the chords and the figure are built from. |
| Rate | 1/32, 1/16, 1/8, dotted 1/8, triplet 1/8 | 1/16 | The step, shared by both hands. |
| Left hand: Accent 1 to 3 | 0 to 127 | 68, 45, 53 | The left hand's accent contour, one bar per step of its cycle. The bar playing now is lit (display slot 0). |
| Right hand: Accent 1 to 4 | 0 to 127 | 76, 50, 61, 46 | The right hand's contour (display slot 1). |
| Tension | 0 to 1 | 0.5 | How much the bar's own accents count against the free-floating cycles. |
| Balance | -1 to +1 | 0 | Which hand is in front: -1 the left, +1 the right. |
| Length | 0.2 to 1.5 | 0.9 | Note length as a share of a step. Above 1 the notes overlap. |
| Variation | 0 to 9999 | 0 | A variation by number. 0 is none. |
| Amount | 0 to 1 | 0.5 | How far a variation goes. |

The panel keys are `Key`, `Mode`, `Rate`, `Left hand/Accent 1` to `3`, `Right hand/Accent 1` to `4`, `Tension`, `Balance`, `Length`, `Variation` and `Amount`. `ui.json` lays them out on the 720 unit panel: the three menus on top, the left hand's bars next to the right hand's, then the knobs and the variation. It uses the Night swatch in the minimal style, for slow piano at night.

## How the rules work

Each step's velocity comes from `rawAttack` in `pp_core.cpp`, with the contour depth at 1 and the meeting weight at 0.3, as in Pattern Player's preset:

- **The hand's own contour:** the mean of its accents, plus the step's accent minus that mean, scaled by 1 + 0.6 x the hand's place in the balance.
- **The bar:** Tension x 14 x (the bar weight minus 0.3). The weight is 1 on the downbeat, 0.6 on the half bar, 0.35 on a beat, 0.15 on an 8th and 0 elsewhere.
- **The meeting points:** where both cycles restart together (every 12 steps), 0.3 x 14 more.
- **Balance:** 10 x the hand's place in front, added to its level.

Everything about a step is a function of its position in the song, counted from position 0. So starting at bar 5 sounds the same as arriving there.

A variation is Pattern Player's `generateVariation` with notes locked. Variation N is Pattern Player's seed N, and gives exactly the same moves there. A move rotates or swaps the accents, leans on one accent, thins the weakest positions, changes the length or walks the contour. A new Variation or Amount starts on the next bar line while playing, and at once while stopped.

## What it keeps and what it leaves out

**Kept from Pattern Player:** the two hands and the shared pulse; preset 0 and material 0, "3 vs 4 ostinato (brief)"; progression 0, "1-6-4-1 (brief)", with its colours; natural minor and major from `kModes`; the key; five of the rates; the accent rules of `rawAttack` and `barWeight`; density; the accent walk; the variation generator; the engine's note handling (`Engine::process`, `startNote`, `flush`); and the variation switch on the bar line from `PluginProcessor::processBlock`.

**Left out,** as the spec says: the other modes, progressions and materials, the chord map, the held chord, capture, echo, breath, the phrase arc, articulation, per-hand rates, levels, octaves, mutes and channels, the polyrhythm timings, favourites and history, and the Perform page. The variation number is a parameter, so a DAW project brings the same accents back.

## Intentional differences

1. **No Lean.** Pattern Player's variations may move a hand up to 40 ms early or late. Two Hands generates those moves, so every other move of a seed is Pattern Player's own, but never applies them: a variation changes accents, density and length, never when a note starts. 80 of the first 300 seeds at Amount 0.5 have a Lean move.
2. **Amount is part of the variation.** In Pattern Player the amount is frozen when Generate is pressed. Here Variation and Amount are both parameters, so changing either is a new variation, and it waits for the next bar line like one.
3. **One Length for both hands,** where Pattern Player has a length per hand. Variations still change one hand's length.
4. **The display shows the accent position,** after any walk, so the lit bar is the accent being played. Pattern Player's dots show the note position.
5. **The block is split at the bar line by sample,** not by working out a new song position for the second part. It gives the same notes, and cannot round a sample the other way.
6. **No fused multiply-add.** The arithmetic is written one product per statement, so the native plugin and the browser agree to the bit. Pattern Player, compiled with clang's default flags on Apple Silicon, fuses some of them, and then a velocity that falls exactly on a half (16.5, say) can round the other way: 24 notes in the 224,633 of the gold test. Compiled with `-ffp-contract=off`, Pattern Player gives exactly what Two Hands gives.

Not a difference, but worth knowing: the register follows Pattern Player. The left hand's root sits between F2 and E3, and the tonic of the figure between F#3 and F4, so moving the key from E to F drops the left hand an octave, and from F to F# the right hand. To transpose straight up from C instead, use `center + key` as the centre in `buildPattern`.

## How it was verified

`test/run.sh` builds and runs everything. Real output, 8 November 2026:

```
== Native harness (clang++ -std=c++17, default flags)
Block sizes: the same events at 37, 64, 128, 480 and 1024 samples
  defaults    768 notes,   2304 events, identical at all five sizes
  busy        576 notes,   1728 events, identical at all five sizes
  changes     412 notes,   1236 events, identical at all five sizes
Song position: starting at bar 5 sounds the same as arriving there
  defaults                    128 notes from bar 5 on, identical
  dotted 1/8, variation 42     42 notes from bar 5 on, identical
  triplet 1/8, variation 9     96 notes from bar 5 on, identical
Notes: none stuck, none overlapping on a channel, all in range
  a loop back mid-note ends the 2 sounding notes at offset 0 of that block
  stopping mid-note sends 4 note offs at offset 0, and nothing else while stopped
  a tempo or song position that is not a number ends the 2 sounding notes and starts none
  128 runs, 44188 notes, every one ended once
3 against 4: the hands realign every 12 steps
  64 steps: the cycles start together 6 times, at steps 0, 12, 24, 36, 48 and 60, with +4.2 on the accent
Tension and Balance: velocities follow the rules of rawAttack
  7680 notes at 20 settings, every velocity as the rules say
  Tension 0 to 1: right-hand downbeats 77.3 to 87.3, its other notes 57.3 to 55.1
  Balance 0 to +1: right hand 56.2 to 65.7, left hand 54.4 to 44.2
Variations: a seed gives the same accents every time, and never moves a note
  400 seeds: 392 change the playing, 236 of them the velocities only, 146 the lengths, 20 thin the notes;
  no note on moved or changed pitch (0 new, 0 moved)
  set mid-bar while playing, a variation or an Amount starts at the next bar line; while stopped, at once
Key and Mode: the pitches
  C minor: left C3 Eb3 G3, Ab2 C3 Eb3, F2 Ab2 C3, C3 Eb3 G3; right G4 Eb4 D4 Eb4 throughout
  C major: left C3 E3 G3, A2 C3 E3, F2 A2 C3, C3 E3 G3; right G4 E4 D4 E4 throughout
  all 12 keys in both modes: 672 pitches as the theory says
Rate: the step length of each choice
  1/32        a step every   3000 samples at 120 BPM and 48 kHz (128 notes in 4 bars), lasting 0.9 of it
  1/16        a step every   6000 samples at 120 BPM and 48 kHz (64 notes in 4 bars), lasting 0.9 of it
  1/8         a step every  12000 samples at 120 BPM and 48 kHz (32 notes in 4 bars), lasting 0.9 of it
  dotted 1/8  a step every  18000 samples at 120 BPM and 48 kHz (22 notes in 4 bars), lasting 0.9 of it
  triplet 1/8 a step every   8000 samples at 120 BPM and 48 kHz (48 notes in 4 bars), lasting 0.9 of it
  and to the rounded sample at 97 BPM and 44.1 kHz
Wrote 27 scripts for the WebAssembly comparison
PASS: 2878878 checks, 0 failed

== Native harness again, with fused multiply-add off
PASS: 2878878 checks, 0 failed

== Gold test against Pattern Player
-- both built with fused multiply-add off: the source as written
Gold: Two Hands against the Pattern Player engine (pp_core, pp_theory)
  preset 0, C minor - 3 vs 4 (brief), shared 1/16, 24 bars: 768 notes, identical at blocks of 128, 37 and 1024
  12 keys x 2 modes x 5 rates, Tension, Balance, Length and accents moved: 120 of 120 runs identical, 42240 notes
  seeds 1 to 300 at Amount 0.5 and 1 to 100 at 0, 0.25 and 1: 600 of 600 runs identical, 178582 notes
  (151 of those variations have a Lean move, which Two Hands leaves out)
  against Pattern Player with Lean kept: 80 of 300 seeds differ, all of them seeds with a Lean move
  what three seeds do, at Amount 0.5:
       1: right length -0.14, left density -0.23, right accents walk every 1 cycles
      42: left density -0.21, left length +0.26, left accent 3 +7.1
    2026: right length +0.24, left accent 3 -10.1, left density -0.24
  changes while playing, loops and jumps, tempo changes, stops, 44.1 kHz in 3/4: 5 of 5 identical
PASS: 728 runs, 224633 notes compared, 0 failed
-- both built with the default flags, as Pattern Player ships
  (the same lines, then)
  Pattern Player built with fused multiply-add: 24 notes 1 apart in velocity, at an exact half
PASS: 728 runs, 224633 notes compared, 0 failed

== WebAssembly build and comparison
logic.wasm: 12142 bytes
WebAssembly against native: 27 of 27 scripts identical, 33750 events (11250 notes) compared to the sample

== Source rules (shell/check-project.sh)
check-project: ok (dsp.dsp, logic.cpp, ui.json)

== Web checks (the logic rules, the strict gate, compileLogic, checkMidiTool, the layout)
checkLogicRules: 0 problems
gateLogicSource: 0 problems
compileLogic: ok, 12142 bytes, API 2, 15 controls, 0 sound controls, 272 ms with clang++, hash be0347ac0307
checkMidiTool: ok
{ "ok": true, "problems": [], "stats": { "notes": 512, "notesPerBar": 32, "channels": [ 1, 2 ], "ms": 110 } }
panel keys: Key, Mode, Rate, Left hand/Accent 1, Left hand/Accent 2, Left hand/Accent 3, Right hand/Accent 1,
  Right hand/Accent 2, Right hand/Accent 3, Right hand/Accent 4, Tension, Balance, Length, Variation, Amount
ui.json: 15 items, text contrast 12.48 to 1
web checks: all passed

Two Hands: everything passed
```

The tests, in `test/`:

| File | What it does |
|---|---|
| `harness.cpp` | The native checks, plain `clang++ -std=c++17` with no JUCE. The velocity rules are written out again from `pp_core.cpp`, so a slip in the port shows. |
| `gold.cpp` | Builds `pp_core.cpp` and `pp_theory.cpp` from a checkout of Pattern Player in place (`PATTERN_PLAYER`, which has no default; without it the gold test is skipped) and compares every note on and off: time, channel, pitch and velocity. |
| `runner.h` | A scripted host: blocks, parameter changes, jumps, tempo changes and stops. |
| `wasm-compare.mjs` | Replays the harness's scripts through the WebAssembly build, through the ABI, and compares every event. |
| `web-check.ts` | The logic rules, the strict gate, `compileLogic`, `checkMidiTool`, and the layout against the compiled controls. It reads the site's own code, so it is in Pangea's own repository only, and `run.sh` skips it without it. |
| `run.sh` | All of the above, plus `shell/check-project.sh`. |

Licence: `logic.cpp`, this README and `test/` derive from Lukasz Furman's Pattern Player, by permission. Since 25 November 2026 he offers them, as they are here, under the GNU Affero General Public License, version 3 or later, as well as under his own licence, at your choice: `shell/platform/web/FELT-LICENSE.md` has both, and the lines below the credit at the top of `logic.cpp` say so.
