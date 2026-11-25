#!/bin/bash
# The step budget for logic modules in the native plugin (docs/midi-tools.md, "The step budget";
# decision 46).
#
#   shell/tools/test-budget.sh            with a bare clang++ and no JUCE, a few seconds:
#     1. tools/fixtures/runaway, compiled as the plugin compiles a module: every fault is
#        stopped for the right reason, heavy honest work is not (tools/test-budget.cpp);
#     2. MIDI Steps, Two Hands, Rolling Line, Pocket Drums, Ostinato Hands, Bass and Drums and
#        Hands and Drums (built on the MIDI shelf: Line, Drums and Keys, alone and in pairs) over
#        many settings: the most steps one block takes;
#     3. what a budget cannot stop is refused when it compiles, and its fix compiles;
#     4. every function of each compiled module calls the budget (tools/check-instrumented.py),
#        and a function a module exempted is found.
#   shell/tools/test-budget.sh --plugin   also builds Runaway as real plugins (build.sh --verify)
#     and runs `pangea-render stop-check` on the processor and on the built VST3, so the budget
#     is proven inside the plugin a DAW loads. About a minute after the first build.
set -euo pipefail
SHELL_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PLATFORM="$SHELL_DIR/platform"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
FLAGS=()
while IFS= read -r flag; do FLAGS+=("$flag"); done < <(grep '^-' "$PLATFORM/logic_native.flags")

# The module's own translation unit, exactly as CMakeLists.txt compiles it, for one logic.cpp.
module_object() { # <logic.cpp> <out.o>
    clang++ -std=c++17 -O2 "${FLAGS[@]}" -I "$PLATFORM" -DLOGIC_FILE="\"$1\"" -c "$PLATFORM/logic_native.cpp" -o "$2"
}

clang++ -std=c++17 -O2 -Wall -Wextra -Werror -I "$PLATFORM" -c "$SHELL_DIR/tools/test-budget.cpp" -o "$OUT/driver.o"
for name in runaway midi-steps two-hands rolling-line pocket-drums ostinato-hands bass-and-drums hands-and-drums; do
    case "$name" in
        runaway) src="$SHELL_DIR/tools/fixtures/runaway/logic.cpp" ;;
        *) src="$SHELL_DIR/examples/$name/logic.cpp" ;;
    esac
    module_object "$src" "$OUT/$name.o"
    clang++ "$OUT/driver.o" "$OUT/$name.o" -o "$OUT/$name"
done

echo "== 1. Runaway: the faults"
"$OUT/runaway" faults
echo "== 2. MIDI Steps, Two Hands, Rolling Line, Pocket Drums, Ostinato Hands, Bass and Drums and Hands and Drums: the most steps a block"
"$OUT/midi-steps" calibrate
"$OUT/two-hands" calibrate
"$OUT/rolling-line" calibrate
"$OUT/pocket-drums" calibrate
"$OUT/ostinato-hands" calibrate
"$OUT/bass-and-drums" calibrate
"$OUT/hands-and-drums" calibrate

