#!/bin/bash
# Pangea native build. Turns a project directory (plugin.json, dsp.dsp, optionally logic.cpp and
# ui.json) into an AU and a VST3 on this Mac, and optionally installs them for Logic. A MIDI tool
# (kind "midi", docs/midi-tools.md) builds as two plugins: <name>, an instrument with MIDI out
# (AU aumu and VST3), and <name> FX, an AU MIDI effect (aumi) for Logic's MIDI FX slot.
#
#   shell/build.sh <project dir> [--install] [--verify] [--clean] [--no-auval] [--allow-logic] [--native-editor] [--standalone]
#
#   --install      copy into ~/Library/Audio/Plug-Ins and validate the AU (both, for a MIDI tool) with auval
#   --verify       also build pangea-render and prove the plugin makes sound offline (MIDI or a test
#                  signal); for a MIDI tool, prove notes come out and none are left on, then load the
#                  VST3 as a host would and check its MIDI out against the module's own notes; for
#                  every kind, prove that Pangea's bypass does what a bypassed plugin does; and for an
#                  effect, that its buses are as before or, when its code declares a sidechain, that
#                  the Sidechain input is there, off by default, and heard (docs/sidechain.md)
#   --clean        start from an empty build directory
#   --no-auval     install without running auval
#   --allow-logic  build a project that carries logic.cpp. A logic module is native code, and the
#                  source check is defence in depth, not a sandbox, so the caller must say so
#                  explicitly. The desktop app passes it only for the signed-in user's own projects.
#   --native-editor  draw the interface with the shell's own editor (platform/PangeaEditor.h), the
#                  older panel. Without it the interface is the component library's, drawn in a web
#                  view (platform/PangeaWebEditor.h): the panel the builder shows (decision 77)
#   --standalone   test: also build a standalone app, for measuring
#
# Progress goes to stdout as it happens. The last line is always one JSON object:
#   { "ok": true, "component": "...", "vst3": "...", "editor": "web", "faust": "2.88.0", "juce": "8.0.10", "seconds": 9, ... }
# A MIDI tool's line also has "fxPluginCode", "componentFx", "auvalFx" and "auvalFxLog".
# An instrument's line also has "effectSection": "built", "none" or "left out" (with "effectSectionError").
# Exit code 0 when the build (and, with --install, the installation and AU validation) succeeded.
set -euo pipefail

SHELL_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SHELL_DIR/.." && pwd)"
BUILD_DIR="${PANGEA_BUILD_DIR:-$SHELL_DIR/build}"   # test: PANGEA_BUILD_DIR keeps a second build apart
GEN_DIR="$BUILD_DIR/generated"
JUCE_VERSION="8.0.10"
STARTED=$(date +%s)
# run_faust and faust_failure: Faust with the shell's time limit, and what to say when it fails.
source "$SHELL_DIR/tools/run-faust.sh"

PROJECT_DIR=""
INSTALL=0
VERIFY=0
CLEAN=0
RUN_AUVAL=1
ALLOW_LOGIC=0
WEB_EDITOR=1   # the component library's panel in a web view; --native-editor gives the shell's own
STANDALONE=0
for arg in "$@"; do
    case "$arg" in
        --install) INSTALL=1 ;;
        --verify) VERIFY=1 ;;
        --clean) CLEAN=1 ;;
        --no-auval) RUN_AUVAL=0 ;;
        --allow-logic) ALLOW_LOGIC=1 ;;
        --native-editor) WEB_EDITOR=0 ;;
        --web-editor) WEB_EDITOR=1 ;;   # the default since decision 77; still taken, so an older call keeps working
        --standalone) STANDALONE=1 ;;
        --help|-h) sed -n '2,28p' "$0"; exit 0 ;;
        -*) echo "unknown option: $arg" >&2; exit 2 ;;
        *) PROJECT_DIR="$arg" ;;
    esac
done

