# run-faust.sh: how the shell runs Faust, and what it says when Faust does not finish.
# build.sh, build-module.sh and tools/build-effect.sh source it.
#
#   run_faust <directory> <log file> <faust arguments>...
#       Runs faust inside <directory> with the shell's time limit and simple names (below), its
#       messages into <log file>, and returns faust's own exit status.
#   faust_failure <exit status> <log file> <what was compiled>
#       Prints on one line what to say about a run that failed. <what was compiled> names the
#       file the member's project gave, for example its dsp.dsp, not the copy Faust ran on.
#
# Faust stops itself once a compile has taken longer than its time limit (its -t option). It does
# so with an alarm: the process ends on signal 14 (ALRM), the shell sees exit status 142, and
# Faust writes nothing at all. The build used to report that as "faust failed: " with nothing
# after it (Drum Forge v4 in the Pangea plugin, 12 November 2026).

# Faust's own default, written here so that the message can name it.
FAUST_TIME_LIMIT=120

# -sn, simple names (decision 78). Faust 2.88.0 names every call of a function, for its diagrams,
# by writing the call's argument out as text. An argument that holds the same parts many times is
# written out in full each time, so the text can double with every level: Drum Forge v4 took
# 374 s, almost all of it writing names, and ran past the limit. With -sn a call is named without
# its argument. The C++ is the same byte for byte (146 versions compared, 13 November 2026); only
# the names in Faust's own error messages are shorter. The browser's Faust (2.90.19) bounds the
# text itself, so -sn changes nothing there.
run_faust() {
    local dir="$1" log="$2"
    shift 2
    (cd "$dir" && faust -t "$FAUST_TIME_LIMIT" -sn "$@" 2> "$log")
}

faust_failure() {
    local status="$1" log="$2" what="$3" words version signal
    words="$( (tr '\n' ' ' < "$log" | sed -E 's/[[:space:]]+/ /g; s/^ //; s/ $//' | cut -c1-500) 2>/dev/null || true)"
    version="${FAUST_VERSION:-$( (faust --version | head -1 | sed -nE 's/.*Version ([0-9.]+).*/\1/p') 2>/dev/null || true)}"
    if [ "$status" -eq 142 ]; then
        echo "Faust${version:+ $version} stopped at its time limit of $FAUST_TIME_LIMIT seconds before it had finished compiling $what."
    elif [ "$status" -gt 128 ]; then
        signal="$(kill -l "$status" 2>/dev/null || echo "$((status - 128))")"
        echo "Faust${version:+ $version} was stopped by signal $signal before it had finished compiling $what.${words:+ It said: $words}"
    elif [ -n "$words" ]; then
        echo "faust failed: $words"
    else
        echo "Faust${version:+ $version} failed on $what with exit status $status and said nothing."
    fi
}
