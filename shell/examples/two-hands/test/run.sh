#!/bin/bash
# Builds and runs every Two Hands check (README.md, "How it was verified"):
#   1. the native harness, plain clang++ with no JUCE, built twice: with the default flags
#      and with fused multiply-add off;
#   2. the gold test against the Pattern Player engine, read from its own folder, never copied;
#   3. the WebAssembly build with the flags of docs/midi-tools.md, run from Node through the
#      ABI and compared with the native output to the sample;
#   4. the source rules of shell/check-project.sh, and the web side (test/web-check.ts: the
#      logic rules, the strict gate, compileLogic and checkMidiTool) when it and node_modules
#      are there; the public copy of the shell has no web-check.ts, and the step says so.
#
#   shell/examples/two-hands/test/run.sh
#
# Settings, each with a default for a Mac with Homebrew's llvm and lld:
#   CXX             the native compiler (clang++)
#   WASM_CXX        clang for WebAssembly (/opt/homebrew/opt/llvm/bin/clang++)
#   WASM_LD         wasm-ld (/opt/homebrew/opt/lld/bin/wasm-ld)
#   PATTERN_PLAYER  a checkout of Pattern Player, given by you (no default); without it the
#                   gold test is skipped and says so
# Exit 0 when everything passes.
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT="$(cd "$HERE/.." && pwd)"
REPO="$(cd "$PROJECT/../../.." && pwd)"
PLATFORM="$REPO/shell/platform"
CXX="${CXX:-clang++}"
WASM_CXX="${WASM_CXX:-/opt/homebrew/opt/llvm/bin/clang++}"
WASM_LD="${WASM_LD:-/opt/homebrew/opt/lld/bin/wasm-ld}"
PATTERN_PLAYER="${PATTERN_PLAYER:-}"

BUILD="$(mktemp -d "${TMPDIR:-/tmp}/two-hands-test.XXXXXX")"
trap 'rm -rf "$BUILD"' EXIT
FAILED=0
step() { printf '\n== %s\n' "$*"; }
fail() { echo "FAILED: $*"; FAILED=$((FAILED + 1)); }

step "Native harness (clang++ -std=c++17, default flags)"
"$CXX" -std=c++17 -O2 -Wall -Wextra -I "$PLATFORM" -I "$PROJECT" "$HERE/harness.cpp" -o "$BUILD/harness" \
    || fail "the native harness did not build"
if [ -x "$BUILD/harness" ]; then
    "$BUILD/harness" --dump "$BUILD/native.dump" || fail "the native harness"
fi

step "Native harness again, with fused multiply-add off"
"$CXX" -std=c++17 -O2 -ffp-contract=off -I "$PLATFORM" -I "$PROJECT" "$HERE/harness.cpp" -o "$BUILD/harness-nofma" \
    || fail "the native harness (no fused multiply-add) did not build"
if [ -x "$BUILD/harness-nofma" ]; then
    "$BUILD/harness-nofma" > "$BUILD/harness-nofma.txt"; status=$?
    tail -1 "$BUILD/harness-nofma.txt"
    [ "$status" -eq 0 ] || fail "the native harness with fused multiply-add off"
fi

step "Gold test against Pattern Player"
CORE="$PATTERN_PLAYER/core"
if [ -n "$PATTERN_PLAYER" ] && [ -f "$CORE/pp_core.cpp" ] && [ -f "$CORE/pp_theory.cpp" ]; then
    "$CXX" -std=c++17 -O2 -ffp-contract=off -I "$PLATFORM" -I "$PROJECT" -I "$CORE" \
        "$HERE/gold.cpp" "$CORE/pp_core.cpp" "$CORE/pp_theory.cpp" -o "$BUILD/gold" \
        || fail "the gold test did not build"
    if [ -x "$BUILD/gold" ]; then
        echo "-- both built with fused multiply-add off: the source as written"
        "$BUILD/gold" || fail "the gold test"
    fi
    "$CXX" -std=c++17 -O2 -I "$PLATFORM" -I "$PROJECT" -I "$CORE" \
        "$HERE/gold.cpp" "$CORE/pp_core.cpp" "$CORE/pp_theory.cpp" -o "$BUILD/gold-fused" \
        || fail "the gold test (default flags) did not build"
    if [ -x "$BUILD/gold-fused" ]; then
        echo "-- both built with the default flags, as Pattern Player ships"
        "$BUILD/gold-fused" --fused > "$BUILD/gold-fused.txt"; status=$?
        grep -v '^    ' "$BUILD/gold-fused.txt"
        [ "$status" -eq 0 ] || fail "the gold test with the default flags"
    fi
else
    echo "skipped: no Pattern Player (set PATTERN_PLAYER to a checkout of it)${PATTERN_PLAYER:+; none at $PATTERN_PLAYER}"
fi

step "WebAssembly build and comparison"
if [ -x "$WASM_CXX" ] && [ -x "$WASM_LD" ]; then
    "$WASM_CXX" --target=wasm32 -std=c++17 -O2 -nostdlib -nostdinc -fno-exceptions -fno-rtti \
        -I "$PLATFORM" -I "$PROJECT" '-DLOGIC_FILE="logic.cpp"' "$PLATFORM/logic_wasm.cpp" \
        -fuse-ld="$WASM_LD" -Wl,--no-entry -Wl,--strip-all -Wl,--export-if-defined=__wasm_call_ctors \
        -Wl,--initial-memory=1048576 -Wl,--max-memory=4194304 -Wl,-z,stack-size=262144 \
        -o "$BUILD/logic.wasm" || fail "the WebAssembly build"
    if [ -f "$BUILD/logic.wasm" ] && [ -f "$BUILD/native.dump" ]; then
        echo "logic.wasm: $(wc -c < "$BUILD/logic.wasm" | tr -d ' ') bytes"
        node "$HERE/wasm-compare.mjs" "$BUILD/logic.wasm" "$BUILD/native.dump" || fail "WebAssembly against native"
    fi
else
    fail "no clang or wasm-ld for WebAssembly ($WASM_CXX, $WASM_LD)"
fi

step "Source rules (shell/check-project.sh)"
"$REPO/shell/check-project.sh" "$PROJECT" || fail "check-project.sh"

step "Web checks (the logic rules, the strict gate, compileLogic, checkMidiTool, the layout)"
if [ ! -f "$HERE/web-check.ts" ]; then
    # The public copy of the shell leaves it out: it checks against the site's own code.
    echo "skipped: test/web-check.ts is not here (it needs the site's code, which this copy does not have)"
elif [ -d "$REPO/node_modules" ]; then
    (cd "$REPO" && npx tsx "$HERE/web-check.ts") || fail "the web checks"
else
    echo "skipped: no node_modules (run pnpm install at $REPO)"
fi

echo
if [ "$FAILED" -eq 0 ]; then echo "Two Hands: everything passed"; else echo "Two Hands: $FAILED part(s) failed"; fi
exit "$FAILED"