# Everything the final JSON line reports, filled in as we go.
STAGE="start"
NAME=""; SLUG=""; KIND=""; BUNDLE_ID=""; MANUFACTURER_CODE=""; PLUGIN_CODE=""; VERSION=""; DESCRIPTION=""
FX_PLUGIN_CODE=""; FX_BUNDLE_ID=""
FAUST_VERSION=""; FAUST_EXPECTED=""
COMPONENT=""; VST3=""; COMPONENT_FX=""
INSTALLED=0; AUVAL="skipped"; AUVAL_LOG=""; AUVAL_FX="skipped"; AUVAL_FX_LOG=""
VERIFY_RESULT="skipped"
FIRST_BUILD=0
HAS_LOGIC=0
HAS_EFFECT=0; EFFECT_SECTION=""; EFFECT_SECTION_ERROR=""

finish() {
    local ok="$1" error="${2:-}"
    local seconds=$(( $(date +%s) - STARTED ))
    OK="$ok" ERROR="$error" STAGE="$STAGE" NAME="$NAME" SLUG="$SLUG" KIND="$KIND" VERSION="$VERSION" \
    PLUGIN_CODE="$PLUGIN_CODE" MANUFACTURER_CODE="$MANUFACTURER_CODE" BUNDLE_ID="$BUNDLE_ID" \
    COMPONENT="$COMPONENT" VST3="$VST3" INSTALLED="$INSTALLED" AUVAL="$AUVAL" AUVAL_LOG="$AUVAL_LOG" \
    VERIFY_RESULT="$VERIFY_RESULT" FAUST_VERSION="$FAUST_VERSION" FAUST_EXPECTED="$FAUST_EXPECTED" \
    JUCE_VERSION="$JUCE_VERSION" SECONDS_TAKEN="$seconds" FIRST_BUILD="$FIRST_BUILD" HAS_LOGIC="$HAS_LOGIC" \
    FX_PLUGIN_CODE="$FX_PLUGIN_CODE" COMPONENT_FX="$COMPONENT_FX" AUVAL_FX="$AUVAL_FX" AUVAL_FX_LOG="$AUVAL_FX_LOG" \
    EFFECT_SECTION="$EFFECT_SECTION" EFFECT_SECTION_ERROR="$EFFECT_SECTION_ERROR" WEB_EDITOR="$WEB_EDITOR" \
    python3 -c '
import json, os
e = os.environ
out = {
    "ok": e["OK"] == "1",
    "stage": e["STAGE"],
    "name": e["NAME"], "slug": e["SLUG"], "kind": e["KIND"], "version": e["VERSION"],
    "pluginCode": e["PLUGIN_CODE"], "manufacturerCode": e["MANUFACTURER_CODE"], "bundleId": e["BUNDLE_ID"],
    "logic": e["HAS_LOGIC"] == "1",
    "component": e["COMPONENT"], "vst3": e["VST3"],
    "editor": "web" if e["WEB_EDITOR"] == "1" else "native",
    "installed": e["INSTALLED"] == "1", "auval": e["AUVAL"], "auvalLog": e["AUVAL_LOG"],
    "verify": e["VERIFY_RESULT"],
    "faust": e["FAUST_VERSION"], "faustExpected": e["FAUST_EXPECTED"], "juce": e["JUCE_VERSION"],
    "firstBuild": e["FIRST_BUILD"] == "1",
    "seconds": int(e["SECONDS_TAKEN"]),
}
# A MIDI tool is two plugins: the instrument with MIDI out above, and the AU MIDI effect for Logic.
if e["KIND"] == "midi":
    out["fxPluginCode"] = e["FX_PLUGIN_CODE"]
    out["componentFx"] = e["COMPONENT_FX"]
    out["auvalFx"] = e["AUVAL_FX"]
    out["auvalFxLog"] = e["AUVAL_FX_LOG"]
# An instrument says what became of its effect section: "built", "none" or "left out".
if e["EFFECT_SECTION"]:
    out["effectSection"] = e["EFFECT_SECTION"]
    if e["EFFECT_SECTION_ERROR"]:
        out["effectSectionError"] = e["EFFECT_SECTION_ERROR"]
if e["ERROR"]:
    out["error"] = e["ERROR"]
print(json.dumps(out))
'
    if [ "$ok" = "1" ]; then exit 0; else exit 1; fi
}

