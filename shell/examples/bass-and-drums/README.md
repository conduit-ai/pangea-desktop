# Bass and Drums

A bass line and a drum groove in one tool: the MIDI shelf's example of two components (`docs/midi-shelf.md`), Line and Drums. It makes no sound: it plays the bass on MIDI channel 1 and the drums on channel 10, following the host's transport. Drums shares the tool, so it plays on channel 10, the drum channel an instrument expects, and the bass keeps channel 1; alone, Drums may stay on channel 1 (Pocket Drums).

`logic.cpp` is the file as Pangea saves it: the tool's own code, an example of what Pangea saves for a tool asked for a bass line with drums, with the code of the kit, Line and Drums filled in after its `#include "logic_api.h"` line. Each component writes its events, the second after the first, and `shelf::sortEvents` puts them in order. After a change to `library/midi`, which is in Pangea's own repository only, fill it again there with

    npx tsx library/midi/test/fill.ts shell/examples/bass-and-drums/logic.cpp

The bass lines are those of the scenes Roller, Gallop and Offbeat bass of Bass Hand (`furmanlukasz/bass-hand` at `1af3fcb`, `core/bs_scenes.cpp`), and the drum lanes those of the scenes Broken beat, Half time and Tresillo of Drum Hand (`furmanlukasz/drum-hand` at `e297ba1`, `core/dh_scenes.cpp`), both by Lukasz Furman, by permission; `library/midi/NOTICE.md` and `library/midi/LICENSE.md`.

## The controls

| Control | Range | Default | What it does |
|---|---|---|---|
| Key | C to B | E | The key. |
| Mode | Minor, Dorian, Phrygian | Minor | The scale the chords are built from (a 1-2 vamp, two bars a chord). |
| Line | Roller, Gallop, Offbeat | Roller | The bass line, walking up into each new chord. |
| Groove | Broken beat, Half time, Tresillo | Broken beat | The kick, snare, closed hat and ghost lanes. |
| Ghosts | 0 to 1 | 0.6 | The share of the ghost strokes that play; none below 0.05. |
| Fill | Off, Every 4 bars | Every 4 bars | A snare roll at the end of every fourth bar. |
| Swing | 0 to 1 | 0 | Both parts' sixteenths swung, straight to 0.75. |
| Intensity | 0 to 1 | 0.5 | Both parts louder and fuller above the middle, softer and sparser below. |

## How it was verified

In Pangea's own repository only, `library/midi/test/run.sh`: the native harness (every control at each of its values, changes while playing, loops, tempo changes, stops, 44.1 kHz in 3/4, blocks of 37 to 1024), its WebAssembly build against the native one to the sample, `shell/check-project.sh`, the desktop app's checker, the logic rules, the gate, the server's compile and check (channels 1 and 10), the fuel a block takes and the native step budget: the heaviest pair's figures in `docs/midi-shelf.md`.

Licence: the Hand family code in `logic.cpp` is under the licence its block's first line names, `library/midi/LICENSE.md`, which is in Pangea's own repository only; its text is the first part of `shell/platform/web/FELT-LICENSE.md`. Since 25 November 2026 its author also offers the whole of `logic.cpp`, as it is here, under the GNU Affero General Public License, version 3 or later, at your choice, as `shell/platform/web/FELT-LICENSE.md` says. The block's line itself is left as the MIDI shelf writes it; the two lines after the block say so.
