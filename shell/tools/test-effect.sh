#!/bin/bash
# An instrument's effect section, the stage its voices share, in the native plugin (decision 73).
#
#   shell/tools/test-effect.sh            with faust and a bare clang++, a few seconds:
#     1. tools/build-effect.sh on the examples and on small cases: which have an effect section,
#        which have none, which leave one out because it does not compile or is too wide, the
#        channels each takes and gives, as faustwasm adapts them in the builder, and that no
#        source can use the names of the program the effect is compiled in;
#     2. build-module.sh: an instrument's module carries its effect section, and a module of any
#        other kind, or of an instrument without one or with one that is left out, carries none.
#   shell/tools/test-effect.sh --plugin   also builds six instruments as real plugins
#     (build.sh --verify, in $PANGEA_BUILD_DIR or the shell's own build directory):
#     3. Dub Keys: the effect's controls are parameters under the site's keys, Tone is one control
#        of the voices and of the effect, Level and Mix change the render in the processor and in
#        the built VST3, the echo follows the host's tempo, and a layout finds the effect's
#        controls by key;
#     4. Effect Cases (tools/fixtures/effect-cases): one definition read by both is one
#        parameter, two definitions under one key are two, a group both have is one group, and
#        no two parameters share an id;
#     5. Effect Root (tools/fixtures/effect-root): an effect whose only group Faust makes its
#        root keeps its controls apart from a voice control of the same key;
#     6. an instrument whose effect does not compile: built without it, and the build says so;
#     7. an instrument whose effect changes nothing that can be heard: the build's own check
#        fails, though a control the voices read too changes the sound;
#     8. Poly Saw, which has no effect section: built with the parameters it always had.
#     About two and a half minutes after the first build.
set -euo pipefail
SHELL_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
failures=0
ok() { echo "ok    $*"; }
bad() { echo "FAIL  $*"; failures=$((failures + 1)); }

# A small instrument: one voice with the given outputs and an effect (or none) as given.
write_case() { # <dir> <process> [<effect>]
    mkdir -p "$1"
    {
        echo 'import("stdfaust.lib");'
        echo 'declare options "[nvoices:8]";'
        echo 'freq = hslider("freq", 440, 20, 20000, 0.01); gain = hslider("gain", 0.5, 0, 1, 0.01); gate = button("gate");'
        echo 'lvl = hslider("Level", 0.5, 0, 1, 0.01);'
        echo "process = $2;"
        [ -z "${3:-}" ] || echo "effect = $3;"
    } > "$1/dsp.dsp"
}