fail() { echo "error: $2" >&2; STAGE="$1"; finish 0 "$2"; }
say() { echo "[$(( $(date +%s) - STARTED ))s] $*"; }

[ -n "$PROJECT_DIR" ] || fail "arguments" "usage: build.sh <project dir> [--install] [--verify] [--clean]"
[ -d "$PROJECT_DIR" ] || fail "arguments" "no such directory: $PROJECT_DIR"
PROJECT_DIR="$(cd "$PROJECT_DIR" && pwd)"
[ -f "$PROJECT_DIR/plugin.json" ] || fail "project" "missing $PROJECT_DIR/plugin.json"
[ -f "$PROJECT_DIR/dsp.dsp" ] || fail "project" "missing $PROJECT_DIR/dsp.dsp"

# 1. Tools.
STAGE="tools"
command -v xcode-select >/dev/null && xcode-select -p >/dev/null 2>&1 || fail tools "Xcode or the Command Line Tools are not installed (xcode-select -p failed). Run: xcode-select --install"
command -v cmake >/dev/null || fail tools "cmake not found. Install it with: brew install cmake"
command -v ninja >/dev/null || fail tools "ninja not found. Install it with: brew install ninja"
command -v faust >/dev/null || fail tools "faust not found. Install it with: brew install faust"
command -v python3 >/dev/null || fail tools "python3 not found (it comes with the Xcode Command Line Tools)"
[ -f "$SHELL_DIR/JUCE/CMakeLists.txt" ] || fail tools "JUCE is missing. Run: git clone --depth 1 --branch $JUCE_VERSION https://github.com/juce-framework/JUCE $SHELL_DIR/JUCE"

FAUST_VERSION="$(faust --version 2>/dev/null | head -1 | sed -E 's/.*Version ([0-9.]+).*/\1/')"
FAUST_EXPECTED="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["native"]["faust"])' "$REPO_DIR/library/faust-versions.json" 2>/dev/null || true)"
if [ -n "$FAUST_EXPECTED" ] && [ "$FAUST_VERSION" != "$FAUST_EXPECTED" ]; then
    say "warning: faust $FAUST_VERSION found, library/faust-versions.json pins $FAUST_EXPECTED. The sound may drift from the browser."
fi

FAUST_BIN="$(command -v faust)"
FAUST_INCLUDE="$(cd "$(dirname "$FAUST_BIN")/.." && pwd)/include"
if [ ! -f "$FAUST_INCLUDE/faust/dsp/poly-dsp.h" ] && command -v brew >/dev/null; then
    FAUST_INCLUDE="$(brew --prefix faust 2>/dev/null)/include"
fi
[ -f "$FAUST_INCLUDE/faust/dsp/poly-dsp.h" ] || fail tools "Faust architecture headers not found next to $FAUST_BIN (looked for include/faust/dsp/poly-dsp.h)"
say "tools: faust $FAUST_VERSION, JUCE $JUCE_VERSION, $(cmake --version | head -1), ninja $(ninja --version), $(xcode-select -p)"

# 2. The project.
STAGE="project"
eval "$(python3 -I "$SHELL_DIR/tools/read-plugin-json.py" "$PROJECT_DIR/plugin.json")"
[ -z "${PLUGIN_ERROR:-}" ] || fail project "$PLUGIN_ERROR"

IS_MIDI_TOOL=OFF; FX_TARGET=""; FX_AU_TYPE=""
case "$KIND" in
    instrument) IS_SYNTH=TRUE; TARGET=PangeaInstrument; AU_TYPE=aumu ;;
    midi) IS_SYNTH=FALSE; IS_MIDI_TOOL=ON; TARGET=PangeaMidi; AU_TYPE=aumu; FX_TARGET=PangeaMidiFx; FX_AU_TYPE=aumi ;;
    *) IS_SYNTH=FALSE; TARGET=PangeaEffect; AU_TYPE=aufx ;;
esac
if [ "$KIND" = "midi" ]; then
    say "project: $NAME (MIDI tool, $PLUGIN_CODE and $NAME FX $FX_PLUGIN_CODE, $BUNDLE_ID, version $VERSION)"
    [ -f "$PROJECT_DIR/logic.cpp" ] || fail project "a MIDI tool is its logic module, and $PROJECT_DIR/logic.cpp is missing"
