#!/bin/bash
# Pangea's bypass in the native plugin: the one parameter every plugin has (decision 75).
#
#   shell/tools/test-bypass.sh            with a bare clang++, a second:
#     1. tools/test-platform.sh: the fader, what a bypassed effect gives on each channel, and the
#        blend both processors use (platform/PangeaBypass.h).
#   shell/tools/test-bypass.sh --plugin   also builds a plugin of every kind
#     (build.sh --verify, in $PANGEA_BUILD_DIR or the shell's own build directory). --verify runs
#     pangea-render bypass-check on the processor and on the built VST3, loaded as a DAW loads it:
#     2. Tape Drift, an effect: its input to the bit while bypassed, on every kind of track, a
#        30 ms fade at each switch and back to its own render after; the switch on its panel;
#     3. Poly Saw, an instrument: silence, no voice left holding a note, the next note plays;
#     4. Dub Keys, an instrument with an effect section: the same, with the bypass last in the
#        parameter list, after the effect's own;
#     5. Acid Line, an instrument that its logic module plays: the module is run as stopped;
#     6. MIDI Steps, a MIDI tool: its notes end, it starts none, what it is sent passes;
#     7. three cases made here: an instrument whose logic module plays its voices, an effect with
#        a logic module, and an effect whose own code has a control named Bypass;
#     8. Tape Drift with the web panel compiled in (build.sh --web-editor), in a build directory
#        of its own: it builds, and its processor and VST3 pass the same check. The page itself
#        is checked in the Pangea plugin (build-host.sh --check).
#     About four minutes after the first build. Nothing is installed.
set -euo pipefail
SHELL_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
failures=0
ok() { echo "ok    $*"; }
bad() { echo "FAIL  $*"; failures=$((failures + 1)); }

echo "== 1. The fader, the dry sound and the blend, with a bare compiler"
if bash "$SHELL_DIR/tools/test-platform.sh" > "$OUT/platform.log" 2>&1; then ok "$(tail -1 "$OUT/platform.log")"; else bad "tools/test-platform.sh:"; sed 's/^/      /' "$OUT/platform.log"; fi

