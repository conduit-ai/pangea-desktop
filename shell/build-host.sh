#!/bin/bash
# build-host.sh: builds the Pangea plugin (docs/pangea-plugin.md): Pangea (an effect), Pangea
# Instrument (instruments and MIDI tools) and Pangea MIDI FX (MIDI tools in Logic's MIDI FX slot).
#
#   build-host.sh [--install] [--check] [--no-auval] [--clean]
#
# Its own build directory (build-host/), so a project's build is never touched. --install copies
# the plugins into ~/Library/Audio/Plug-Ins, restarts AudioComponentRegistrar so Logic sees them,
# and validates the three Audio Units with auval. --check also builds pangea-host-check and runs
# tools/test-host.sh, which loads real modules into all three plugins and checks what they do.
# The last line is one JSON object: {"ok", "stage", "installed", "auval", "check", "seconds", "error"?}.
set -euo pipefail

SHELL_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SHELL_DIR/build-host"
# The version a DAW sees. It goes up whenever the plugin's parameters or buses change, so that a DAW
# which keeps what it read of an Audio Unit by its version reads them again: 0.3.0 gives Pangea (the
# effect) a sidechain input, off by default (docs/sidechain.md); 0.2.0 has the bypass after the
# 128 slots (decision 75), 0.1.0 had the slots alone.
HOST_VERSION="0.3.0"
STARTED=$(date +%s)
INSTALL=0; CHECK=0; RUN_AUVAL=1; CLEAN=0
for arg in "$@"; do
    case "$arg" in
        --install) INSTALL=1 ;;
        --check) CHECK=1 ;;
        --no-auval) RUN_AUVAL=0 ;;
        --clean) CLEAN=1 ;;
        --help|-h) sed -n '2,12p' "$0"; exit 0 ;;
        *) echo "unknown option: $arg" >&2; exit 2 ;;
    esac
done

STAGE="start"; INSTALLED=0; AUVAL="skipped"; CHECK_RESULT="skipped"
finish() {
    local ok="$1" error="${2:-}"
    OK="$ok" ERROR="$error" STAGE="$STAGE" INSTALLED="$INSTALLED" AUVAL="$AUVAL" CHECK_RESULT="$CHECK_RESULT" \
    SECONDS_TAKEN=$(( $(date +%s) - STARTED )) python3 -I -c '
import json, os
e = os.environ
out = {"ok": e["OK"] == "1", "stage": e["STAGE"], "installed": e["INSTALLED"] == "1", "auval": e["AUVAL"],
       "check": e["CHECK_RESULT"], "seconds": int(e["SECONDS_TAKEN"])}
if e["ERROR"]:
    out["error"] = e["ERROR"]
print(json.dumps(out))
'
    if [ "$ok" = "1" ]; then exit 0; else exit 1; fi
}
fail() { echo "error: $2" >&2; STAGE="$1"; finish 0 "$2"; }
say() { echo "[$(( $(date +%s) - STARTED ))s] $*"; }

STAGE="tools"
command -v cmake >/dev/null || fail tools "cmake not found. Install it with: brew install cmake"
command -v ninja >/dev/null || fail tools "ninja not found. Install it with: brew install ninja"
command -v faust >/dev/null || fail tools "faust not found. Install it with: brew install faust"
[ -f "$SHELL_DIR/JUCE/CMakeLists.txt" ] || fail tools "JUCE is missing. Run: git clone --depth 1 --branch 8.0.10 https://github.com/juce-framework/JUCE $SHELL_DIR/JUCE"
FAUST_BIN="$(command -v faust)"
FAUST_INCLUDE="$(cd "$(dirname "$FAUST_BIN")/.." && pwd)/include"
if [ ! -f "$FAUST_INCLUDE/faust/dsp/poly-dsp.h" ] && command -v brew >/dev/null; then
    FAUST_INCLUDE="$(brew --prefix faust 2>/dev/null)/include"
fi
[ -f "$FAUST_INCLUDE/faust/dsp/poly-dsp.h" ] || fail tools "Faust architecture headers not found next to $FAUST_BIN"