echo "== 1. Which code has an effect section, and its channels"
# expect <name> <source dsp> <what build-effect.sh must print, or its start>
expect() {
    mkdir -p "$OUT/$1"
    cp "$2" "$OUT/$1/dsp.dsp"
    local got
    got="$(bash "$SHELL_DIR/tools/build-effect.sh" "$OUT/$1" "$OUT/$1/FaustEffect.h")" || { bad "$1: build-effect.sh could not run"; return; }
    case "$got" in
        "$3"*) ;;
        *) bad "$1: expected \"$3\", got \"$got\""; return ;;
    esac
    case "$got" in
        built*) grep -q "class mydsp_effect" "$OUT/$1/FaustEffect.h" || { bad "$1: no class mydsp_effect was written"; return; } ;;
        *) [ ! -e "$OUT/$1/FaustEffect.h" ] || { bad "$1: a class was written for \"$got\""; return; } ;;
    esac
    ok "$1: $(printf '%s' "$got" | cut -c1-110)"
}
shape() { # <name> <process> <effect> <expected>
    write_case "$OUT/src-$1" "$2" "$3"
    expect "$1" "$OUT/src-$1/dsp.dsp" "$4"
}
mono='os.osc(freq) * gate * gain'
stereo='os.osc(freq) * gate * gain <: _, _'
expect "Dub Keys" "$SHELL_DIR/examples/dub-keys/dsp.dsp" "built 2 2"
expect "Poly Saw" "$SHELL_DIR/examples/poly-saw/dsp.dsp" "none"
expect "Acid Line" "$SHELL_DIR/examples/acid-line/dsp.dsp" "none"
# The voice's outputs to the effect's inputs: as they are, split, or summed. The effect's outputs,
# then silent channels up to the widest count. The same table the builder's compile gives.
shape "stereo voice, stereo effect" "$stereo" 'par(i, 2, *(lvl))' "built 2 2"
shape "mono voice, stereo effect" "$mono" 'par(i, 2, *(lvl))' "built 1 2"
shape "mono voice, mono to stereo effect" "$mono" '*(lvl) <: _, _' "built 1 2"
shape "mono voice, mono effect" "$mono" '*(lvl)' "built 1 1"
shape "stereo voice, mono effect" "$stereo" '*(lvl)' "built 2 2"
shape "stereo voice, mono to stereo effect" "$stereo" '*(lvl) <: _, _' "built 2 2"
shape "stereo voice, stereo to mono effect" "$stereo" '+ : *(lvl)' "built 2 2"
shape "stereo voice, four to stereo effect" "$stereo" '_, _, _, _ :> par(i, 2, *(lvl))' "built 2 4"
shape "stereo voice, three input effect" "$stereo" '_, _, ! : par(i, 2, *(lvl))' "left-out "
shape "stereo voice, an effect with no input" "$stereo" 'os.osc(100) * lvl <: _, _' "left-out "
# Line 6 is the effect's own line in dsp.dsp: the compiler's line numbers are the source's.
shape "an effect that does not compile" "$stereo" 'par(i, 2, *(lvl)) : nothingHere' "left-out line 6: ERROR : undefined symbol : nothingHere"
# The effect is compiled inside a program of this script's own. A source that names what that
# program defines must find nothing, as it finds nothing in the builder: the names carry a hash of
# the source itself. (Before they did, this effect built here and nowhere else.)
shape "an effect that names the program it is compiled in" "$stereo" 'pangeaBus(2) : par(i, 2, *(lvl))' "left-out line 6: ERROR : undefined symbol : pangeaBus"
hash="$(shasum -a 256 "$OUT/src-stereo voice, stereo effect/dsp.dsp" | cut -c1-32)"
grep -q "pangea_${hash}_bus(n)" "$OUT/stereo voice, stereo effect/effect/dsp.dsp" && ok "that program's own names carry the hash of the source: pangea_${hash}_..." \
    || bad "the program's names do not carry the source's hash $hash"
# More outputs than the shell has channel pointers for: left out, never built.
shape "an effect with sixteen outputs" "$stereo" 'par(i, 2, _ <: par(j, 8, _))' "left-out the effect section gives 16 channels, and a plugin carries 8 at most"
# Called with paths that are not whole, it works all the same.
mkdir -p "$OUT/relative" && cp "$OUT/src-stereo voice, stereo effect/dsp.dsp" "$OUT/relative/dsp.dsp"
got="$(cd "$OUT" && bash "$SHELL_DIR/tools/build-effect.sh" relative relative/FaustEffect.h)"
[ "$got" = "built 2 2" ] && [ -f "$OUT/relative/FaustEffect.h" ] && ok "a directory given as a relative path: $got" || bad "a directory given as a relative path: \"$got\""
write_case "$OUT/src-plain" "$stereo"
expect "no effect at all" "$OUT/src-plain/dsp.dsp" "none"
# A last line that is a comment with no line end must not swallow what follows the source.
write_case "$OUT/src-comment" "$stereo" 'par(i, 2, *(lvl))'
printf '// the end' >> "$OUT/src-comment/dsp.dsp"
expect "a comment on the last line" "$OUT/src-comment/dsp.dsp" "built 2 2"