if [ "${1:-}" = "--plugin" ]; then
    BUILD="${PANGEA_BUILD_DIR:-$SHELL_DIR/build}"
    RENDER="$BUILD/pangea-render_artefacts/Release/pangea-render"
    field() { python3 -I -c 'import json, sys; print(json.loads(sys.argv[1]).get(sys.argv[2], ""))' "$1" "$2"; }

    # built <name> <project dir> [build.sh options]: builds with --verify, and the bypass check
    # that --verify runs must have passed. Prints what it found, less the lines that only repeat
    # for the VST3 what the processor showed.
    built() {
        local name="$1" dir="$2"; shift 2
        local line
        line="$(bash "$SHELL_DIR/build.sh" "$dir" --verify "$@" 2> "$OUT/build.err" | tee "$OUT/build.log" | tail -1)" || true
        if [ "$(field "$line" ok)" = "True" ] && [ "$(field "$line" verify)" = "passed" ] && grep -q "^bypass: bypass-check: passed" "$OUT/build.log"; then
            ok "$name builds, and the bypass check of build.sh --verify passes ($(grep -c '^bypass: ok' "$OUT/build.log") checks, the processor and the VST3)"
            sed -n 's/^bypass: \(ok    the processor: .*\)$/\1/p; s/^bypass: \(ok    the VST3 tells.*\)$/\1/p; s/^bypass: \(note .*\)$/\1/p' "$OUT/build.log" | cut -c1-260
        else
            bad "$name: $line"
            grep -E "^bypass: (FAIL|bypass-check)" "$OUT/build.log" | sed 's/^/      /' || true
            tail -5 "$OUT/build.err" | sed 's/^/      /'
        fi
    }
    # The processor's parameters as `pangea-render render` lists them: "id = value (name)". (The
    # render itself fails for a MIDI tool, which makes no sound: only the list is wanted here.)
    listing() { { "$RENDER" render "$OUT/take.wav" 0.3 || true; } | sed -n 's/^  \([a-z0-9_]* = .*\)$/\1/p'; }
    # last_is_bypass <name>: the bypass is the plugin's last parameter, off.
    last_is_bypass() {
        local last
        last="$(listing | tail -1)"
        [ "$last" = "pangea_bypass = Off (Bypass)" ] && ok "$1: its last parameter is Pangea's bypass, off: $last" \
            || bad "$1: its last parameter is \"$last\""
    }

    echo "== 2. Tape Drift, an effect"
    # Built with --native-editor: the checks below are of the shell's own panel, the only one that
    # can be drawn off screen. A plugin built without it opens with the component library's panel
    # in a web view (decision 77), which case 8 builds.
    built "Tape Drift" "$SHELL_DIR/examples/tape-drift" --native-editor
    last_is_bypass "Tape Drift"
    # The shell's own panel: the switch at the far right of the title row in every look, and the
    # controls drawn fainter while bypassed.
    switch_line() { sed -n 's/^editor: \(bypass switch .*\)$/\1/p'; }
    plain="$("$RENDER" editor "$OUT/in.png" | switch_line)"
    [ "$plain" = "bypass switch off, in the title row, its right edge 33 units from the window's, the controls drawn in full" ] \
        && ok "its panel has the switch: $plain" || bad "its panel says \"$plain\""
    bypassed="$("$RENDER" editor "$OUT/bypassed.png" --set pangea_bypass=1 | switch_line)"
    [ "$bypassed" = "bypass switch on, in the title row, its right edge 33 units from the window's, the controls drawn at 45 per cent" ] \
        && ok "bypassed: $bypassed" || bad "bypassed, its panel says \"$bypassed\""
    cmp -s "$OUT/in.png" "$OUT/bypassed.png" && bad "the panel is drawn the same bypassed and not" || ok "and the two pictures differ"
    looks=""
    for style in vintage minimal pedal playful retro; do
        got="$("$RENDER" editor "$OUT/$style.png" --ui "$SHELL_DIR/docs/screens/ui/$style.json" --set pangea_bypass=1 | switch_line)"
        case "$got" in
            "bypass switch on, in the title row, its right edge 33 units from the window's, the controls drawn at 45 per cent") looks="$looks $style" ;;
            *) bad "the $style look, bypassed, says \"$got\"" ;;
        esac
    done
    [ "$looks" = " vintage minimal pedal playful retro" ] && ok "the switch is in the title row of all five looks:$looks" || bad "the switch is missing from a look: found in$looks"

    echo "== 3. Poly Saw, an instrument"
    built "Poly Saw" "$SHELL_DIR/examples/poly-saw"
    last_is_bypass "Poly Saw"
    # Faust prints a line for a note off that no voice holds, and nothing may be printed from the
    # sound thread. The check sends such note offs: of the notes the bypass ended, and of one
    # played while bypassed. The shell must not have passed them on.
    if grep -q "Playing pitch" "$OUT/build.err"; then bad "Faust was asked for a note off that no voice held: $(grep -c "Playing pitch" "$OUT/build.err") lines printed"
    else ok "the note offs of notes the bypass ended, or never took, reach no voice: nothing is printed from the sound thread"; fi

    echo "== 4. Dub Keys, an instrument with an effect section"
    built "Dub Keys" "$SHELL_DIR/examples/dub-keys"
    last_is_bypass "Dub Keys"
    # An Audio Unit lists parameters by version hint: the voice's, then the effect's own, then the bypass.
    grep -q "^effect:   in an Audio Unit's list its own parameters come after the others (version hints 2 and 1)" "$OUT/build.log" \
        && grep -q "^effect:   and Pangea's bypass comes after them all (version hint 3)" "$OUT/build.log" \
        && ok "in an Audio Unit's list the effect's own parameters come after the voice's, and the bypass after them all (version hints 1, 2 and 3)" \
        || bad "the effect check does not place the bypass last: $(grep "^effect: .*Audio Unit\|^effect: .*bypass" "$OUT/build.log" | tr '\n' ' ')"

    echo "== 5. Acid Line, an instrument that its logic module plays"
    built "Acid Line" "$SHELL_DIR/examples/acid-line" --allow-logic
    grep -q "^bypass: ok    the processor: its logic module is run as stopped while bypassed" "$OUT/build.log" \
        && ok "its module is run as stopped while it is bypassed" || bad "nothing shows its module run as stopped"

    echo "== 6. MIDI Steps, a MIDI tool"
    built "MIDI Steps" "$SHELL_DIR/examples/midi-steps" --allow-logic
    last_is_bypass "MIDI Steps"
    grep -q "^host: ok    a bypass ends the tool's notes, lets the notes played in through, and ends those when it comes back" "$OUT/build.log" \
        && ok "and the host check's own bypass, which now moves the plugin's parameter, passes as before" \
        || bad "the host check's bypass line is missing or failed: $(grep "^host: .*bypass" "$OUT/build.log")"

    echo "== 7. Three cases made here"
    plugin_json() { # <dir> <name> <kind> <code>
        printf '{ "name": "%s", "slug": "%s", "kind": "%s", "bundleId": "com.pangea.%s", "manufacturerCode": "Pnga", "pluginCode": "%s", "version": "0.1.0", "description": "A bypass test case." }\n' \
            "$2" "$(basename "$1")" "$3" "$(basename "$1")" "$4" > "$1/plugin.json"
    }
    # An instrument whose logic module plays its voices: Poly Saw's sound with MIDI Steps' module.
    mkdir -p "$OUT/step-saw"
    cp "$SHELL_DIR/examples/poly-saw/dsp.dsp" "$OUT/step-saw/dsp.dsp"
    cp "$SHELL_DIR/examples/midi-steps/logic.cpp" "$OUT/step-saw/logic.cpp"
    plugin_json "$OUT/step-saw" "Step Saw" instrument Stsw
    built "Step Saw, an instrument whose logic module plays its voices" "$OUT/step-saw" --allow-logic
    grep -q "^bypass: ok    the processor: .* voices hold notes before the bypass, and none once its fade is over, its module's notes ended with the rest" "$OUT/build.log" \
        && grep -q "^bypass: ok    the processor: its logic module is run as stopped while bypassed, with none of its notes left on, and as playing after" "$OUT/build.log" \
        && ok "the notes its module had on end with the bypass, and the module starts none until it comes off" \
        || bad "nothing shows the module's notes ending"

    # An effect with a logic module: the module shuts the sound for half of every beat.
    mkdir -p "$OUT/gate-pulse"
    cat > "$OUT/gate-pulse/dsp.dsp" <<'EOF'
