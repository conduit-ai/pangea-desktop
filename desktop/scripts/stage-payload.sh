#!/bin/bash
# stage-payload.sh: what the packaged app carries, laid out for Tauri's bundler.
#
#   desktop/scripts/stage-payload.sh
#
# 1. build-faust.sh: Faust (without LLVM) and Ninja at their pinned versions, in the tools cache.
# 2. compare-faust.sh: the app's Faust must write the same C++ as the Faust used so far for every
#    .dsp in the repository (decision 78's comparison). The Faust compared with is
#    PANGEA_COMPARE_FAUST, else Homebrew's when it is installed; with neither, packaging stops,
#    unless PANGEA_COMPARE_FAUST=none says to go on without the comparison.
# 3. Everything into desktop/src-tauri/target/pangea-payload:
#      bin/faust, bin/ninja                     the binaries (Tauri's externalBin takes them, as
#      bin/faust-<target>, bin/ninja-<target>   these names, into Contents/MacOS)
#      pangea/manifest.json                     what is carried, read by the app (toolchain.rs)
#      pangea/faust/include, pangea/faust/share Faust's headers and libraries
#      pangea/licences/faust, pangea/licences/ninja   licence texts, and Faust's complete source
#      pangea/licences/shell                    the shell's LICENSE (GNU AGPL 3.0), NOTICE and
#                                               FELT-LICENSE.md
#      pangea/shell, pangea/library             the shell as committed, without JUCE, and the
#                                               Faust pins its scripts read
#    tauri.bundle.conf.json maps pangea/ into Contents/Resources/pangea. The same folder is what
#    PANGEA_DESKTOP_PAYLOAD points at to run the app's setup against it without a bundle.
#
# The last line is one JSON object: {"ok", "payload", "faust", "ninja", "compared"}.
set -euo pipefail

DESKTOP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO_DIR="$(cd "$DESKTOP_DIR/.." && pwd)"
OUT="$DESKTOP_DIR/src-tauri/target/pangea-payload"
CONF="$DESKTOP_DIR/src-tauri/tauri.conf.json"

fail() { echo "error: $1" >&2; echo "{\"ok\": false, \"error\": \"$1\"}"; exit 1; }

echo "== the tools the app carries"
TOOLS_LINE="$(bash "$DESKTOP_DIR/scripts/build-faust.sh" | tee /dev/stderr | tail -1)"
FAUST_DIR="$(python3 -I -c 'import json,sys; d=json.loads(sys.argv[1]); print(d["faust"] if d["ok"] else "")' "$TOOLS_LINE")"
NINJA_DIR="$(python3 -I -c 'import json,sys; d=json.loads(sys.argv[1]); print(d["ninja"] if d["ok"] else "")' "$TOOLS_LINE")"
[ -n "$FAUST_DIR" ] && [ -n "$NINJA_DIR" ] || fail "build-faust.sh did not finish"

echo "== the app's Faust against the Faust used so far"
OLD_FAUST="${PANGEA_COMPARE_FAUST:-}"
if [ -z "$OLD_FAUST" ] && [ -x /opt/homebrew/bin/faust ]; then OLD_FAUST=/opt/homebrew/bin/faust; fi
COMPARED="null"
if [ "$OLD_FAUST" = "none" ]; then
    echo "comparison skipped (PANGEA_COMPARE_FAUST=none)"
elif [ -z "$OLD_FAUST" ]; then
    fail "no Faust to compare with: install the Faust used so far, set PANGEA_COMPARE_FAUST, or PANGEA_COMPARE_FAUST=none"
else
    COMPARE_LINE="$(bash "$DESKTOP_DIR/scripts/compare-faust.sh" "$FAUST_DIR/bin/faust" "$OLD_FAUST" | tail -1)" \
        || fail "the app's Faust writes different C++ from $OLD_FAUST: $COMPARE_LINE"
    echo "$COMPARE_LINE"
    COMPARED="$COMPARE_LINE"
fi

