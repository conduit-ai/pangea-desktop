#!/bin/bash
# build-module.sh: one version of a Pangea project as a module for the Pangea plugin
# (docs/pangea-plugin.md). The desktop app runs it; people never see it.
#
#   build-module.sh <project dir> <module dir> [--project-id <id>] [--version-number <n>] [--allow-logic]
#
# The same project directory build.sh takes (plugin.json, dsp.dsp, optional logic.cpp and ui.json),
# checked by the same check-project.sh. Faust writes the class (and a second one for an
# instrument's effect section, tools/build-effect.sh), clang compiles it with
# platform/PangeaModule.cpp (and the logic module with the step budget's flags) into one library,
# module.dylib, about a second. The module directory gets module.dylib, manifest.json, plugin.json
# and ui.json, all at once: it is built next to the target and renamed into place.
#
# Progress lines go to stdout; the last line is always one JSON object:
#   {"ok": true, "stage": "done", "moduleDir", "module", "name", "slug", "kind", "logic", "abi", "faust", "seconds"}
#   {"ok": false, "stage": "arguments|tools|project|check|faust|logic|compile|write", "error": "..."}  (exit 1)
# An instrument's line also has "effectSection": "built", "none" or "left out" (with "effectSectionError").
set -euo pipefail

SHELL_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SHELL_DIR/.." && pwd)"
STARTED=$(python3 -c 'import time; print(time.time())')
# run_faust and faust_failure: Faust with the shell's time limit, and what to say when it fails.
source "$SHELL_DIR/tools/run-faust.sh"

PROJECT_DIR=""; MODULE_DIR=""; PROJECT_ID=""; VERSION_NUMBER=""; ALLOW_LOGIC=0
while [ $# -gt 0 ]; do
    case "$1" in
        --project-id) PROJECT_ID="${2:-}"; shift 2 ;;
        --version-number) VERSION_NUMBER="${2:-}"; shift 2 ;;
        --allow-logic) ALLOW_LOGIC=1; shift ;;
        --help|-h) sed -n '2,18p' "$0"; exit 0 ;;
        -*) echo "unknown option: $1" >&2; exit 2 ;;
        *) if [ -z "$PROJECT_DIR" ]; then PROJECT_DIR="$1"; else MODULE_DIR="$1"; fi; shift ;;
    esac
done

STAGE="start"
NAME=""; SLUG=""; KIND=""; HAS_LOGIC=0; FAUST_VERSION=""; MODULE_PATH=""; ABI=""
HAS_EFFECT=0; EFFECT_SECTION=""; EFFECT_SECTION_ERROR=""
WORK=""

finish() {
    local ok="$1" error="${2:-}"
    [ -z "$WORK" ] || rm -rf "$WORK"
    OK="$ok" ERROR="$error" STAGE="$STAGE" NAME="$NAME" SLUG="$SLUG" KIND="$KIND" HAS_LOGIC="$HAS_LOGIC" \
    MODULE_DIR="$MODULE_DIR" MODULE_PATH="$MODULE_PATH" ABI="$ABI" FAUST_VERSION="$FAUST_VERSION" STARTED="$STARTED" \
    EFFECT_SECTION="$EFFECT_SECTION" EFFECT_SECTION_ERROR="$EFFECT_SECTION_ERROR" \
    python3 -I -c '
import json, os, time
e = os.environ
out = {"ok": e["OK"] == "1", "stage": e["STAGE"]}
if e["OK"] == "1":
    out.update({"moduleDir": e["MODULE_DIR"], "module": e["MODULE_PATH"], "name": e["NAME"], "slug": e["SLUG"],
                "kind": e["KIND"], "logic": e["HAS_LOGIC"] == "1", "abi": int(e["ABI"] or 0), "faust": e["FAUST_VERSION"]})
    # An instrument says what became of its effect section: "built", "none" or "left out".
    if e["EFFECT_SECTION"]:
        out["effectSection"] = e["EFFECT_SECTION"]
        if e["EFFECT_SECTION_ERROR"]:
            out["effectSectionError"] = e["EFFECT_SECTION_ERROR"]
out["seconds"] = round(time.time() - float(e["STARTED"]), 2)
if e["ERROR"]:
    out["error"] = e["ERROR"]
print(json.dumps(out))
'
    if [ "$ok" = "1" ]; then exit 0; else exit 1; fi
}
fail() { echo "error: $2" >&2; STAGE="$1"; finish 0 "$2"; }
say() { echo "$*"; }

