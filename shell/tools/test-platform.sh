#!/bin/bash
# Compiles and runs tools/test-platform.cpp: the note safety net (platform/PangeaNotes.h), the
# display hints of logic controls (platform/PangeaLogicMeta.h), the bypass's fader
# (platform/PangeaBypass.h) and the sidechain's feed (platform/PangeaSidechain.h), with a bare
# clang++ and no JUCE.
#
#   shell/tools/test-platform.sh
set -euo pipefail
SHELL_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
clang++ -std=c++17 -O1 -Wall -Wextra -Werror -I "$SHELL_DIR/platform" "$SHELL_DIR/tools/test-platform.cpp" -o "$OUT/test-platform"
"$OUT/test-platform"