echo "== 2. What a module carries"
ABI="$(sed -nE 's/^#define PANGEA_MODULE_ABI ([0-9]+).*/\1/p' "$SHELL_DIR/platform/PangeaModuleApi.h")"
plugin_json() { # <dir> <kind>
    printf '{ "name": "Case", "slug": "case", "kind": "%s", "bundleId": "com.pangea.case", "manufacturerCode": "Pnga", "pluginCode": "Case", "version": "0.1.0", "description": "A test case." }\n' "$2" > "$1/plugin.json"
}
# module <name> <project dir> <effectSection the JSON line must give, or "absent"> <1 when the module must make an effect>
module() {
    local line
    line="$(bash "$SHELL_DIR/build-module.sh" "$2" "$OUT/modules/$1/v1" --project-id "case" --version-number 1 2>/dev/null | tail -1)"
    if python3 -I - "$line" "$OUT/modules/$1/v1/module.dylib" "$3" "$4" "$ABI" > "$OUT/module.log" 2>&1 <<'EOF'
import ctypes, json, sys
line, dylib, want_section, want_effect, abi = sys.argv[1:6]
summary = json.loads(line)
assert summary.get("ok"), "the module did not build: %s" % line
got = summary.get("effectSection", "absent")
assert got == want_section, "effectSection is %s, expected %s" % (got, want_section)
library = ctypes.CDLL(dylib)
assert library.pangea_module_abi() == int(abi), "the module's number is %d, the header's %s" % (library.pangea_module_abi(), abi)
library.pangea_create_effect.restype = ctypes.c_void_p
made = library.pangea_create_effect() is not None
assert made == (want_effect == "1"), "pangea_create_effect %s an effect" % ("made" if made else "made no")
print("effectSection %s, pangea_create_effect %s, module number %s" % (got, "makes the effect" if made else "gives nothing", abi))
EOF
    then ok "$1: $(cat "$OUT/module.log")"
    else bad "$1: $(tail -1 "$OUT/module.log")"; fi
}
module "Dub Keys" "$SHELL_DIR/examples/dub-keys" "built" 1
module "Poly Saw" "$SHELL_DIR/examples/poly-saw" "none" 0
# An effect plugin may call a helper `effect`: that is no effect section, and nothing is built for it.
mkdir -p "$OUT/helper"
printf 'import("stdfaust.lib");\neffect = par(i, 2, *(0.5));\nprocess = effect;\n' > "$OUT/helper/dsp.dsp"
plugin_json "$OUT/helper" effect
module "an effect plugin with a helper called effect" "$OUT/helper" "absent" 0
write_case "$OUT/broken" "$stereo" 'par(i, 2, *(lvl)) : nothingHere'
plugin_json "$OUT/broken" instrument
module "an instrument whose effect does not compile" "$OUT/broken" "left out" 0
write_case "$OUT/wide" "$stereo" 'par(i, 2, _ <: par(j, 8, _))'
plugin_json "$OUT/wide" instrument
module "an instrument whose effect gives sixteen channels" "$OUT/wide" "left out" 0

