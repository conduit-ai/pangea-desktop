#!/bin/bash
# Builds, installs and validates every example (or, with --no-install, only builds them), then
# draws each plugin's interface into shell/docs/screens/, so the native editor can be compared
# with docs/interface-styles.md and the web panel. Poly Saw is also drawn in all five styles, with a hand-made layout and with a
# ui.json full of mistakes (to show the fallbacks), from the files in shell/docs/screens/ui/.
# The two MIDI tools are drawn after a couple of seconds of playing, so their playheads show:
# MIDI Steps in all five styles and with a layout that places its controls by their keys, and
# Two Hands with its own layout.
#
#   shell/tools/render-screens.sh [--no-build | --no-install]
#
#   --no-build     draw with the pangea-render of the last build only (no builds, no auval); the
#                  examples' own pictures then need their builds, so only Poly Saw's are drawn.
#   --no-install   build and verify every example, and install and validate none: for a working
#                  copy that must leave the Mac's installed plugins alone.
set -euo pipefail
SHELL_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SCREENS="$SHELL_DIR/docs/screens"
RENDER="$SHELL_DIR/build/pangea-render_artefacts/Release/pangea-render"
if [ ! -d "$SCREENS/ui" ]; then
    echo "render-screens.sh: $SCREENS/ui is not here. The public copy of this repository leaves out shell/docs (the pictures and the interface files they are drawn with), so this script draws nothing there." >&2
    exit 2
fi
mkdir -p "$SCREENS"
BUILD=1
INSTALL=(--install)
[ "${1:-}" = "--no-build" ] && BUILD=0
[ "${1:-}" = "--no-install" ] && INSTALL=()

build() {   # build <example> [build.sh options]
    local example="$1"; shift
    echo "== $example"
    # --native-editor: these pictures are of the shell's own editor, the only one that can be drawn
    # off screen. A plugin built without it has the component library's panel in a web view
    # (decision 77), so the examples this installs carry the older panel until they are built again.
    bash "$SHELL_DIR/build.sh" "$SHELL_DIR/examples/$example" ${INSTALL[@]+"${INSTALL[@]}"} --verify --native-editor "$@" | tail -1
}

draw() {   # draw <png name> [pangea-render editor options]
    local name="$1"; shift
    "$RENDER" editor "$SCREENS/$name.png" "$@" | sed "s/^/$name: /"
}

if [ "$BUILD" = "1" ]; then
    build tape-drift && draw tape-drift
    build stomp-fuzz && draw stomp-fuzz --set boost=1
    build glass-chorus && draw glass-chorus --set wide=1
    build acid-line --allow-logic && draw acid-line
    build two-hands --allow-logic && draw two-hands --play 2.2
    build midi-steps --allow-logic && draw midi-steps --play 2.2
    for style in vintage minimal pedal playful retro; do
        draw "midi-steps-$style" --ui "$SCREENS/ui/midi-steps-$style.json" --play 2.2
    done
    draw midi-steps-layout --ui "$SCREENS/ui/midi-steps-layout.json" --play 2.2
    build dub-keys && draw dub-keys
    build poly-saw && draw poly-saw
fi

# Poly Saw is the last build, so its render tool draws the variants.
for style in vintage minimal pedal playful retro; do
    draw "poly-saw-$style" --ui "$SCREENS/ui/$style.json"
done
draw poly-saw-layout --ui "$SCREENS/ui/poly-saw-layout.json"
draw poly-saw-fallbacks --ui "$SCREENS/ui/fallbacks.json"
draw poly-saw-small --ui "$SCREENS/ui/retro.json" --width 300