else
    say "project: $NAME ($KIND, $PLUGIN_CODE, $BUNDLE_ID, version $VERSION)"
fi

# A shared project is untrusted code: the source rules are checked here, before anything compiles.
STAGE="check"
CHECK_OUTPUT="$(bash "$SHELL_DIR/check-project.sh" "$PROJECT_DIR" 2>&1)" || fail check "$CHECK_OUTPUT"
say "$CHECK_OUTPUT"

# 3. Faust to C++.
STAGE="faust"
if [ "$CLEAN" = "1" ]; then rm -rf "$BUILD_DIR"; fi
[ -f "$BUILD_DIR/build.ninja" ] || FIRST_BUILD=1
mkdir -p "$GEN_DIR"
# The compiler runs on a copy in the build directory so that it can only see the standard libraries,
# not a .lib file someone put next to the project.
cp "$PROJECT_DIR/dsp.dsp" "$GEN_DIR/dsp.dsp"
FAUST_STATUS=0
run_faust "$GEN_DIR" "$GEN_DIR/faust.log" -lang cpp -cn mydsp -ftz 1 -o "$GEN_DIR/FaustDsp.h.new" dsp.dsp || FAUST_STATUS=$?
if [ "$FAUST_STATUS" != 0 ]; then
    cat "$GEN_DIR/faust.log" >&2
    fail faust "$(faust_failure "$FAUST_STATUS" "$GEN_DIR/faust.log" "$PROJECT_DIR/dsp.dsp")"
fi
mv "$GEN_DIR/FaustDsp.h.new" "$GEN_DIR/FaustDsp.h"
say "faust: dsp.dsp -> FaustDsp.h ($(wc -l < "$GEN_DIR/FaustDsp.h" | tr -d ' ') lines)"

# An instrument's effect section, the stage all its voices share (`effect = ...;` beside `process`):
# its own class, which the shell runs on the mixed voices as the builder does (decision 73). An
# effect that does not compile is left out, as the builder leaves it out, and so is one that gives
# more channels than a plugin carries. The build says so. One that Faust did not finish stops the
# build, with build-effect.sh's reason.
rm -rf "$GEN_DIR/effect"
if [ "$KIND" = "instrument" ]; then
    EFFECT_LINE="$(bash "$SHELL_DIR/tools/build-effect.sh" "$GEN_DIR" "$GEN_DIR/FaustEffect.h.new" "$PROJECT_DIR/dsp.dsp" 2> "$GEN_DIR/effect.err")" || {
        cat "$GEN_DIR/effect.err" >&2
        EFFECT_REASON="$(grep -v '^[[:space:]]*$' "$GEN_DIR/effect.err" | tail -1 || true)"
        [ -n "$EFFECT_REASON" ] || EFFECT_REASON="the instrument's effect section could not be built"
        fail faust "$EFFECT_REASON"
    }
    case "$EFFECT_LINE" in
        built\ *)
            HAS_EFFECT=1; EFFECT_SECTION="built"
            mv "$GEN_DIR/FaustEffect.h.new" "$GEN_DIR/FaustEffect.h"
            read -r _ EFFECT_INS EFFECT_OUTS <<< "$EFFECT_LINE"
            say "faust: the effect section -> FaustEffect.h ($EFFECT_INS in, $EFFECT_OUTS out, $(wc -l < "$GEN_DIR/FaustEffect.h" | tr -d ' ') lines)"
            ;;
        left-out\ *)
            EFFECT_SECTION="left out"; EFFECT_SECTION_ERROR="${EFFECT_LINE#left-out }"
            say "warning: the effect section is left out, so the plugin plays the voices alone: $EFFECT_SECTION_ERROR"
            ;;
        *) EFFECT_SECTION="none" ;;
    esac
fi
# The build directory is shared: a class left by another project must never be compiled in.
[ "$HAS_EFFECT" = "1" ] || rm -f "$GEN_DIR/FaustEffect.h" "$GEN_DIR/FaustEffect.h.new"