if [ "${1:-}" = "--plugin" ]; then
    BUILD="${PANGEA_BUILD_DIR:-$SHELL_DIR/build}"
    RENDER="$BUILD/pangea-render_artefacts/Release/pangea-render"
    field() { python3 -I -c 'import json, sys; print(json.loads(sys.argv[1]).get(sys.argv[2], ""))' "$1" "$2"; }
    # The ids of the processor's parameters for the project's own controls, in order, from
    # `pangea-render render`. Pangea's bypass, the one parameter after them all, is left out: its
    # id (pangea_bypass) has an underscore, which no control's id can have, and this pattern takes
    # none. tools/test-bypass.sh checks that it is there and that it is last.
    parameter_ids() { "$RENDER" render "$OUT/take.wav" 0.5 | sed -nE 's/^  ([a-z0-9]+) = .*/\1/p' | tr '\n' ' ' | sed 's/ $//'; }

    echo "== 3. Dub Keys as a real plugin"
    # --native-editor: the layout check below reads the shell's own editor, which describes what it
    # draws. A default build has the component library's panel (decision 77), which ignores ui.layout
    # and cannot be drawn off screen; nothing else in this part depends on the panel.
    line="$(bash "$SHELL_DIR/build.sh" "$SHELL_DIR/examples/dub-keys" --verify --native-editor | tee "$OUT/dub-keys.log" | tail -1)"
    if [ "$(field "$line" ok)" = "True" ] && [ "$(field "$line" effectSection)" = "built" ] && [ "$(field "$line" verify)" = "passed" ]; then
        ok "it builds with its effect section, and build.sh's own effect check passes"
    else
        bad "the build: $line"; sed 's/^/      /' "$OUT/dub-keys.log" | tail -20
    fi
    [ -x "$RENDER" ] || { bad "pangea-render was not built"; echo "$failures failures"; exit 1; }
    ids="$(parameter_ids)"
    [ "$ids" = "attack release tone sync time feedback mix level" ] && ok "its parameters are the voice's and then the effect's, Tone once: $ids" \
        || bad "its parameters are \"$ids\""
    # The keys of those parameters are the keys its ui.json places, which are the keys the site's
    # compile gives the same code (src/lib/faust/instrument-effect.test.ts holds the site to them).
    keys="$("$RENDER" keys | tr '\n' ',' | sed 's/,$//')"
    placed="$(python3 -I -c 'import json, sys; print(",".join(i["key"] for i in json.load(open(sys.argv[1]))["layout"]["items"]))' "$SHELL_DIR/examples/dub-keys/ui.json")"
    [ "$keys" = "$placed" ] && ok "their keys are the site's, as its ui.json has them: $keys" \
        || bad "their keys are \"$keys\", and ui.json places \"$placed\""
    if "$RENDER" effect-check --vst3 "$BUILD/PangeaInstrument_artefacts/Release/VST3/Dub Keys.vst3" --expect level --expect mix --shared tone > "$OUT/effect-check.log"; then
        sed -n 's/^  \(.*\)$/ok    \1/p' "$OUT/effect-check.log"
    else
        bad "pangea-render effect-check:"; sed 's/^/      /' "$OUT/effect-check.log"
    fi
    if "$RENDER" tempo 96 150 > "$OUT/tempo.log"; then sed 's/^/ok    /' "$OUT/tempo.log"; else bad "the echo's tempo does not follow the host:"; sed 's/^/      /' "$OUT/tempo.log"; fi
    # A layout finds the effect's controls by the keys the site gives them: two of them hidden by
    # key leave six cells. A key that missed would drop the item and show the control below.
    python3 -I - "$SHELL_DIR/examples/dub-keys/ui.json" "$OUT/hidden.json" <<'EOF'
import json, sys
ui = json.load(open(sys.argv[1]))
for item in ui["layout"]["items"]:
    if item["key"] in ("Echo/Mix", "Level"):
        item["hidden"] = True