import("stdfaust.lib");
declare name "Gate Pulse";
open = nentry("open [logic:1]", 1, 0, 1, 1) : si.smoo;
depth = hslider("Depth", 0.8, 0, 1, 0.01);
process = par(i, 2, *(1 - depth * (1 - open)));
EOF
    cat > "$OUT/gate-pulse/logic.cpp" <<'EOF'
// Gate Pulse: opens the sound for the first half of every beat and shuts it for the second.
#include "logic_api.h"

static const char* const kLogicControls[] = { "open" };
static const int kNumLogicControls = 1;
static const LogicParam kLogicParams[] = { { "Rate", 1.0f, 4.0f, 2.0f, 1.0f } };
static const int kNumLogicParams = 1;

struct Logic
{
    bool wasPlaying = false;

    void prepare (double) { wasPlaying = false; }

    int process (const LogicBlock& b, LogicEvent* out, int maxEvents)
    {
        int count = 0;
        if (! b.playing)
        {
            if (wasPlaying && maxEvents > 0)
                out[count++] = { 0, 0, 1.0f };
            wasPlaying = false;
            return count;
        }
        wasPlaying = true;
        const double samplesPerBeat = b.sampleRate * 60.0 / b.bpm;
        const double half = samplesPerBeat / (double) b.params[0] * 0.5;
        const long long start = logic_round (b.ppqStart * samplesPerBeat);
        for (long long k = logic_floor ((double) start / half); ; ++k)
        {
            const long long at = logic_round ((double) k * half);
            if (at >= start + b.numSamples)
                break;
            if (at >= start && count < maxEvents)
                out[count++] = { (int) (at - start), 0, logic_mod (k, 2) == 0 ? 1.0f : 0.0f };
        }
        return count;
    }
};
EOF
    plugin_json "$OUT/gate-pulse" "Gate Pulse" effect Gtpl
    built "Gate Pulse, an effect with a logic module" "$OUT/gate-pulse" --allow-logic

    # An effect whose own code has a control named Bypass (the model is not yet told to write
    # none): two parameters of that name, with two ids, and Pangea's still bypasses the plugin.
    mkdir -p "$OUT/own-bypass"
    cat > "$OUT/own-bypass/dsp.dsp" <<'EOF'
import("stdfaust.lib");
declare name "Own Bypass";
off = checkbox("Bypass");
level = hslider("Level", 0.5, 0, 1, 0.01) : si.smoo;
process = par(i, 2, _ <: *(level * (1 - off)), *(off) :> _);
EOF
    plugin_json "$OUT/own-bypass" "Own Bypass" effect Ownb
    built "Own Bypass, an effect with a Bypass control of its own" "$OUT/own-bypass"
    names="$(listing | tr '\n' ';')"
    [ "$names" = "bypass = Off (Bypass);level = 0.50 (Level);pangea_bypass = Off (Bypass);" ] \
        && ok "its own control is bypass and Pangea's is pangea_bypass, two parameters of one name: $names" \
        || bad "its parameters are \"$names\""
    grep -q "^bypass: ok    the host sees 2 parameter named Bypass, one of them Pangea's" "$OUT/build.log" \
        && ok "a host shows two parameters named Bypass, and is told that Pangea's is the bypass" \
        || bad "the VST3's parameters named Bypass: $(grep "^bypass: .*named Bypass" "$OUT/build.log")"

    echo "== 8. Tape Drift with the web panel compiled in"
    line="$(PANGEA_BUILD_DIR="$OUT/build-web" bash "$SHELL_DIR/build.sh" "$SHELL_DIR/examples/tape-drift" --verify --web-editor 2> "$OUT/web.err" | tee "$OUT/web.log" | tail -1)" || true
    if [ "$(field "$line" ok)" = "True" ] && [ "$(field "$line" verify)" = "passed" ] && grep -q "^bypass: bypass-check: passed" "$OUT/web.log"; then
        ok "it builds with the library's panel in a web view, and its processor and VST3 pass the bypass check ($(grep -c '^bypass: ok' "$OUT/web.log") checks)"
    else
        bad "the build with --web-editor: $line"; tail -5 "$OUT/web.err" | sed 's/^/      /'
    fi
fi

if [ "$failures" -gt 0 ]; then echo "$failures bypass checks failed"; exit 1; fi
echo "all bypass checks passed"