STAGE="configure"
[ "$CLEAN" = "1" ] && rm -rf "$BUILD_DIR"
BUILD_CHECK=OFF; [ "$CHECK" = "1" ] && BUILD_CHECK=ON
cmake -S "$SHELL_DIR" -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DPANGEA_HOST_ONLY=ON -DPANGEA_BUILD_HOST_CHECK=$BUILD_CHECK -DPANGEA_VERSION="$HOST_VERSION" \
    -DPANGEA_FAUST_INCLUDE="$FAUST_INCLUDE" > "$BUILD_DIR.configure.log" 2>&1 \
    || { tail -20 "$BUILD_DIR.configure.log" >&2; fail configure "cmake configure failed (see $BUILD_DIR.configure.log)"; }
say "configured $BUILD_DIR"

STAGE="build"
TARGETS=(PangeaHostEffect_AU PangeaHostEffect_VST3 PangeaHostInstrument_AU PangeaHostInstrument_VST3 PangeaHostMidiFx_AU)
[ "$CHECK" = "1" ] && TARGETS+=(pangea-host-check)
cmake --build "$BUILD_DIR" --target "${TARGETS[@]}" > "$BUILD_DIR.build.log" 2>&1 \
    || { grep -E 'error:' "$BUILD_DIR.build.log" | head -20 >&2; fail build "the build failed: $(grep -m1 -E 'error:' "$BUILD_DIR.build.log" | cut -c1-300)"; }
say "built $(printf '%s ' "${TARGETS[@]}")"

ART="$BUILD_DIR"
COMPONENTS=("$ART/PangeaHostEffect_artefacts/Release/AU/Pangea.component"
            "$ART/PangeaHostInstrument_artefacts/Release/AU/Pangea Instrument.component"
            "$ART/PangeaHostMidiFx_artefacts/Release/AU/Pangea MIDI FX.component")
VST3S=("$ART/PangeaHostEffect_artefacts/Release/VST3/Pangea.vst3"
       "$ART/PangeaHostInstrument_artefacts/Release/VST3/Pangea Instrument.vst3")

if [ "$CHECK" = "1" ]; then
    STAGE="check"
    if bash "$SHELL_DIR/tools/test-host.sh" "$ART/pangea-host-check_artefacts/Release/pangea-host-check" "${VST3S[0]}" "${VST3S[1]}"; then
        CHECK_RESULT="passed"
    else
        CHECK_RESULT="failed"
        fail check "tools/test-host.sh failed"
    fi
fi

if [ "$INSTALL" = "1" ]; then
    STAGE="install"
    COMP_DIR="$HOME/Library/Audio/Plug-Ins/Components"
    VST3_DIR="$HOME/Library/Audio/Plug-Ins/VST3"
    mkdir -p "$COMP_DIR" "$VST3_DIR"
    for c in "${COMPONENTS[@]}"; do rm -rf "$COMP_DIR/$(basename "$c")"; cp -R "$c" "$COMP_DIR/"; done
    for v in "${VST3S[@]}"; do rm -rf "$VST3_DIR/$(basename "$v")"; cp -R "$v" "$VST3_DIR/"; done
    killall -9 AudioComponentRegistrar >/dev/null 2>&1 || true
    INSTALLED=1
    say "installed into $COMP_DIR and $VST3_DIR"
    if [ "$RUN_AUVAL" = "1" ]; then
        STAGE="auval"
        AUVAL="passed"
        sleep 1   # give the registrar a moment to come back with the new components
        # auval must run in the user's GUI session, where the per-user registrar lives (as build.sh does).
        AUVAL_RUNNER=()
        if [ "$(launchctl managername 2>/dev/null)" != "Aqua" ]; then
            AUVAL_RUNNER=("$SHELL_DIR/tools/run-in-gui-session.sh")
        fi
        for spec in "aufx Phfx" "aumu Phin" "aumi Phmi"; do
            # shellcheck disable=SC2086
            ${AUVAL_RUNNER[@]+"${AUVAL_RUNNER[@]}"} /usr/bin/auval -v $spec Pnga > "$BUILD_DIR.auval-${spec// /-}.log" 2>&1 || true
            if ! grep -q "AU VALIDATION SUCCEEDED" "$BUILD_DIR.auval-${spec// /-}.log"; then
                AUVAL="failed"
                tail -15 "$BUILD_DIR.auval-${spec// /-}.log" >&2
                fail auval "auval failed for $spec Pnga (see $BUILD_DIR.auval-${spec// /-}.log)"
            fi
            say "auval $spec Pnga: $(grep -E 'AU VALIDATION SUCCEEDED|FAILED' "$BUILD_DIR.auval-${spec// /-}.log" | head -1)"
        done
    fi
fi

STAGE="done"
finish 1
