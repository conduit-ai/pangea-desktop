#!/bin/bash
# build-effect.sh: an instrument's effect section as its own Faust class (decision 73).
#
#   tools/build-effect.sh <directory holding dsp.dsp> <FaustEffect.h to write> [<the source's own path>]
#
# The third argument is only for messages: the file the member's project gave, which the
# directory holds a copy of.
#
# An instrument's Faust code may define, beside `process` (one voice), `effect`: one stage that all
# its voices share, where the delay, the reverb, the output level and the safety clip usually go.
# The builder plays it: faustwasm compiles `effect` as a second program and runs it on
# the mixed voices. This writes that second program for the native build, so the plugin plays it
# too. build.sh and build-module.sh call it for an instrument, after check-project.sh has passed
# and Faust has compiled the voice from the same copy of dsp.dsp.
#
# The program is the source inside an environment, with its channels adapted as faustwasm 0.20.0
# adapts them (FaustPolyDspGenerator.compile): the voices' outputs go to the effect's inputs, split
# when there are fewer and summed when there are more, and the effect's outputs are followed by
# silent channels up to the widest of the three counts. So the class takes what the voices give
# and gives what the builder's node gives.
#
# Prints one line and exits 0:
#   built <inputs> <outputs>   class mydsp_effect is in <FaustEffect.h>
#   none                       the code defines no `effect`; nothing is written
#   left-out <why>             `effect` is defined but cannot be built: it does not compile (the
#                              builder then plays the voices alone, so the plugin does too), or it
#                              gives more channels than the shell carries. Nothing is written.
# Exits 1, with the reason on stderr, when it could not run at all, or when Faust did not finish:
# stopped at its time limit or by a signal, which says nothing about the code, so the section is
# not left out.
set -euo pipefail

SHELL_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# run_faust and faust_failure: Faust with the shell's time limit, and what to say when it fails.
source "$SHELL_DIR/tools/run-faust.sh"
DIR="${1:-}"; OUT="${2:-}"; SOURCE_NAME="${3:-}"
[ -n "$DIR" ] && [ -n "$OUT" ] || { echo "usage: build-effect.sh <directory holding dsp.dsp> <FaustEffect.h>" >&2; exit 1; }
[ -f "$DIR/dsp.dsp" ] || { echo "build-effect: no dsp.dsp in $DIR" >&2; exit 1; }
command -v faust >/dev/null || { echo "build-effect: faust not found" >&2; exit 1; }
# Faust runs inside the directory, so both paths are made whole first.
case "$OUT" in /*) ;; *) OUT="$(pwd)/$OUT" ;; esac
DIR="$(cd "$DIR" && pwd)"
SOURCE_NAME="${SOURCE_NAME:-$DIR/dsp.dsp}"

# The program's own definitions sit in the scope the source can see, so their names carry a hash
# of the source itself: no source can hold the hash of its own text, so none can name them. An
# `effect` can therefore use nothing here that the builder's compile would not also find, and a
# project cannot have an effect in its plugin alone.
# What comes before the source is one line with no line end, so a line the compiler names is the
# same line of dsp.dsp. The new line before the closing brace ends a comment on its last line.
# The program is itself called dsp.dsp, in a directory of its own: code that declares no name
# gets its root group named after its file, and the voice's and the effect's roots must be one
# name for a control that both read to have one address, as it has in the builder.
P="pangea_$(shasum -a 256 "$DIR/dsp.dsp" | cut -c1-32)_"
rm -rf "$DIR/effect"
mkdir -p "$DIR/effect"
{
    printf '%s ' \
        "${P}bus(n) = par(i, n, _);" \
        "${P}adapt(1, n, m) = ${P}bus(n) <: ${P}bus(m);" \
        "${P}adapt(0, n, m) = ${P}bus(n) :> ${P}bus(m);" \
        "${P}widen(1, n, w) = ${P}bus(n);" \
        "${P}widen(0, n, w) = ${P}bus(n), par(i, w - n, 0);" \
        "${P}code = environment {"
    cat "$DIR/dsp.dsp"
    printf '\n};\n'
    printf '%s\n' \
        "${P}voice = outputs(${P}code.process);" \
        "${P}ins = inputs(${P}code.effect);" \
        "${P}outs = outputs(${P}code.effect);" \
        "${P}width = max(${P}voice, max(${P}ins, ${P}outs));" \
        "process = ${P}adapt(${P}voice <= ${P}ins, ${P}voice, ${P}ins) : ${P}code.effect : ${P}widen(${P}outs == ${P}width, ${P}outs, ${P}width);"
} > "$DIR/effect/dsp.dsp"

rm -f "$OUT"
# The same options as the voice (build.sh, build-module.sh), and its own class name.
STATUS=0
run_faust "$DIR/effect" "$DIR/effect/faust.log" -lang cpp -cn mydsp_effect -ftz 1 -o "$OUT" dsp.dsp || STATUS=$?
if [ "$STATUS" = 0 ]; then
    # The class's own two lines; a table the code builds has its own, named after the table.
    count() { awk -v f="virtual int $1()" 'index($0, f) { getline; gsub(/[^0-9]/, ""); print; exit }' "$OUT"; }
    INS="$(count getNumInputs)"; OUTS="$(count getNumOutputs)"
    # The shell hands Faust a fixed number of channel pointers (kMaxFaustChannels). The effect's
    # inputs come from a buffer the size of the voices' mix, but its outputs go through that array.
    MOST="$(sed -nE 's/^constexpr int kMaxFaustChannels = ([0-9]+);.*/\1/p' "$SHELL_DIR/platform/HybridShell.cpp" | head -1)"
    if [ "${OUTS:-0}" -gt "${MOST:-8}" ]; then
        rm -f "$OUT"
        echo "left-out the effect section gives $OUTS channels, and a plugin carries ${MOST:-8} at most"
        exit 0
    fi
    echo "built $INS $OUTS"
    exit 0
fi
rm -f "$OUT"
if [ "$STATUS" -gt 128 ]; then
    faust_failure "$STATUS" "$DIR/effect/faust.log" "the effect section of $SOURCE_NAME" >&2
    exit 1
fi
if grep -qE 'undefined symbol : effect$' "$DIR/effect/faust.log"; then
    echo "none"
else
    # The compiler's words, on one line, with this program's own names (which the compiler may
    # shorten with dots in the middle) and its file name taken out.
    echo "left-out $(tr '\n' ' ' < "$DIR/effect/faust.log" | sed -E 's/pangea_[0-9a-f]+(\.\.\.[0-9a-f]*)?_//g; s/[[:space:]]+/ /g; s/ $//; s/dsp\.dsp:([0-9]+) ?:/line \1:/g' | cut -c1-400)"
fi