[ -n "$PROJECT_DIR" ] && [ -n "$MODULE_DIR" ] || fail arguments "usage: build-module.sh <project dir> <module dir> [--project-id <id>] [--version-number <n>] [--allow-logic]"
[ -d "$PROJECT_DIR" ] || fail arguments "no such directory: $PROJECT_DIR"
PROJECT_DIR="$(cd "$PROJECT_DIR" && pwd)"
case "$MODULE_DIR" in /*) ;; *) MODULE_DIR="$(pwd)/$MODULE_DIR" ;; esac
[ -z "$VERSION_NUMBER" ] || [[ "$VERSION_NUMBER" =~ ^[0-9]{1,6}$ ]] || fail arguments "--version-number takes a whole number"
[ -f "$PROJECT_DIR/plugin.json" ] || fail project "missing $PROJECT_DIR/plugin.json"
[ -f "$PROJECT_DIR/dsp.dsp" ] || fail project "missing $PROJECT_DIR/dsp.dsp"

# 1. Tools: the same Faust as build.sh, and clang from Xcode or the Command Line Tools.
STAGE="tools"
command -v xcode-select >/dev/null && xcode-select -p >/dev/null 2>&1 || fail tools "Xcode or the Command Line Tools are not installed (xcode-select -p failed). Run: xcode-select --install"
command -v clang++ >/dev/null || fail tools "clang++ not found (it comes with the Xcode Command Line Tools)"
command -v faust >/dev/null || fail tools "faust not found. Install it with: brew install faust"
command -v python3 >/dev/null || fail tools "python3 not found (it comes with the Xcode Command Line Tools)"
FAUST_VERSION="$(faust --version 2>/dev/null | head -1 | sed -E 's/.*Version ([0-9.]+).*/\1/')"
FAUST_EXPECTED="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["native"]["faust"])' "$REPO_DIR/library/faust-versions.json" 2>/dev/null || true)"
if [ -n "$FAUST_EXPECTED" ] && [ "$FAUST_VERSION" != "$FAUST_EXPECTED" ]; then
    say "warning: faust $FAUST_VERSION found, library/faust-versions.json pins $FAUST_EXPECTED. The sound may drift from the browser."
fi
FAUST_BIN="$(command -v faust)"
FAUST_INCLUDE="$(cd "$(dirname "$FAUST_BIN")/.." && pwd)/include"
if [ ! -f "$FAUST_INCLUDE/faust/dsp/dsp.h" ] && command -v brew >/dev/null; then
    FAUST_INCLUDE="$(brew --prefix faust 2>/dev/null)/include"
fi
[ -f "$FAUST_INCLUDE/faust/dsp/dsp.h" ] || fail tools "Faust architecture headers not found next to $FAUST_BIN (looked for include/faust/dsp/dsp.h)"
ABI="$(sed -nE 's/^#define PANGEA_MODULE_ABI ([0-9]+).*/\1/p' "$SHELL_DIR/platform/PangeaModuleApi.h")"
[ -n "$ABI" ] || fail tools "platform/PangeaModuleApi.h names no PANGEA_MODULE_ABI"

# 2. The project, read and checked exactly as build.sh does.
STAGE="project"
eval "$(python3 -I "$SHELL_DIR/tools/read-plugin-json.py" "$PROJECT_DIR/plugin.json")"
[ -z "${PLUGIN_ERROR:-}" ] || fail project "$PLUGIN_ERROR"
case "$KIND" in effect|instrument|midi) ;; *) KIND=effect ;; esac
if [ "$KIND" = "midi" ] && [ ! -f "$PROJECT_DIR/logic.cpp" ]; then
    fail project "a MIDI tool is its logic module, and $PROJECT_DIR/logic.cpp is missing"