echo "== 3. Refused when it compiles"
failures=0
# A whole module around one line of `extra` (at file scope) and one in process (`inner`).
write_module() { # <file> <extra> <inner>
    cat > "$1" <<EOF
#include "logic_api.h"
static const char* const kLogicControls[1] = { "" };
static const int kNumLogicControls = 0;
static const LogicParam kLogicParams[] = { { "Size", 1.0f, 64.0f, 8.0f, 1.0f, "[style:knob]" } };
static const int kNumLogicParams = 1;
$2
struct Logic
{
    float last = 0.0f;
    void prepare (double) {}
    int process (const LogicBlock& b, LogicEvent*, int)
    {
        $3
        return 0;
    }
};
EOF
}
refused() { # <what> <diagnostic> <extra> <inner> <fixed extra> <fixed inner>
    write_module "$OUT/case.cpp" "$3" "$4"
    if module_object "$OUT/case.cpp" "$OUT/case.o" > "$OUT/case.log" 2>&1; then
        echo "FAIL  $1: it compiled"; failures=$((failures + 1)); return
    fi
    if ! grep -q -- "$2" "$OUT/case.log"; then
        echo "FAIL  $1: refused, but not with $2:"; sed 's/^/      /' "$OUT/case.log"; failures=$((failures + 1)); return
    fi
    write_module "$OUT/fixed.cpp" "$5" "$6"
    if ! module_object "$OUT/fixed.cpp" "$OUT/fixed.o" > "$OUT/fixed.log" 2>&1; then
        echo "FAIL  $1: the fix did not compile:"; sed 's/^/      /' "$OUT/fixed.log"; failures=$((failures + 1)); return
    fi
    echo "ok    $1: refused ($(grep -m1 -o -- "$2" "$OUT/case.log")), and its fix compiles"
}
refused "an array sized at run time" "Wvla" \
    "" "const int n = (int) b.params[0]; float a[n]; a[0] = 1.0f; last = a[0];" \
    "" "float a[64] = {}; a[(int) b.params[0] % 64] = 1.0f; last = a[0];"
refused "a 20 KB array on the stack" "frame-larger-than" \
    "" "volatile float big[5000]; big[(int) b.params[0]] = 1.0f; last = big[0];" \
    "struct Table { float big[5000]; }; static Table gTable = {};" "gTable.big[(int) b.params[0]] = 1.0f; last = gTable.big[0];"
refused "a global made by running code" "global-constructors" \
    "static float half (float x) { return x * 0.5f; } static float gHalf = half (3.0f);" "last = gHalf;" \
    "static constexpr float half (float x) { return x * 0.5f; } static float gHalf = half (3.0f);" "last = gHalf;"
refused "a global with a destructor" "exit-time-destructors" \
    "struct Noisy { ~Noisy() {} }; static Noisy gNoisy;" "last = 1.0f;" \
    "struct Quiet { int n = 0; }; static Quiet gQuiet;" "last = (float) gQuiet.n;"

if [ "$failures" -gt 0 ]; then echo "$failures refusal checks failed"; exit 1; fi
echo "all refusal checks passed"

echo "== 4. Every function counts"
for name in runaway midi-steps two-hands rolling-line pocket-drums ostinato-hands bass-and-drums hands-and-drums; do
    python3 "$SHELL_DIR/tools/check-instrumented.py" "$OUT/$name.o" | sed "s/^check-instrumented:/ok    $name:/"
done
# The checkers refuse the attribute; this is what build.sh would still catch if one did not.
sed 's/    int dive (int depth, const int\* above)/    __attribute((no_sanitize("coverage"))) int dive (int depth, const int* above)/' \
    "$SHELL_DIR/tools/fixtures/runaway/logic.cpp" > "$OUT/exempt.cpp"
grep -q no_sanitize "$OUT/exempt.cpp" || { echo "FAIL  the exempting module was not written"; exit 1; }
module_object "$OUT/exempt.cpp" "$OUT/exempt.o"
if python3 "$SHELL_DIR/tools/check-instrumented.py" "$OUT/exempt.o" > "$OUT/exempt.log"; then
    echo "FAIL  a function exempted with no_sanitize was not found"; exit 1
fi
echo "ok    a function exempted with no_sanitize is found: $(sed 's/^check-instrumented: //' "$OUT/exempt.log")"

if [ "${1:-}" = "--plugin" ]; then
    echo "== 5. Runaway as real plugins"
    "$SHELL_DIR/build.sh" "$SHELL_DIR/tools/fixtures/runaway" --verify --allow-logic | sed 's/^/build: /'
    RENDER="$SHELL_DIR/build/pangea-render_artefacts/Release/pangea-render"
    [ -x "$RENDER" ] || RENDER="$SHELL_DIR/build/pangea-render_artefacts/pangea-render"
    "$RENDER" stop-check "$SHELL_DIR/build/PangeaMidi_artefacts/Release/VST3/Runaway.vst3"
fi
