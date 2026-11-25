#!/bin/bash
# test-host.sh: the Pangea plugin's checks (docs/pangea-plugin.md), on real modules.
#
#   tools/test-host.sh <pangea-host-check> <Pangea.vst3> [<Pangea Instrument.vst3>]
#
# Builds modules with build-module.sh into a scratch host folder (Tape Drift v1 and a v2 with a new
# Drive control and a new Mix default, Poly Saw, Dub Keys with its effect section, Acid Line with its
# logic module, Two Hands, the Runaway fixture, Kick Duck with its sidechain (docs/sidechain.md), and a module with an
# old format number), writes the folder's library.json as the
# desktop app would, and runs pangea-host-check against it, then its web panel checks in the GUI
# session. build-host.sh --check runs this. The meters (decision 91) have two fixtures of their own,
# tools/fixtures/meters: the Step Drive Filter, made by the Pangea team to decision 89's convention,
# and Meter Keys, an instrument with a meter in its voice and one in its effect section. Their expected.json, what
# the site finds in the same code (held there by src/lib/faust/plugin-meters.test.ts), is copied in.
# Song Gate (tools/fixtures/song-gate) is decision 123's song-synced example (until decision 124):
# the song's play state and position.
set -euo pipefail

SHELL_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CHECK_BIN="$1"; shift
HOST="$(mktemp -d "${TMPDIR:-/tmp}/pangea-host-test.XXXXXX")"
trap 'rm -rf "$HOST"' EXIT
export PANGEA_HOST_DIR="$HOST"

module() {   # module <project dir> <id> <version> [--allow-logic]
    local out
    out="$(bash "$SHELL_DIR/build-module.sh" "$1" "$HOST/modules/$2/v$3" --project-id "$2" --version-number "$3" ${4:-} | tail -1)"
    python3 -I -c 'import json,sys; d=json.loads(sys.argv[1]); sys.exit(0 if d.get("ok") else 1)' "$out" \
        || { echo "module $2 v$3 did not build: $out" >&2; exit 1; }
    echo "  built $2 v$3 ($(python3 -I -c 'import json,sys; print(json.loads(sys.argv[1])["seconds"])' "$out") s)"
}

echo "Building the test modules into $HOST"
V2="$HOST/projects/tape-drift-v2"
mkdir -p "$V2"
cp "$SHELL_DIR/examples/tape-drift/plugin.json" "$SHELL_DIR/examples/tape-drift/ui.json" "$V2/"
# v2 names its owner and the site, as a plugin.json from the site does, for the web panel's footer.
python3 -I - "$V2/plugin.json" <<'EOF'
import json, sys
p = json.load(open(sys.argv[1]))
p["owner"] = {"name": "Test Member", "email": "test@example.invalid"}
p["site"] = "https://pangea.example"
json.dump(p, open(sys.argv[1], "w"), indent=2)
EOF
python3 -I - "$SHELL_DIR/examples/tape-drift/dsp.dsp" "$V2/dsp.dsp" <<'EOF'
import sys
s = open(sys.argv[1]).read()
s = s.replace('mix      = hslider("[5] Mix [style:knob]", 0.35,',
              'drive    = hslider("[6] Drive [style:knob]", 2, 1, 8, 0.01) : si.smoo;\nmix      = hslider("[5] Mix [style:knob]", 0.6,')
s = s.replace('safe = max(-1) : min(1);', 'safe = *(drive) : ma.tanh : /(drive) : max(-1) : min(1);')
assert "Drive" in s and "0.6," in s
open(sys.argv[2], "w").write(s)
EOF
module "$SHELL_DIR/examples/tape-drift" test-tape-drift 1
module "$V2" test-tape-drift 2
module "$SHELL_DIR/examples/poly-saw" test-poly-saw 1
module "$SHELL_DIR/examples/dub-keys" test-dub-keys 1
module "$SHELL_DIR/examples/acid-line" test-acid-line 1 --allow-logic
module "$SHELL_DIR/examples/kick-duck" test-kick-duck 1
# Two Hands in the Felt look with its XY pad marked (tools/fixtures/felt-xy): the plugin's own web
# panel must draw the pad on the marked pair, as the site does for the same marks.
TWO="$HOST/projects/two-hands-felt"
mkdir -p "$TWO"
cp "$SHELL_DIR/examples/two-hands/plugin.json" "$SHELL_DIR/examples/two-hands/dsp.dsp" "$TWO/"
python3 -I - "$SHELL_DIR/examples/two-hands" "$TWO" "$SHELL_DIR/tools/fixtures/felt-xy" <<'EOF'
import json, re, sys
src, dst, fix = sys.argv[1:4]
marks = json.load(open(fix + "/marks.json"))
logic = open(src + "/logic.cpp").read()
for name, axis in marks.items():
    logic, n = re.subn(r'(\{\s*"' + re.escape(name) + r'",[^"]*")([^"]*)("\s*\})', r'\1\2[xy:' + axis + r']\3', logic)
    assert n == 1, name