fi
say "project: $NAME ($KIND)"

# A shared project is untrusted code: the source rules are checked here, before anything compiles.
STAGE="check"
CHECK_OUTPUT="$(bash "$SHELL_DIR/check-project.sh" "$PROJECT_DIR" 2>&1)" || fail check "$CHECK_OUTPUT"
say "$CHECK_OUTPUT"

# 3. Faust to C++, in a work directory next to the module directory, on a copy of the source so
#    the compiler sees the standard libraries only.
STAGE="faust"
mkdir -p "$(dirname "$MODULE_DIR")"
WORK="$(mktemp -d "$(dirname "$MODULE_DIR")/.build.XXXXXX")"
mkdir -p "$WORK/gen" "$WORK/out"
cp "$PROJECT_DIR/dsp.dsp" "$WORK/gen/dsp.dsp"
FAUST_STATUS=0
run_faust "$WORK/gen" "$WORK/gen/faust.log" -lang cpp -cn mydsp -ftz 1 -o "$WORK/gen/FaustDsp.h" dsp.dsp || FAUST_STATUS=$?
if [ "$FAUST_STATUS" != 0 ]; then
    cat "$WORK/gen/faust.log" >&2
    fail faust "$(faust_failure "$FAUST_STATUS" "$WORK/gen/faust.log" "$PROJECT_DIR/dsp.dsp")"
fi
say "faust: dsp.dsp -> FaustDsp.h ($(wc -l < "$WORK/gen/FaustDsp.h" | tr -d ' ') lines)"

# An instrument's effect section, the stage all its voices share, exactly as build.sh builds it
# (tools/build-effect.sh, decision 73): the module carries it and the plugin runs it on the mixed
# voices. One that does not compile is left out, as the builder leaves it out, and so is one that
# gives more channels than a plugin carries. One that Faust did not finish stops the build, with
# build-effect.sh's reason.
if [ "$KIND" = "instrument" ]; then
    EFFECT_LINE="$(bash "$SHELL_DIR/tools/build-effect.sh" "$WORK/gen" "$WORK/gen/FaustEffect.h" "$PROJECT_DIR/dsp.dsp" 2> "$WORK/gen/effect.err")" || {
        cat "$WORK/gen/effect.err" >&2
        EFFECT_REASON="$(grep -v '^[[:space:]]*$' "$WORK/gen/effect.err" | tail -1 || true)"
        [ -n "$EFFECT_REASON" ] || EFFECT_REASON="the instrument's effect section could not be built"
        fail faust "$EFFECT_REASON"
    }
    case "$EFFECT_LINE" in
        built\ *)
            HAS_EFFECT=1; EFFECT_SECTION="built"
            read -r _ EFFECT_INS EFFECT_OUTS <<< "$EFFECT_LINE"
            say "faust: the effect section -> FaustEffect.h ($EFFECT_INS in, $EFFECT_OUTS out, $(wc -l < "$WORK/gen/FaustEffect.h" | tr -d ' ') lines)"
            ;;
        left-out\ *)
            EFFECT_SECTION="left out"; EFFECT_SECTION_ERROR="${EFFECT_LINE#left-out }"
            say "warning: the effect section is left out, so the plugin plays the voices alone: $EFFECT_SECTION_ERROR"
            ;;
        *) EFFECT_SECTION="none" ;;
    esac
fi

if [ -f "$PROJECT_DIR/logic.cpp" ]; then
    STAGE="logic"
    # Native code from a project: built only when the caller takes responsibility for it (decision 17).
    [ "$ALLOW_LOGIC" = "1" ] || fail logic "this project carries a logic module (logic.cpp), which is native code. Pass --allow-logic to build it; the desktop app does so only for the signed-in user's own projects until logic modules are sandboxed."
    if grep -E '^\s*#\s*include' "$PROJECT_DIR/logic.cpp" | grep -vE '#\s*include\s*"logic_api.h"' >/dev/null; then
        fail logic "logic.cpp may include nothing but logic_api.h"
    fi
    cp "$PROJECT_DIR/logic.cpp" "$WORK/gen/LogicModule.cpp"
    HAS_LOGIC=1
