# Hands and Drums

Two piano hands and a drum groove in one tool: the MIDI shelf's example of two components (`docs/midi-shelf.md`), Keys and Drums. It makes no sound: it plays the left hand on MIDI channel 1, the right hand on channel 2 and the drums on channel 10, following the host's transport. Drums shares the tool, so it plays on channel 10, the drum channel an instrument expects, and no hand does; alone, Drums may stay on channel 1 (Pocket Drums).

`logic.cpp` is the file as Pangea saves it: the tool's own code, an example of what Pangea saves for a tool asked for two hands with drums, with the code of the kit, Drums and Keys filled in after its `#include "logic_api.h"` line. Each component writes its events, the second after the first, and `shelf::sortEvents` puts them in order. After a change to `library/midi`, which is in Pangea's own repository only, fill it again there with

    npx tsx library/midi/test/fill.ts shell/examples/hands-and-drums/logic.cpp

The figures are those of the materials 3 vs 4 ostinato and 5 vs 4 of Pattern Player (`furmanlukasz/pattern-player` at `7f99aa5`, `core/pp_theory.cpp`), and the drum lanes those of the scenes Open road and Offbeat hats of Drum Hand (`furmanlukasz/drum-hand` at `e297ba1`, `core/dh_scenes.cpp`), both by Lukasz Furman, by permission; `library/midi/NOTICE.md` and `library/midi/LICENSE.md`.

## The controls

| Control | Range | Default | What it does |
|---|---|---|---|
| Key | C to B | D | The key. |
| Mode | Minor, Dorian, Mixolydian | Dorian | The scale the chords and the right hand are built from. |
| Chords | 1-6-4-1, 1-4 drift, 6-4-1-5 | 1-6-4-1 | The family progressions, two bars a chord. |
| Figure | 3 against 4, 5 against 4 | 3 against 4 | The left hand in chord tones against the right in steps of the key, in sixteenths. |
| Groove | Open road, Offbeat hats | Open road | The drum lanes. |
| Fill | Off, Every 4 bars | Every 4 bars | A hat build at the end of every fourth bar. |
| Tension | 0 to 1 | 0.5 | How much the bar's own accents count against the hands' cycles. |
| Balance | -1 to 1 | 0 | Which hand is in front. |
| Intensity | 0 to 1 | 0.5 | The drums louder and fuller above the middle, softer and thinner below. |

## How it was verified

In Pangea's own repository only, `library/midi/test/run.sh`: the native harness (every control at each of its values, changes while playing, loops, tempo changes, stops, 44.1 kHz in 3/4, blocks of 37 to 1024), its WebAssembly build against the native one to the sample, `shell/check-project.sh`, the desktop app's checker, the logic rules, the gate, the server's compile and check (channels 1, 2 and 10), the fuel a block takes and the native step budget: the heaviest pair's figures in `docs/midi-shelf.md`.

Licence: the Hand family code in `logic.cpp` is under the licence its block's first line names, `library/midi/LICENSE.md`, which is in Pangea's own repository only; its text is the first part of `shell/platform/web/FELT-LICENSE.md`. Since 25 November 2026 its author also offers the whole of `logic.cpp`, as it is here, under the GNU Affero General Public License, version 3 or later, at your choice, as `shell/platform/web/FELT-LICENSE.md` says. The block's line itself is left as the MIDI shelf writes it; the two lines after the block say so.