# The interface is data, never source: ui.json (already checked by check-project.sh) is copied
# next to the generated code and CMake compiles its bytes into the plugin as a byte array. A
# project without one embeds an empty object, which the editor draws with the default look.
# The copy is replaced only when it changed, so an unchanged interface relinks nothing.
if [ -f "$PROJECT_DIR/ui.json" ]; then
    cp "$PROJECT_DIR/ui.json" "$GEN_DIR/ui.json.new"
    UI_NOTE="ui.json -> embedded ($(wc -c < "$GEN_DIR/ui.json.new" | tr -d ' ') bytes)"
else
    printf '{}\n' > "$GEN_DIR/ui.json.new"
    UI_NOTE="no ui.json, the default look"
fi
if cmp -s "$GEN_DIR/ui.json.new" "$GEN_DIR/ui.json"; then rm -f "$GEN_DIR/ui.json.new"; else mv "$GEN_DIR/ui.json.new" "$GEN_DIR/ui.json"; fi
say "ui: $UI_NOTE"

# The web panel's footer says who built the plugin and links to Pangea ("Built by <name> with
# Pangea"). Both come from plugin.json: the owner's name and the site's address. Only those two go
# into the plugin, as data like ui.json, so the owner's email address is never compiled in.
python3 -I "$SHELL_DIR/tools/write-credit.py" "$PROJECT_DIR/plugin.json" "$GEN_DIR/credit.json.new" \
    || fail project "could not read who built the plugin from plugin.json"
if cmp -s "$GEN_DIR/credit.json.new" "$GEN_DIR/credit.json"; then rm -f "$GEN_DIR/credit.json.new"; else mv "$GEN_DIR/credit.json.new" "$GEN_DIR/credit.json"; fi

if [ -f "$PROJECT_DIR/logic.cpp" ]; then
    # Native code from a project: built only when the caller takes responsibility for it
    # (docs/decisions.md 17). Without the flag the sound part could still be built, but a
    # project with a logic module is one thing, so the whole build is refused.
    [ "$ALLOW_LOGIC" = "1" ] || fail logic "this project carries a logic module (logic.cpp), which is native code. Pass --allow-logic to build it; the desktop app does so only for the signed-in user's own projects until logic modules are sandboxed."
    # The web app checks the full rules before a logic module is saved; this repeats the one that matters most.
    if grep -E '^\s*#\s*include' "$PROJECT_DIR/logic.cpp" | grep -vE '#\s*include\s*"logic_api.h"' >/dev/null; then
        fail logic "logic.cpp may include nothing but logic_api.h"
    fi
    cp "$PROJECT_DIR/logic.cpp" "$GEN_DIR/LogicModule.cpp"
    HAS_LOGIC=1
    say "logic: logic.cpp -> LogicModule.cpp"
else
    rm -f "$GEN_DIR/LogicModule.cpp"
fi

# 4. Configure and build (Release, no link-time optimisation).
STAGE="configure"
CONFIGURE_LOG="$BUILD_DIR/configure.log"
if ! cmake -S "$SHELL_DIR" -B "$BUILD_DIR" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DPANGEA_NAME="$NAME" \
        -DPANGEA_SLUG="$SLUG" \
        -DPANGEA_BUNDLE_ID="$BUNDLE_ID" \
        -DPANGEA_MANUFACTURER_CODE="$MANUFACTURER_CODE" \
        -DPANGEA_PLUGIN_CODE="$PLUGIN_CODE" \
        -DPANGEA_VERSION="$VERSION" \
        -DPANGEA_DESCRIPTION="$DESCRIPTION" \
        -DPANGEA_IS_SYNTH="$IS_SYNTH" \
        -DPANGEA_IS_MIDI_TOOL="$IS_MIDI_TOOL" \
        -DPANGEA_FX_PLUGIN_CODE="${FX_PLUGIN_CODE:-Pgpx}" \
        -DPANGEA_FX_BUNDLE_ID="${FX_BUNDLE_ID:-$BUNDLE_ID.fx}" \
        -DPANGEA_HAS_LOGIC="$([ "$HAS_LOGIC" = "1" ] && echo ON || echo OFF)" \
        -DPANGEA_HAS_EFFECT="$([ "$HAS_EFFECT" = "1" ] && echo ON || echo OFF)" \
        -DPANGEA_GENERATED_DIR="$GEN_DIR" \
        -DPANGEA_FAUST_INCLUDE="$FAUST_INCLUDE" \
        -DPANGEA_BUILD_RENDER="$([ "$VERIFY" = "1" ] && echo ON || echo OFF)" \
        -DPANGEA_WEB_EDITOR="$([ "$WEB_EDITOR" = "1" ] && echo ON || echo OFF)" \
        -DPANGEA_STANDALONE="$([ "$STANDALONE" = "1" ] && echo ON || echo OFF)" \
        > "$CONFIGURE_LOG" 2>&1; then
    tail -40 "$CONFIGURE_LOG" >&2
    fail configure "cmake configure failed, see $CONFIGURE_LOG"