open(dst + "/logic.cpp", "w").write(logic)
ui = json.load(open(src + "/ui.json"))
ui["look"] = json.load(open(fix + "/look.json"))
json.dump(ui, open(dst + "/ui.json", "w"), indent=2)
EOF
module "$TWO" test-two-hands 1 --allow-logic
module "$SHELL_DIR/examples/midi-steps" test-midi-steps 1 --allow-logic
module "$SHELL_DIR/tools/fixtures/runaway" test-runaway 1 --allow-logic
module "$SHELL_DIR/tools/fixtures/meters/step-drive" test-step-drive 1
module "$SHELL_DIR/tools/fixtures/meters/meter-keys" test-meter-keys 1
# Song Gate, decision 123's song-synced example: a gate that follows the song.
module "$SHELL_DIR/tools/fixtures/song-gate" test-song-gate 1
cp "$SHELL_DIR/tools/fixtures/meters/expected.json" "$HOST/meters-expected.json"
module "$SHELL_DIR/examples/glass-chorus" test-old-abi 1
python3 -I - "$HOST/modules/test-old-abi/v1/manifest.json" <<'EOF'
import json, sys
m = json.load(open(sys.argv[1])); m["abi"] = 999
json.dump(m, open(sys.argv[1], "w"))
EOF

python3 -I - "$HOST/library.json" "$$" <<'EOF'
import json, sys
# Where each is filed (decision 85), as the desktop app writes it from the site's list: the
# category's id and name, the tags, the other words it is found by as its category, and the summary. Runaway is not
# filed, as a plugin made before decision 85 is not, and Old Chorus has the fields of a
# library.json written before it: none of the five. The Step Drive Filter and Meter Keys, the meter fixtures of
# decision 91, are not filed either, and come last.
def p(pid, name, kind, head, mine=True, filed=None, summary=None, description=None):
    category, category_name, tags, keywords = filed or (None, None, [], [])
    return {"projectId": pid, "name": name, "slug": pid, "kind": kind, "headVersion": head, "mine": mine,
            "owner": "Test" if mine else "Someone else", "description": description, "updatedAt": None,
            "category": category, "categoryName": category_name, "tags": tags, "keywords": keywords,
            "summary": summary}
def before(entry):
    for key in ("category", "categoryName", "tags", "keywords", "summary"):
        del entry[key]
    return entry
DELAY = ("delay", "Delay", ["tape", "dub"], ["echo"])
lib = {"format": 1, "updatedAt": "2026-11-10T17:00:00Z", "appPid": int(sys.argv[2]), "signedIn": True,
       "member": {"name": "Test", "email": "test@example.invalid"}, "error": None,
       "plugins": [p("test-tape-drift", "Tape Drift", "effect", 1, filed=DELAY, summary="A tape echo that wobbles. Use it for dub throws."),
                   p("test-poly-saw", "Poly Saw", "instrument", 1, filed=("poly-synth", "Poly synth", ["pad", "subtractive"], ["polysynth", "polyphonic"]),
                     summary="A soft saw synth for pads."),
                   p("test-dub-keys", "Dub Keys", "instrument", 1, filed=("keys", "Keys", ["organ", "delay", "dub"], ["keyboard"]),
                     summary="An organ with an echo after it."),
                   p("test-acid-line", "Acid Line", "instrument", 1, filed=("mono-synth", "Mono synth", ["bass", "step-sequencer"], ["monosynth", "monophonic"]),
                     summary="A squelchy bass voice that plays its own line."),
                   p("test-two-hands", "Two Hands", "midi", 1, filed=("sequencer", "Sequencer", ["generative"], []), summary="Two patterns played together."),
                   p("test-midi-steps", "MIDI Steps", "midi", 1, filed=("arpeggiator", "Arpeggiator", ["velocity"], ["arp"]), summary="A stepped arpeggio."),
                   p("test-runaway", "Runaway", "midi", 1, description="A tool that never stops, to test the step budget."),
                   before(p("test-old-abi", "Old Chorus", "effect", 1, description="A chorus from before, with a short delay inside it.")),
                   p("test-not-mine", "Not Mine", "effect", 3, False, filed=("delay", "Delay", ["ping-pong"], ["echo"]), summary="Someone else's echo."),
                   p("test-step-drive", "Step Drive Filter", "effect", 1), p("test-meter-keys", "Meter Keys", "instrument", 1),
                   p("test-kick-duck", "Kick Duck", "effect", 1), p("test-song-gate", "Song Gate", "effect", 1)]}
json.dump(lib, open(sys.argv[1], "w"), indent=2)
EOF

"$CHECK_BIN" "$@"

# The web panel needs a window on screen, so its checks run in the GUI session (a window shows for a
# few seconds). The session does not pass the environment through, so env carries the folder.
echo "Web panel checks (in the GUI session)"
bash "$SHELL_DIR/tools/run-in-gui-session.sh" env PANGEA_HOST_DIR="$HOST" "$CHECK_BIN" --web
