#!/bin/bash
# compare-faust.sh: does the Faust the app carries write the same C++ as the Faust used today?
#
#   desktop/scripts/compare-faust.sh <faust the app carries> [<faust used today>]
#
# The second defaults to Homebrew's (/opt/homebrew/bin/faust), the one every Pangea build has used
# so far. Each compiles every .dsp in the repository (the shell's examples and fixtures, the site's
# Faust fixtures, the edit sets, the panel trials) exactly as the shell does: run_faust from
# shell/tools/run-faust.sh (-t 120 -sn), -lang cpp -cn mydsp -ftz 1, in a folder that holds only
# the file. Where the code defines an instrument's effect section, shell/tools/build-effect.sh
# writes its second class with each Faust as well. Decision 78's comparison is the model: the C++
# must be the same byte for byte, and a program both refuse must be refused with the same words.
#
# The last line is one JSON object: {"ok", "programs", "classes", "same", "bothRefused", "different": [...]}.
# Exit code 0 only when nothing differs.
set -uo pipefail

DESKTOP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO_DIR="$(cd "$DESKTOP_DIR/.." && pwd)"
SHELL_DIR="$REPO_DIR/shell"
NEW="${1:-}"
OLD="${2:-/opt/homebrew/bin/faust}"
[ -x "$NEW" ] && [ -x "$OLD" ] || { echo "usage: compare-faust.sh <faust the app carries> [<faust used today>]" >&2; exit 2; }
NEW_DIR="$(cd "$(dirname "$NEW")" && pwd)"
OLD_DIR="$(cd "$(dirname "$OLD")" && pwd)"
# shellcheck source=../../shell/tools/run-faust.sh
source "$SHELL_DIR/tools/run-faust.sh"

WORK="$(mktemp -d "${TMPDIR:-/tmp}/pangea-compare-faust.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

echo "new: $("$NEW" --version | head -1) at $NEW"
echo "old: $("$OLD" --version | head -1) at $OLD"

PROGRAMS=0; CLASSES=0; SAME=0; REFUSED=0; DIFFERENT=()

# compile <faust dir> <out dir> <source>: the voice's class and, when there is one, the effect's.
compile() {
    local fdir="$1" out="$2" src="$3"
    mkdir -p "$out/gen"
    cp "$src" "$out/gen/dsp.dsp"
    ( export PATH="$fdir:/usr/bin:/bin"; run_faust "$out/gen" "$out/faust.log" -lang cpp -cn mydsp -ftz 1 -o "$out/FaustDsp.h" dsp.dsp ) \
        && echo ok > "$out/status" || echo "failed $?" > "$out/status"
    if grep -qE '^effect[[:space:]]*(\(|=)' "$src"; then
        ( export PATH="$fdir:/usr/bin:/bin"; bash "$SHELL_DIR/tools/build-effect.sh" "$out/gen" "$out/FaustEffect.h" "$src" ) \
            > "$out/effect.line" 2> "$out/effect.err" || echo "failed" >> "$out/effect.line"
        sed -E 's/pangea_[0-9a-f]+_//g' "$out/effect.err" > "$out/effect.err.cmp"
    fi
}

# same <file a> <file b>: both missing, or both there and equal.
same() {
    if [ ! -e "$1" ] && [ ! -e "$2" ]; then return 0; fi
    [ -e "$1" ] && [ -e "$2" ] && cmp -s "$1" "$2"
}

while IFS= read -r src; do
    rel="${src#"$REPO_DIR"/}"
    n=$((PROGRAMS + 1))
    compile "$NEW_DIR" "$WORK/$n/new" "$src"
    compile "$OLD_DIR" "$WORK/$n/old" "$src"
    PROGRAMS=$n
    a="$WORK/$n/new"; b="$WORK/$n/old"
    ok=1
    same "$a/status" "$b/status" || ok=0
    if [ "$(cat "$a/status")" = ok ]; then
        same "$a/FaustDsp.h" "$b/FaustDsp.h" || ok=0
        CLASSES=$((CLASSES + 1))
    else
        same "$a/faust.log" "$b/faust.log" || ok=0
        REFUSED=$((REFUSED + 1))
    fi
    if [ -e "$a/effect.line" ] || [ -e "$b/effect.line" ]; then
        same "$a/effect.line" "$b/effect.line" || ok=0
        same "$a/FaustEffect.h" "$b/FaustEffect.h" || ok=0
        same "$a/effect.err.cmp" "$b/effect.err.cmp" || ok=0
        [ -e "$a/FaustEffect.h" ] && CLASSES=$((CLASSES + 1))
    fi
    if [ "$ok" = 1 ]; then
        SAME=$((SAME + 1))
        echo "same  $rel ($(cat "$a/status")$( [ -e "$a/effect.line" ] && echo ", effect section: $(cut -d' ' -f1 "$a/effect.line")"))"
    else
        DIFFERENT+=("$rel")
        echo "DIFF  $rel"
        diff "$b/FaustDsp.h" "$a/FaustDsp.h" 2>/dev/null | head -10
    fi
done < <(find "$REPO_DIR" -name '*.dsp' -type f \
            -not -path "$REPO_DIR/node_modules/*" -not -path "$REPO_DIR/desktop/node_modules/*" \
            -not -path "$REPO_DIR/.claude/*" -not -path "$REPO_DIR/shell/JUCE/*" \
            -not -path "$REPO_DIR/shell/build/*" -not -path "$REPO_DIR/shell/build-host/*" \
            -not -path "$REPO_DIR/desktop/src-tauri/target/*" | sort)

DIFF_JSON="$(printf '%s\n' "${DIFFERENT[@]+"${DIFFERENT[@]}"}" | python3 -I -c 'import json,sys; print(json.dumps([l for l in sys.stdin.read().splitlines() if l]))')"
OK=$([ "${#DIFFERENT[@]}" = 0 ] && echo true || echo false)
echo "{\"ok\": $OK, \"programs\": $PROGRAMS, \"classes\": $CLASSES, \"same\": $SAME, \"bothRefused\": $REFUSED, \"different\": $DIFF_JSON}"
[ "$OK" = true ]