fi
say "configure: done ($( [ "$FIRST_BUILD" = "1" ] && echo "first build, JUCE compiles once" || echo "incremental"))"

STAGE="build"
BUILD_LOG="$BUILD_DIR/build.log"
TARGETS=("${TARGET}_AU" "${TARGET}_VST3")
[ -n "$FX_TARGET" ] && TARGETS+=("${FX_TARGET}_AU")
[ "$VERIFY" = "1" ] && TARGETS+=("pangea-render")
[ "$STANDALONE" = "1" ] && TARGETS+=("${TARGET}_Standalone")
BUILD_START=$(date +%s)
if ! cmake --build "$BUILD_DIR" --target "${TARGETS[@]}" > "$BUILD_LOG" 2>&1; then
    grep -E "error|Error|FAILED" "$BUILD_LOG" | head -30 >&2
    fail build "compile failed, see $BUILD_LOG: $(grep -E ' error:' "$BUILD_LOG" | head -3 | tr '\n' ' ' | cut -c1-600)"
fi
ARTEFACTS="$BUILD_DIR/${TARGET}_artefacts/Release"
COMPONENT="$ARTEFACTS/AU/$NAME.component"
VST3="$ARTEFACTS/VST3/$NAME.vst3"
[ -d "$COMPONENT" ] || fail build "built, but $COMPONENT is missing"
[ -d "$VST3" ] || fail build "built, but $VST3 is missing"
if [ -n "$FX_TARGET" ]; then
    COMPONENT_FX="$BUILD_DIR/${FX_TARGET}_artefacts/Release/AU/$NAME FX.component"
    [ -d "$COMPONENT_FX" ] || fail build "built, but $COMPONENT_FX is missing"
    say "build: $(( $(date +%s) - BUILD_START ))s for ${TARGET}_AU, ${TARGET}_VST3 and ${FX_TARGET}_AU"
else
    say "build: $(( $(date +%s) - BUILD_START ))s for ${TARGET}_AU and ${TARGET}_VST3"
fi
say "build: $COMPONENT"
if [ -n "$COMPONENT_FX" ]; then say "build: $COMPONENT_FX"; fi
say "build: $VST3"

# The step budget must cover all of the module's code (docs/midi-tools.md, "The step budget in the
# plugin"; decision 46): every function of each compiled copy calls it, or the build is refused
# before anything is installed. This does not depend on the source checks spotting what turned it off.
if [ "$HAS_LOGIC" = "1" ]; then
    LOGIC_TARGETS=("$TARGET")
    [ -n "$FX_TARGET" ] && LOGIC_TARGETS+=("$FX_TARGET")
    [ "$VERIFY" = "1" ] && LOGIC_TARGETS+=("pangea-render")
    for t in "${LOGIC_TARGETS[@]}"; do
        obj="$BUILD_DIR/CMakeFiles/$t.dir/platform/logic_native.cpp.o"
        [ -f "$obj" ] || fail logic "built, but $obj is missing, so the step budget cannot be checked"
        if ! BUDGET_NOTE="$(python3 "$SHELL_DIR/tools/check-instrumented.py" "$obj")"; then
            fail logic "${BUDGET_NOTE#check-instrumented: }"
        fi
    done
    [ "${#LOGIC_TARGETS[@]}" = "1" ] && BUILDS="1 build" || BUILDS="${#LOGIC_TARGETS[@]} builds"
    say "logic: ${BUDGET_NOTE#check-instrumented: } (checked in $BUILDS)"