fi

# 4. Compile. -O3 as the plugin's Release build; hidden by default so only the module's C entry
#    points are visible and two versions can be loaded at once.
STAGE="compile"
COMMON=(-std=c++17 -O3 -DNDEBUG -fPIC -fvisibility=hidden -fvisibility-inlines-hidden -mmacosx-version-min=12.0
        -I "$WORK/gen" -I "$SHELL_DIR/platform" -I "$FAUST_INCLUDE")
compile() {
    local log="$WORK/compile.log"
    if ! clang++ "$@" > "$log" 2>&1; then
        cat "$log" >&2
        fail compile "the compiler refused it: $(grep -m1 -E 'error' "$log" | cut -c1-400)"
    fi
}
compile "${COMMON[@]}" -DHAS_LOGIC=$HAS_LOGIC -DHAS_EFFECT=$HAS_EFFECT -c "$SHELL_DIR/platform/PangeaModule.cpp" -o "$WORK/module.o"
OBJECTS=("$WORK/module.o")
if [ "$HAS_LOGIC" = "1" ]; then
    LOGIC_FLAGS=()
    while IFS= read -r flag; do LOGIC_FLAGS+=("$flag"); done < <(grep -E '^-' "$SHELL_DIR/platform/logic_native.flags")
    compile "${COMMON[@]}" "${LOGIC_FLAGS[@]}" '-DLOGIC_FILE="LogicModule.cpp"' -c "$SHELL_DIR/platform/logic_native.cpp" -o "$WORK/logic.o"
    OBJECTS+=("$WORK/logic.o")
fi
compile -shared -mmacosx-version-min=12.0 -Wl,-dead_strip "${OBJECTS[@]}" -o "$WORK/out/module.dylib"
say "compile: module.dylib ($(( $(wc -c < "$WORK/out/module.dylib") / 1024 )) KB$( [ "$HAS_EFFECT" = "1" ] && echo ", with its effect section")$( [ "$HAS_LOGIC" = "1" ] && echo ", with its logic module"))"

# 5. The module directory, all at once.
STAGE="write"
cp "$PROJECT_DIR/plugin.json" "$WORK/out/plugin.json"
if [ -f "$PROJECT_DIR/ui.json" ]; then cp "$PROJECT_DIR/ui.json" "$WORK/out/ui.json"; else printf '{}\n' > "$WORK/out/ui.json"; fi
SHA256="$(shasum -a 256 "$WORK/out/module.dylib" | cut -d' ' -f1)"
PROJECT_ID="$PROJECT_ID" VERSION_NUMBER="$VERSION_NUMBER" NAME="$NAME" SLUG="$SLUG" KIND="$KIND" HAS_LOGIC="$HAS_LOGIC" \
ABI="$ABI" FAUST_VERSION="$FAUST_VERSION" SHA256="$SHA256" python3 -I -c '
import json, os, datetime
e = os.environ
manifest = {
    "format": 1, "abi": int(e["ABI"]),
    "projectId": e["PROJECT_ID"] or None,
    "version": int(e["VERSION_NUMBER"]) if e["VERSION_NUMBER"] else None,
    "name": e["NAME"], "slug": e["SLUG"], "kind": e["KIND"], "logic": e["HAS_LOGIC"] == "1",
    "faust": e["FAUST_VERSION"],
    "builtAt": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
    "sha256": e["SHA256"],
}
print(json.dumps(manifest, indent=2))
' > "$WORK/out/manifest.json"
OLD=""
if [ -e "$MODULE_DIR" ]; then OLD="$WORK/old"; mv "$MODULE_DIR" "$OLD"; fi
mv "$WORK/out" "$MODULE_DIR" || fail write "could not move the module into $MODULE_DIR"
MODULE_PATH="$MODULE_DIR/module.dylib"
say "module: $MODULE_DIR"

STAGE="done"
finish 1
