# Pocket Drums

A drum groove with ghost notes on the snare and a fill every few bars: the MIDI tool built on Drums, a component of the MIDI shelf (`docs/midi-shelf.md`). It makes no sound: it plays a drum kit on MIDI channel 1 into a drum sampler or a drum rack, following the host's transport.

`logic.cpp` is the file as Pangea saves it: the tool's own code, an example of what Pangea saves for a tool asked for a drum pattern, with the shelf's code filled in just after its `#include "logic_api.h"` line, between the two lines that start `// ---- MIDI components` and `// ---- End of MIDI components`. After a change to `library/midi`, which is in Pangea's own repository only, fill it again there with

    npx tsx library/midi/test/fill.ts shell/examples/pocket-drums/logic.cpp

The five grooves are the lanes of the scenes Broken beat, Half time, Open road, Tresillo and Offbeat hats of Drum Hand by Lukasz Furman (`furmanlukasz/drum-hand` at `e297ba1`, `core/dh_scenes.cpp`), by permission, with a ghost lane of Pangea's own where the scene has none; `library/midi/NOTICE.md` and `library/midi/LICENSE.md`.

## The controls

| Control | Range | Default | What it does |
|---|---|---|---|
| Groove | Broken beat, Half time, Open road, Tresillo, Offbeat hats | Broken beat | The lanes, each its own cycle and rate. |
| Ghosts | 0 to 1 | 0.7 | The share of the ghost strokes that play, the strongest first; none below 0.05. |
| Fill | Off, Snare roll, Toms down, Hat build, Kick pickup, Drop out | Snare roll | What plays at the end of the bar when a fill comes. |
| Fill every | 2 bars, 4 bars, 8 bars, Phrase ends | 4 bars | How often it comes; Phrase ends places it by the Variation. |
| Swing | 0 to 1 | 0 | The sixteenths swung from the groove's own feel to 0.75. |
| Human | 0 to 1 | 0.3 | Seeded human timing and touch. |
| Intensity | 0 to 1 | 0.5 | Louder, stronger accents and the chances sounding more often above the middle; softer and thinner below. |
| Kit | General MIDI, Drum rack | General MIDI | The note map: General MIDI (36 kick, 38 snare) or a drum rack's pads from 36 in order. |
| Variation | 0 to 999 | 0 | The seed of the chances, the human timing and where phrase end fills fall. |

## How it was verified

In Pangea's own repository only, `library/midi/test/run.sh` runs it all: the native harness (`library/midi/test/example-harness.cpp`: 34 sessions, every control at each of its values, changes while playing, loops, tempo changes, stops, 44.1 kHz in 3/4, blocks of 37 to 1024), its WebAssembly build against the native one to the sample, `shell/check-project.sh`, the desktop app's checker, the logic rules, the gate, the server's compile and check, the fuel a block takes and the native step budget. Drums itself is held note for note to Drum Hand by `library/midi/test/drums-gold.cpp`.

Licence: the Hand family code in `logic.cpp` is under the licence its block's first line names, `library/midi/LICENSE.md`, which is in Pangea's own repository only; its text is the first part of `shell/platform/web/FELT-LICENSE.md`. Since 25 November 2026 its author also offers the whole of `logic.cpp`, as it is here, under the GNU Affero General Public License, version 3 or later, at your choice, as `shell/platform/web/FELT-LICENSE.md` says. The block's line itself is left as the MIDI shelf writes it; the two lines after the block say so.