fi

# 5. Offline proof that it makes sound.
if [ "$VERIFY" = "1" ]; then
    STAGE="verify"
    RENDER="$BUILD_DIR/pangea-render_artefacts/Release/pangea-render"
    [ -x "$RENDER" ] || RENDER="$BUILD_DIR/pangea-render_artefacts/pangea-render"
    [ -x "$RENDER" ] || fail verify "pangea-render was not built"
    # Every plugin has Pangea's bypass (decision 75): the processor and the VST3 are played with it
    # on from the start and switched in the middle, and held to what a bypassed plugin of this
    # kind gives, with every switch a fade.
    bypass_check() {
        if "$RENDER" bypass-check --vst3 "$VST3" | tee "$BUILD_DIR/bypass-check.log" | sed 's/^/bypass: /'; then
            VERIFY_RESULT="passed"
        else
            VERIFY_RESULT="failed"
            fail verify "the bypass does not do what a bypassed plugin does, see $BUILD_DIR/bypass-check.log"
        fi
    }
    if [ "$KIND" = "midi" ]; then
        # A MIDI tool makes no sound: the proof is its notes. First the processor offline (notes come
        # out, every one ends, none is left on after stop), then the VST3 loaded as a host loads it,
        # its MIDI out compared with the module's own notes.
        if "$RENDER" notes | tee "$BUILD_DIR/notes.log" | sed 's/^/notes: /'; then
            VERIFY_RESULT="passed"
        else
            VERIFY_RESULT="failed"
            fail verify "the offline run made no notes, or left one on, see $BUILD_DIR/notes.log"
        fi
        if "$RENDER" host-check "$VST3" | tee "$BUILD_DIR/host-check.log" | sed 's/^/host: /'; then
            VERIFY_RESULT="passed"
        else
            VERIFY_RESULT="failed"
            fail verify "the VST3's MIDI out did not match the module, see $BUILD_DIR/host-check.log"
        fi
        bypass_check
        "$RENDER" editor "$BUILD_DIR/$SLUG-editor.png" --play 2 | sed 's/^/editor: /' || say "editor: snapshot failed (not fatal)"
    else
        WAV="$BUILD_DIR/$SLUG.wav"
        if "$RENDER" render "$WAV" | tee "$BUILD_DIR/render.log" | sed 's/^/render: /'; then
            VERIFY_RESULT="passed"
        else
            VERIFY_RESULT="failed"
            fail verify "the offline render was silent or failed, see $BUILD_DIR/render.log"
        fi
        if "$RENDER" check-vst3 "$VST3" | tee "$BUILD_DIR/vst3.log" | sed 's/^/vst3: /'; then
            VERIFY_RESULT="passed"
        else
            VERIFY_RESULT="failed"
            fail verify "the VST3 did not load in a host, see $BUILD_DIR/vst3.log"
        fi
        # An instrument's effect section must be in the plugin and be heard: its controls are
        # parameters, in the processor and in the VST3, and moving them changes the render.
        if [ "$HAS_EFFECT" = "1" ]; then
            if "$RENDER" effect-check --vst3 "$VST3" | tee "$BUILD_DIR/effect-check.log" | sed 's/^/effect: /'; then
                VERIFY_RESULT="passed"
            else
                VERIFY_RESULT="failed"
                fail verify "the effect section is not all there, or is not heard, see $BUILD_DIR/effect-check.log"
            fi
        fi
        bypass_check
        # The buses: an effect with no sidechain has exactly the ones it always had; one whose code
        # declares a sidechain has a Sidechain input, off by default, that a kick is heard through,
        # in the processor and in the VST3 (docs/sidechain.md).
        if [ "$KIND" = "effect" ]; then
            if "$RENDER" sidechain-check --vst3 "$VST3" --wav "$BUILD_DIR/$SLUG-sidechain.wav" | tee "$BUILD_DIR/sidechain-check.log" | sed 's/^/sidechain: /'; then
                VERIFY_RESULT="passed"
            else
                VERIFY_RESULT="failed"
                fail verify "the plugin's buses or its sidechain are not as they should be, see $BUILD_DIR/sidechain-check.log"
            fi
        fi
        "$RENDER" editor "$BUILD_DIR/$SLUG-editor.png" | sed 's/^/editor: /' || say "editor: snapshot failed (not fatal)"
    fi
