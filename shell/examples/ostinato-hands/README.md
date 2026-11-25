# Ostinato Hands

Two piano hands in polymeter over chords, three against four by default, in D dorian: the MIDI tool built on Keys, a component of the MIDI shelf (`docs/midi-shelf.md`). It makes no sound: it plays the left hand on MIDI channel 1 and the right hand on channel 2 into a piano or another instrument, following the host's transport.

`logic.cpp` is the file as Pangea saves it: the tool's own code, an example of what Pangea saves for a tool asked for two hands, with the shelf's code filled in just after its `#include "logic_api.h"` line, between the two lines that start `// ---- MIDI components` and `// ---- End of MIDI components`. After a change to `library/midi`, which is in Pangea's own repository only, fill it again there with

    npx tsx library/midi/test/fill.ts shell/examples/ostinato-hands/logic.cpp

The four figures are the materials 3 vs 4 ostinato, 5 vs 4, 4 vs 3 turned and 6 vs 4 lilting of Pattern Player by Lukasz Furman (`furmanlukasz/pattern-player` at `7f99aa5`, `core/pp_theory.cpp`), by permission; `library/midi/NOTICE.md` and `library/midi/LICENSE.md`.

## The controls

| Control | Range | Default | What it does |
|---|---|---|---|
| Key | C to B | D | The key. |
| Mode | Minor, Dorian, Major, Mixolydian, Phrygian | Dorian | The scale the chords and the right hand are built from. |
| Chords | 1-6-4-1, 1-4 drift, 1-5-6-4, 6-4-1-5, One chord | 1-6-4-1 | The family progressions (shelf::Harmony). |
| Bars a chord | 1, 2, 4 | 2 | How long each chord lasts. |
| Figure | 3 against 4, 5 against 4, 4 against 3, 6 against 4 | 3 against 4 | The left hand in chord tones against the right in steps of the key, each with its accents. |
| Rate | 1/8, 1/16, 1/8 triplet, dotted 1/8 | 1/16 | The step of both hands. |
| Tension | 0 to 1 | 0.5 | How much the bar's own accents count against the hands' cycles. |
| Balance | -1 to 1 | 0 | Which hand is in front: -1 the left, 1 the right. |
| Length | 0.2 to 1.5 | 0.9 | The note length as a share of a step; above 1 they overlap. |
| Variation | 0 to 9999 | 0 | Pattern Player's variation by number: it moves the accents, thins a hand, changes a length or walks the accents, from the next bar line. 0 is none. |
| Amount | 0 to 1 | 0.5 | How far a variation goes. |

## How it was verified

In Pangea's own repository only, `library/midi/test/run.sh` runs it all: the native harness (`library/midi/test/example-harness.cpp`: 50 sessions, every control at each of its values, changes while playing, loops, tempo changes, stops, 44.1 kHz in 3/4, blocks of 37 to 1024), its WebAssembly build against the native one to the sample, `shell/check-project.sh`, the desktop app's checker, the logic rules, the gate, the server's compile and check, the fuel a block takes and the native step budget. Keys itself is held note for note to Pattern Player by `library/midi/test/keys-gold.cpp`.

Licence: the Hand family code in `logic.cpp` is under the licence its block's first line names, `library/midi/LICENSE.md`, which is in Pangea's own repository only; its text is the first part of `shell/platform/web/FELT-LICENSE.md`. Since 25 November 2026 its author also offers the whole of `logic.cpp`, as it is here, under the GNU Affero General Public License, version 3 or later, at your choice, as `shell/platform/web/FELT-LICENSE.md` says. The block's line itself is left as the MIDI shelf writes it; the two lines after the block say so.