json.dump(ui, open(sys.argv[2], "w"))
EOF
    shown="$("$RENDER" editor "$OUT/editor.png" | sed -nE 's/.* ([0-9]+) controls shown.*/\1/p')"
    hidden="$("$RENDER" editor "$OUT/hidden.png" --ui "$OUT/hidden.json" | sed -nE 's/.* ([0-9]+) controls shown.*/\1/p')"
    [ "$shown" = "8" ] && [ "$hidden" = "6" ] && ok "its layout places all 8 controls, and hiding Echo/Mix and Level by their keys leaves $hidden" \
        || bad "the layout shows $shown controls, and $hidden with Echo/Mix and Level hidden by key (expected 8 and 6)"

    echo "== 4. Effect Cases, the awkward ones (tools/fixtures/effect-cases)"
    line="$(bash "$SHELL_DIR/build.sh" "$SHELL_DIR/tools/fixtures/effect-cases" --verify | tee "$OUT/effect-cases.log" | tail -1)"
    if [ "$(field "$line" ok)" = "True" ] && [ "$(field "$line" effectSection)" = "built" ] && [ "$(field "$line" verify)" = "passed" ]; then
        ok "it builds with its effect section"
    else
        bad "the build: $line"; sed 's/^/      /' "$OUT/effect-cases.log" | tail -20
    fi
    # Bright is one definition read by the voice and by the effect: one parameter, though the code
    # declares no name and the two programs are two files. Trim is two definitions under one key,
    # the voice's from 0 to 1 and the effect's in decibels: two parameters, each where its own
    # code starts it. Tone is a group both have: Damp joins Bright. Level is a label two controls
    # of the voice have, so the second's id is its path; the effect has a Level at that path with
    # another range, and its id is neither of theirs.
    listing="$("$RENDER" render "$OUT/take.wav" 0.5 | sed -n 's/^  \([a-z0-9]* = .*\)$/\1/p' | tr '\n' ';')"
    [ "$listing" = "level = 0.70 (Level);dspsublevel = 0.30 (Level);bright = 0.50 (Bright);trim = 0.80 (Trim);dspsubleveleffect = -3.0 (Level);damp = 4000 (Damp);dsptrim = -12.0 (Trim);" ] \
        && ok "Bright is one parameter, the Trims and the Levels are each their own, at their own starts: $listing" \
        || bad "its parameters are \"$listing\""
    ids="$(printf '%s' "$listing" | tr ';' '\n' | sed 's/ = .*//' | sort | uniq -d | tr '\n' ' ')"
    [ -z "$ids" ] && ok "no two parameters share an id" || bad "these ids are used twice: $ids"
    keys="$("$RENDER" keys | tr '\n' ',' | sed 's/,$//')"
    [ "$keys" = "Osc/Level,Sub/Level,Tone/Bright,Trim,Sub/Level,Tone/Damp,Trim" ] && ok "their keys: $keys" || bad "their keys are \"$keys\""
    if "$RENDER" effect-check --shared bright --expect damp --expect dsptrim --expect dspsubleveleffect > "$OUT/cases-check.log"; then
        sed -n 's/^  \(.*\)$/ok    \1/p' "$OUT/cases-check.log"
    else
        bad "pangea-render effect-check:"; sed 's/^/      /' "$OUT/cases-check.log"
    fi
    # The voice's Trim and Levels are not the effect's: none may be among the section's controls.
    if grep -q -E "\((trim|level|dspsublevel)\)" "$OUT/cases-check.log"; then bad "a control of the voice alone is listed among the effect's"; else ok "the voice's own Trim and Levels are no controls of the effect"; fi

    echo "== 5. Effect Root, an effect that is one group (tools/fixtures/effect-root)"
    line="$(bash "$SHELL_DIR/build.sh" "$SHELL_DIR/tools/fixtures/effect-root" --verify | tee "$OUT/effect-root.log" | tail -1)"
    if [ "$(field "$line" ok)" = "True" ] && [ "$(field "$line" effectSection)" = "built" ] && [ "$(field "$line" verify)" = "passed" ]; then
        ok "it builds with its effect section"
    else
        bad "the build: $line"; sed 's/^/      /' "$OUT/effect-root.log" | tail -20
    fi
    # Faust makes the effect's only group, FX, its root, and a key leaves the root out: the effect's
    # Level has the voice's key, range and start, but another address. Two parameters, as the
    # builder has two controls.
    listing="$("$RENDER" render "$OUT/take.wav" 0.5 | sed -n 's/^  \([a-z0-9]* = .*\)$/\1/p' | tr '\n' ';')"
    [ "$listing" = "level = 0.50 (Level);drive = 2.00 (Drive);fxlevel = 0.50 (Level);" ] \
        && ok "the voice's Level and the effect's are two parameters: $listing" || bad "its parameters are \"$listing\""
    keys="$("$RENDER" keys | tr '\n' ',' | sed 's/,$//')"
    [ "$keys" = "Level,Drive,Level" ] && ok "their keys: $keys" || bad "their keys are \"$keys\""
    if "$RENDER" effect-check --expect drive --expect fxlevel > "$OUT/root-check.log" && ! grep -q "voice" "$OUT/root-check.log"; then
        sed -n 's/^  \(.*\)$/ok    \1/p' "$OUT/root-check.log"
    else
        bad "pangea-render effect-check (no control of this effect may be in the voices):"; sed 's/^/      /' "$OUT/root-check.log"
    fi

    echo "== 6. An instrument whose effect does not compile"
    # The builder plays such an instrument with its voices alone, so the plugin is built without
    # the effect, and the build says so.
    line="$(bash "$SHELL_DIR/build.sh" "$OUT/broken" --verify | tee "$OUT/broken.log" | tail -1)"
    if [ "$(field "$line" ok)" = "True" ] && [ "$(field "$line" effectSection)" = "left out" ] && [ "$(field "$line" verify)" = "passed" ] \
        && [ "$(field "$line" effectSectionError)" = "line 6: ERROR : undefined symbol : nothingHere" ] \
        && grep -q "warning: the effect section is left out, so the plugin plays the voices alone" "$OUT/broken.log"; then
        ok "it builds and plays its voices alone, and the build says why: $(field "$line" effectSectionError)"
    else
        bad "the build: $line"; sed 's/^/      /' "$OUT/broken.log" | tail -20
    fi
    "$RENDER" effect-check > "$OUT/broken-check.log" && grep -q "has no effect section" "$OUT/broken-check.log" && [ ! -e "$BUILD/generated/FaustEffect.h" ] \
        && ok "the plugin has no effect section, and no effect class is left from the build before it" \
        || bad "the plugin built from it: $(cat "$OUT/broken-check.log")"

    echo "== 7. An instrument whose effect is not heard"
    # Level is read by the voices and by the effect, and changes the sound through the voices.
    # Idle is the effect's own, and nothing it does can be heard (a billionth of full scale). A
    # control the voices read proves nothing of the effect, so the build's check must fail.
    write_case "$OUT/unheard" 'os.sawtooth(freq) : fi.lowpass(1, 500 + lvl * 6000) : *(gate * gain * 0.4) <: _, _' \
        'par(i, 2, *(1 + lvl * 1.0e-9) : +(hslider("Idle", 0.5, 0, 1, 0.01) * 1.0e-9))'
    plugin_json "$OUT/unheard" instrument
    line="$(bash "$SHELL_DIR/build.sh" "$OUT/unheard" --verify 2> "$OUT/unheard.err" | tee "$OUT/unheard.log" | tail -1)" || true
    if [ "$(field "$line" ok)" = "False" ] && [ "$(field "$line" stage)" = "verify" ] \
        && grep -q "effect: .*Level (level): in the effect and in 8 voices" "$OUT/unheard.log" \
        && ! grep -q "effect: .*Level (level).*no change" "$OUT/unheard.log" \
        && grep -q "effect: FAIL  none of the effect section's own 1 parameters changes the render, alone or together: the effect is not heard" "$OUT/unheard.log"; then
        ok "its Level changes the sound through the voices, its own Idle changes nothing, and the build's check fails: the effect is not heard"
    else
        bad "the build: $line"; grep "^effect: " "$OUT/unheard.log" | sed 's/^/      /'
    fi

    echo "== 8. Poly Saw, which has no effect section"
    line="$(bash "$SHELL_DIR/build.sh" "$SHELL_DIR/examples/poly-saw" --verify | tee "$OUT/poly-saw.log" | tail -1)"
    if [ "$(field "$line" ok)" = "True" ] && [ "$(field "$line" effectSection)" = "none" ] && [ "$(field "$line" verify)" = "passed" ]; then
        ok "it builds as before: effectSection none"
    else
        bad "the build: $line"; sed 's/^/      /' "$OUT/poly-saw.log" | tail -20
    fi
    [ ! -e "$BUILD/generated/FaustEffect.h" ] && ok "its build directory holds no effect class" \
        || bad "FaustEffect.h is in $BUILD/generated"
    ids="$(parameter_ids)"
    [ "$ids" = "cutoff resonance envamount attack decay sustain release detune volume" ] && ok "its parameters are the nine it had: $ids" \
        || bad "its parameters are \"$ids\""
    "$RENDER" effect-check > "$OUT/none.log" && grep -q "has no effect section" "$OUT/none.log" && ok "$(cat "$OUT/none.log")" \
        || bad "pangea-render effect-check on Poly Saw: $(cat "$OUT/none.log")"
fi

if [ "$failures" -gt 0 ]; then echo "$failures effect section checks failed"; exit 1; fi
echo "all effect section checks passed"
