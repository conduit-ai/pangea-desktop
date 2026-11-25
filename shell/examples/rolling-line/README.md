# Rolling Line

A rolling bass that follows the chords and walks into each new one: the MIDI tool built on Line, the first component of the MIDI shelf (`docs/midi-shelf.md`). It makes no sound: it plays one bass voice on MIDI channel 1 into another instrument, following the host's transport.

`logic.cpp` is the file as Pangea saves it: the tool's own code, an example of what Pangea saves for a tool asked for a bass line, with the shelf's code filled in just after its `#include "logic_api.h"` line, between the two lines that start `// ---- MIDI components` and `// ---- End of MIDI components`. After a change to `library/midi`, which is in Pangea's own repository only, fill it again there with

    npx tsx library/midi/test/fill.ts shell/examples/rolling-line/logic.cpp

## The controls

| Control | Range | Default | What it does |
|---|---|---|---|
| Key | C to B | E | The key. |
| Mode | Minor, Dorian, Phrygian, Phrygian dominant, Harmonic minor | Phrygian dominant | The scale the chords are built from. |
| Chords | 1-2 vamp, 1-6-4-1, 1-7-6-7, 6-4-1-5, One chord | 1-2 vamp | The family progressions (shelf::Harmony). |
| Bars a chord | 1, 2, 4 | 2 | How long each chord lasts. |
| Line | Roller, Gallop, Octaves, Ghost funk, Offbeat | Roller | The cycle, from Bass Hand's scenes Roller, Gallop, Rolling octaves, Sixteenths with ghosts and Offbeat bass. |
| Ghosts | 0 to 1 | 0 | Dead notes added on the rests before the strongest notes. |
| Walk | Off, Walk up, Walk down, Pickup, Slide in | Walk up | The fill at the end of the bar before each change of chord. |
| Swing | 0 to 1 | 0 | Swing on the sixteenths, straight to 0.75. |
| Intensity | 0 to 1 | 0.5 | Louder, stronger accents and fuller above the middle; softer and sparser below. |
| Variation | 0 to 999 | 0 | The seed of the human timing and the chances. |

## How it was verified

In Pangea's own repository only, `library/midi/test/run.sh` runs it all: the native harness (32 sessions, every control, changes while playing, loops, tempo changes, stops, 44.1 kHz in 3/4, blocks of 37 to 1024), its WebAssembly build against the native one to the sample, `shell/check-project.sh`, the desktop app's checker, the logic rules, the gate, the server's compile and check, the fuel a block takes and the native step budget. `shell/build.sh` builds it, with `--verify`, as an instrument with MIDI out and a Logic MIDI FX.

Licence: the Hand family code in `logic.cpp` is under the licence its block's first line names, `library/midi/LICENSE.md`, which is in Pangea's own repository only; its text is the first part of `shell/platform/web/FELT-LICENSE.md`. Since 25 November 2026 its author also offers the whole of `logic.cpp`, as it is here, under the GNU Affero General Public License, version 3 or later, at your choice, as `shell/platform/web/FELT-LICENSE.md` says. The block's line itself is left as the MIDI shelf writes it; the two lines after the block say so.