echo "== staging $OUT"
TARGET="$(rustc -vV | sed -n 's/^host: //p')"
[ -n "$TARGET" ] || fail "rustc did not say its host target"
rm -rf "$OUT"
mkdir -p "$OUT/bin" "$OUT/pangea/faust" "$OUT/pangea/licences"
cp "$FAUST_DIR/bin/faust" "$OUT/bin/faust"
cp "$NINJA_DIR/ninja" "$OUT/bin/ninja"
chmod 755 "$OUT/bin/faust" "$OUT/bin/ninja"
ln -s faust "$OUT/bin/faust-$TARGET"
ln -s ninja "$OUT/bin/ninja-$TARGET"
cp -R "$FAUST_DIR/include" "$OUT/pangea/faust/include"
cp -R "$FAUST_DIR/share" "$OUT/pangea/faust/share"
cp -R "$FAUST_DIR/licences" "$OUT/pangea/licences/faust"
cp "$FAUST_DIR/BUILD.json" "$OUT/pangea/licences/faust/BUILD.json"
mkdir -p "$OUT/pangea/licences/ninja"
cp "$NINJA_DIR/COPYING" "$OUT/pangea/licences/ninja/COPYING"

APP_VERSION="$(python3 -I -c 'import json,sys; print(json.load(open(sys.argv[1]))["version"])' "$CONF")"
COMMIT="$(git -C "$REPO_DIR" rev-parse --short=12 HEAD)"
if [ -n "$(git -C "$REPO_DIR" status --porcelain -- desktop shell library)" ]; then COMMIT="$COMMIT-dirty"; fi

# The shell, as committed: what build.sh, build-module.sh and build-host.sh need, with
# library/faust-versions.json beside it, where the scripts look (<shell>/../library). Not its
# screenshots (shell/docs) or the bench test's origin (shell/bench-origin), which builds never read.
git -C "$REPO_DIR" archive --format=tar HEAD shell library/faust-versions.json \
    | tar -x -C "$OUT/pangea" --exclude 'shell/docs' --exclude 'shell/bench-origin'
for f in build.sh build-module.sh build-host.sh check-project.sh CMakeLists.txt platform/PangeaModuleApi.h LICENSE NOTICE platform/web/FELT-LICENSE.md; do
    [ -f "$OUT/pangea/shell/$f" ] || fail "the staged shell has no $f"
done
[ ! -e "$OUT/pangea/shell/JUCE" ] || fail "the staged shell carries JUCE, which Pangea must not distribute"
# The shell's licence (GNU AGPL 3.0 or later, decision 127) beside the tools' licences, where the
# app's About section opens them; the copies inside pangea/shell travel with the source.
mkdir -p "$OUT/pangea/licences/shell"
cp "$OUT/pangea/shell/LICENSE" "$OUT/pangea/shell/NOTICE" "$OUT/pangea/shell/platform/web/FELT-LICENSE.md" "$OUT/pangea/licences/shell/"
echo "$COMMIT" > "$OUT/pangea/shell/VERSION"
FAUST_VERSION="$("$OUT/bin/faust" --version | head -1 | sed -E 's/.*Version ([0-9.]+).*/\1/')"
NINJA_VERSION="$("$OUT/bin/ninja" --version)"
APP_VERSION="$APP_VERSION" COMMIT="$COMMIT" FAUST_VERSION="$FAUST_VERSION" NINJA_VERSION="$NINJA_VERSION" python3 -I -c '
import json, os
e = os.environ
print(json.dumps({
    "format": 1,
    "appVersion": e["APP_VERSION"],
    "commit": e["COMMIT"],
    "faust": {"version": e["FAUST_VERSION"]},
    "ninja": {"version": e["NINJA_VERSION"]},
}, indent=2))
' > "$OUT/pangea/manifest.json"

echo "{\"ok\": true, \"payload\": \"$OUT\", \"faust\": \"$FAUST_VERSION\", \"ninja\": \"$NINJA_VERSION\", \"compared\": $COMPARED}"