fi

# 6. Install for Logic and validate.
if [ "$INSTALL" = "1" ]; then
    STAGE="install"
    COMPONENTS_DIR="$HOME/Library/Audio/Plug-Ins/Components"
    VST3_DIR="$HOME/Library/Audio/Plug-Ins/VST3"
    mkdir -p "$COMPONENTS_DIR" "$VST3_DIR"
    rm -rf "$COMPONENTS_DIR/$NAME.component" "$VST3_DIR/$NAME.vst3"
    cp -R "$COMPONENT" "$COMPONENTS_DIR/"
    cp -R "$VST3" "$VST3_DIR/"
    COMPONENT="$COMPONENTS_DIR/$NAME.component"
    VST3="$VST3_DIR/$NAME.vst3"
    if [ -n "$COMPONENT_FX" ]; then
        rm -rf "$COMPONENTS_DIR/$NAME FX.component"
        cp -R "$COMPONENT_FX" "$COMPONENTS_DIR/"
        COMPONENT_FX="$COMPONENTS_DIR/$NAME FX.component"
    fi
    INSTALLED=1
    killall -9 AudioComponentRegistrar 2>/dev/null || true
    say "install: $COMPONENT"
    if [ -n "$COMPONENT_FX" ]; then say "install: $COMPONENT_FX"; fi
    say "install: $VST3"

    if [ "$RUN_AUVAL" = "1" ]; then
        STAGE="auval"
        sleep 1   # give the registrar a moment to come back with the new component
        # auval must run in the user's GUI session, where the per-user registrar lives (see README).
        AUVAL_RUNNER=()
        if [ "$(launchctl managername 2>/dev/null)" != "Aqua" ]; then
            AUVAL_RUNNER=("$SHELL_DIR/tools/run-in-gui-session.sh")
            say "auval: this shell is not in the GUI session, running auval through launchctl gui/$(id -u)"
        fi
        # auval_one <type> <code> <log> : 0 when the component validates
        auval_one() {
            set +e
            ${AUVAL_RUNNER[@]+"${AUVAL_RUNNER[@]}"} /usr/bin/auval -v "$1" "$2" "$MANUFACTURER_CODE" > "$3" 2>&1
            set -e
            grep -q "AU VALIDATION SUCCEEDED" "$3"
        }
        AUVAL_LOG="$BUILD_DIR/auval.log"
        if auval_one "$AU_TYPE" "$PLUGIN_CODE" "$AUVAL_LOG"; then
            AUVAL="passed"
            say "auval: AU VALIDATION SUCCEEDED (auval -v $AU_TYPE $PLUGIN_CODE $MANUFACTURER_CODE)"
        else
            AUVAL="failed"
            tail -30 "$AUVAL_LOG" >&2
            fail auval "auval -v $AU_TYPE $PLUGIN_CODE $MANUFACTURER_CODE did not succeed, see $AUVAL_LOG"
        fi
        if [ -n "$COMPONENT_FX" ]; then
            AUVAL_FX_LOG="$BUILD_DIR/auval-fx.log"
            if auval_one "$FX_AU_TYPE" "$FX_PLUGIN_CODE" "$AUVAL_FX_LOG"; then
                AUVAL_FX="passed"
                say "auval: AU VALIDATION SUCCEEDED (auval -v $FX_AU_TYPE $FX_PLUGIN_CODE $MANUFACTURER_CODE)"
            else
                AUVAL_FX="failed"
                tail -30 "$AUVAL_FX_LOG" >&2
                fail auval "auval -v $FX_AU_TYPE $FX_PLUGIN_CODE $MANUFACTURER_CODE did not succeed, see $AUVAL_FX_LOG"
            fi
        fi
    fi
fi

STAGE="done"
finish 1
