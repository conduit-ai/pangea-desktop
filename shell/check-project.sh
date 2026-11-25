#!/bin/bash
# Pangea source check. A shared project is untrusted code, so the rules are enforced here,
# where the compiler runs, as well as on the server and in the desktop app. Defence in depth:
# the desktop app builds logic modules only from the user's own projects until they are sandboxed.
#
#   shell/check-project.sh <project dir>
#
# Exit 0 when dsp.dsp (and logic.cpp and ui.json, if present) obey the rules, 1 with a reason
# otherwise. Plain bash plus awk, sed and grep from macOS, and python3 (from the Xcode Command
# Line Tools, which the build needs anyway) for ui.json.
#
# Rules for dsp.dsp
#   - no ffunction, fconstant, fvariable, foreign or soundfile
#   - import(...) only of "stdfaust.lib" or another standard library name
#   - library(...) only of a standard library name; no component(...) at all
# Rules for logic.cpp
#   - no line may end in a backslash (a continuation could split a directive or a word)
#   - no preprocessor directive except exactly   #include "logic_api.h"
#   - none of the words: asm __asm __asm__ __attribute__ extern system new delete malloc calloc
#     realloc free fopen popen socket dlopen thread std goto reinterpret_cast const_cast,
#     and no word starting with exec, pthread_ or __builtin_
#   - the words are refused whether they are in code or inside a string literal
#   - no [[ (attributes), no (* (function pointers and pointer casts)
#   - no raw string literals (R"...") and no digraphs or trigraphs (%: and ??), which could spell
#     something the check would not see
# Comments are stripped before every check, so a comment cannot trip it. The comment stripper
# understands string and character literals, so a quote cannot hide a comment opener and a
# comment cannot hide a quote. String contents are checked too, so a string cannot hide a word.
# Rules for plugin.json, when the directory has one (build.sh requires it)
#   - the fields tools/read-plugin-json.py reads, each in its format; kind is effect, instrument
#     or midi (a MIDI tool, docs/midi-tools.md)
#   - a MIDI tool also needs fxPluginCode (four characters, the first upper case, not the same
#     as pluginCode) and a logic.cpp, because the module is all a MIDI tool does
# Rules for ui.json (the interface; data, never source: build.sh compiles its bytes into the plugin)
#   - a plain file (not a link) of at most 64 KB, UTF-8, one valid JSON object
#   - the interface format of src/lib/ui/schema.ts: the types, the five styles, #rrggbb colours,
#     the string lengths, a layout 720 wide and 120 to 1600 high with at most 128 items, each at
#     whole-number positions inside the panel. tools/check-ui-json.py prints one line per problem.
set -u
SHELL_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

PROJECT_DIR="${1:-}"
if [ -z "$PROJECT_DIR" ] || [ ! -d "$PROJECT_DIR" ]; then
    echo "check-project: usage: check-project.sh <project dir>" >&2
    exit 1
fi
DSP="$PROJECT_DIR/dsp.dsp"
LOGIC="$PROJECT_DIR/logic.cpp"
[ -f "$DSP" ] || { echo "check-project: refused: $DSP is missing" >&2; exit 1; }

# Faust 2.88.0 ships these. soundfiles.lib is left out because it reads files.
STANDARD_LIBS="stdfaust.lib all.lib aanl.lib analyzers.lib basics.lib compressors.lib debug.lib delays.lib demos.lib doc.lib dx7.lib effect.lib env.lib envelopes.lib fds.lib filter.lib filters.lib hoa.lib hysteresis.lib instruments.lib interpolators.lib lfo.lib linearalgebra.lib math.lib maths.lib maxmsp.lib mi.lib misceffects.lib motion.lib music.lib noises.lib operator.lib oscillator.lib oscillators.lib phaflangers.lib physmodels.lib pitchenv.lib platform.lib quantizers.lib reducemaps.lib reverbs.lib routes.lib sf.lib signals.lib spats.lib synths.lib tonestacks.lib tubes.lib vaeffects.lib version.lib wdmodels.lib webaudio.lib"

LOGIC_WORDS="asm __asm __asm__ __attribute__ extern system new delete malloc calloc realloc free fopen popen socket dlopen thread std goto reinterpret_cast const_cast"
LOGIC_PREFIXES="exec pthread_ __builtin_"

refuse() { echo "check-project: refused: $*" >&2; exit 1; }

# strip <faust|cpp> <keep strings: 1|0> <file>
# Removes // and /* */ comments, skipping string (and, for C++, character) literals so that a
# quote inside a comment or a comment opener inside a string cannot confuse it. With keep=0 the
# contents of literals are emptied as well. Newlines are kept so line numbers stay right.
strip() {
    awk -v lang="$1" -v keep="$2" '
        { text = text $0 "\n" }
        END {
            n = length(text); i = 1; state = 0; out = ""; line = 1
            while (i <= n) {
                c = substr(text, i, 1); d = substr(text, i, 2)
                if (c == "\n" && state != 3 && state != 4) line++
                if (state == 0) {
                    if (d == "//") { state = 1; i += 2; continue }
                    if (d == "/*") { state = 2; i += 2; continue }
                    if (c == "\"") { state = 3; out = out c; i++; continue }
                    # No char literals and no digit separators in a logic module: an apostrophe
                    # could be read two ways (a C++14 digit separator or a literal), so none at all.
                    if (lang == "cpp" && c == "\x27") { printf "line %d: an apostrophe is not allowed (no char literals, no digit separators)\n", line > "/dev/stderr"; exit 1 }
                    out = out c; i++
                } else if (state == 1) {
                    if (c == "\n") { state = 0; out = out c }
                    i++
                } else if (state == 2) {
                    if (d == "*/") { state = 0; i += 2; continue }
                    if (c == "\n") out = out c
                    i++
                } else {
                    q = (state == 3) ? "\"" : "\x27"
                    # A literal that reaches the end of its line, holds a comment marker, or (in
                    # Faust) a backslash could be read two ways by two readers: refused here, in
                    # the same pass that strips, so the stripper and the checks cannot disagree.
                    if (c == "\n") { printf "line %d: a string literal must start and end on the same line\n", line > "/dev/stderr"; exit 1 }
                    if (d == "//" || d == "/*" || d == "*/") { printf "line %d: a string literal may not contain a comment marker\n", line > "/dev/stderr"; exit 1 }
                    if (c == "\\") {
                        if (lang == "faust") { printf "line %d: a string literal may not contain a backslash\n", line > "/dev/stderr"; exit 1 }
                        if (keep) out = out d; i += 2; continue
                    }
                    if (c == q) { state = 0; out = out c; i++; continue }
                    if (keep) out = out c
                    i++
                }
            }
            if (state == 3 || state == 4) { printf "line %d: a string literal never closes\n", line > "/dev/stderr"; exit 1 }
            if (state == 2) { printf "line %d: a block comment never closes\n", line > "/dev/stderr"; exit 1 }
            printf "%s", out
        }' "$3"
}

# forbidden_words <words> <prefixes> : reads code on stdin, prints "line N: word" for the first hit.
forbidden_words() {
    awk -v list="$1" -v prefixes="$2" '
        BEGIN {
            n = split(list, words, " "); for (k = 1; k <= n; k++) bad[words[k]] = 1
            np = split(prefixes, pre, " ")
        }
        {
            line = $0
            gsub(/[^A-Za-z0-9_]/, " ", line)
            m = split(line, w, " ")
            for (k = 1; k <= m; k++) {
                if (w[k] in bad) { print "line " NR ": " w[k]; exit }
                for (j = 1; j <= np; j++)
                    if (substr(w[k], 1, length(pre[j])) == pre[j]) { print "line " NR ": " w[k]; exit }
            }
        }'
}

count_word() {   # count_word <word> : occurrences of a whole word in code on stdin
    awk -v word="$1" '
        { line = $0; gsub(/[^A-Za-z0-9_]/, " ", line); m = split(line, w, " "); for (k = 1; k <= m; k++) if (w[k] == word) c++ }
        END { print c + 0 }'
}

first_line_matching() {   # first_line_matching <extended regex> : "N" of the first matching line on stdin, or nothing
    /usr/bin/grep -n -E "$1" | head -1 | cut -d: -f1
}

is_standard_lib() {
    local name
    for name in $STANDARD_LIBS; do [ "$name" = "$1" ] && return 0; done
    return 1
}

# --- plugin.json -----------------------------------------------------------------------------
PLUGIN="$PROJECT_DIR/plugin.json"
CHECKED_PLUGIN=""
if [ -e "$PLUGIN" ] || [ -L "$PLUGIN" ]; then
    { [ -f "$PLUGIN" ] && [ ! -L "$PLUGIN" ]; } || refuse "plugin.json must be a plain file, not a link or a directory"
    command -v python3 >/dev/null || refuse "plugin.json cannot be checked: python3 is missing (it comes with the Xcode Command Line Tools)"
    # The reader prints shell assignments; only the two values needed here are taken from it.
    fields="$(python3 -I "$SHELL_DIR/tools/read-plugin-json.py" "$PLUGIN")"
    plugin_error="$(printf '%s\n' "$fields" | sed -n 's/^PLUGIN_ERROR=//p')"
    [ -z "$plugin_error" ] || refuse "plugin.json: $(eval "printf '%s' $plugin_error")"
    plugin_kind="$(printf '%s\n' "$fields" | sed -n 's/^KIND=//p')"
    if [ "$plugin_kind" = "midi" ] && [ ! -f "$LOGIC" ]; then
        refuse "plugin.json says this is a MIDI tool, but there is no logic.cpp: a MIDI tool is its logic module"
    fi
    CHECKED_PLUGIN="plugin.json ($plugin_kind), "
fi

# --- dsp.dsp ---------------------------------------------------------------------------------
LEXERR="$(mktemp)"
# Only newline and tab may appear as control characters: a carriage return or anything else
# could make a line end in one reader and not another.
# Counted with tr rather than grep, so a NUL byte (which grep cannot match) is caught too.
# Bytes 0x80 and above are left alone: they are UTF-8 text in labels, never control characters.
for f in "$DSP" "$LOGIC"; do
    [ -f "$f" ] || continue
    left="$(LC_ALL=C tr -d '\t\n\040-\176\200-\377' < "$f" | wc -c | tr -d ' ')"
    [ "$left" -eq 0 ] || refuse "$(basename "$f"): control characters other than newline and tab are not allowed ($left found)"
done
DSP_CODE="$(strip faust 0 "$DSP" 2>"$LEXERR")" || refuse "dsp.dsp $(cat "$LEXERR")"
DSP_WITH_STRINGS="$(strip faust 1 "$DSP" 2>"$LEXERR")" || refuse "dsp.dsp $(cat "$LEXERR")"

# Faust's documentation sections are read by other rules in its lexer: refused on the raw text.
if grep -nE '</?(mdoc|equation|diagram|metadata|notice|listing)\b' "$DSP" >/dev/null; then
    refuse "dsp.dsp line $(grep -nE '</?(mdoc|equation|diagram|metadata|notice|listing)\b' "$DSP" | head -1 | cut -d: -f1): documentation sections (<mdoc>, <equation> and the like) are not allowed"
fi
# A string literal may hold no backslash and no comment opener, so it can only be read one way.
badstr="$(printf '%s\n' "$DSP_WITH_STRINGS" | grep -nE '"[^"]*(\\|//|/\*|\*/)[^"]*"' | head -1 | cut -d: -f1)"
[ -z "$badstr" ] || refuse "dsp.dsp line $badstr: a string literal may not contain a backslash or a comment opener"

hit="$(printf '%s\n' "$DSP_CODE" | forbidden_words "ffunction fconstant fvariable foreign soundfile" "")"
[ -z "$hit" ] || refuse "dsp.dsp $hit is not allowed (foreign code and files are off limits)"

# Every import(...), library(...) and component(...) must be the plain form with one string literal,
# and that name must be a standard library.
for keyword in import library component; do
    uses="$(printf '%s\n' "$DSP_CODE" | count_word "$keyword")"
    [ "$uses" -gt 0 ] || continue
    if [ "$keyword" = "component" ]; then
        refuse "dsp.dsp uses component(...), which reads another file"
    fi
    names="$(printf '%s\n' "$DSP_WITH_STRINGS" | awk -v kw="$keyword" '
        {
            line = $0
            while (match(line, kw "[[:space:]]*\\([[:space:]]*\"[^\"]*\"[[:space:]]*\\)")) {
                s = substr(line, RSTART, RLENGTH)
                sub(/^[^"]*"/, "", s); sub(/".*$/, "", s)
                print s
                line = substr(line, RSTART + RLENGTH)
            }
        }')"
    found="$(printf '%s\n' "$names" | sed '/^$/d' | wc -l | tr -d ' ')"
    [ "$found" -eq "$uses" ] || refuse "dsp.dsp has a $keyword(...) that is not a plain $keyword(\"name.lib\")"
    for name in $names; do
        is_standard_lib "$name" || refuse "dsp.dsp imports \"$name\", which is not a standard Faust library"
    done
done

# --- logic.cpp -------------------------------------------------------------------------------
if [ -f "$LOGIC" ]; then
    # Continuation lines first, on the raw file: a backslash before the newline could split a
    # directive or a word so that the checks below would not see it.
    n="$(first_line_matching '\\[[:space:]]*$' < "$LOGIC")"
    [ -z "$n" ] || refuse "logic.cpp line $n ends in a backslash, which is not allowed"

    LOGIC_WITH_STRINGS="$(strip cpp 1 "$LOGIC" 2>"$LEXERR")" || refuse "logic.cpp $(cat "$LEXERR")"
    LOGIC_CODE="$(strip cpp 0 "$LOGIC" 2>"$LEXERR")" || refuse "logic.cpp $(cat "$LEXERR")"

    # Directives: only the one include, spelled plainly, on its own line.
    directives="$(printf '%s\n' "$LOGIC_WITH_STRINGS" | /usr/bin/grep -n '^[[:space:]]*#' || true)"
    if [ -n "$directives" ]; then
        bad="$(printf '%s\n' "$directives" | /usr/bin/grep -v -E '^[0-9]+:[[:space:]]*#include[[:space:]]+"logic_api\.h"[[:space:]]*$' || true)"
        if [ -n "$bad" ]; then
            first="$(printf '%s\n' "$bad" | head -1)"
            refuse "logic.cpp line ${first%%:*}: the only preprocessor directive allowed is #include \"logic_api.h\" (found: $(printf '%s' "${first#*:}" | sed 's/^[[:space:]]*//'))"
        fi
    fi

    # Ways to spell a directive, a string or a token the stripper would not see.
    n="$(printf '%s\n' "$LOGIC_WITH_STRINGS" | first_line_matching '(^|[^A-Za-z0-9_])(u8|u|U|L)?R"')"
    [ -z "$n" ] || refuse "logic.cpp line $n uses a raw string literal, which is not allowed"
    n="$(printf '%s\n' "$LOGIC_WITH_STRINGS" | first_line_matching '%:|\?\?')"
    [ -z "$n" ] || refuse "logic.cpp line $n uses a digraph or trigraph (%: or ??), which is not allowed"

    # Forbidden words, in code and inside string literals alike.
    hit="$(printf '%s\n' "$LOGIC_CODE" | forbidden_words "$LOGIC_WORDS" "$LOGIC_PREFIXES")"
    [ -n "$hit" ] || hit="$(printf '%s\n' "$LOGIC_WITH_STRINGS" | forbidden_words "$LOGIC_WORDS" "$LOGIC_PREFIXES")"
    [ -z "$hit" ] || refuse "logic.cpp $hit is not allowed (no allocation, threads, files, network, casts or foreign code)"

    n="$(printf '%s\n' "$LOGIC_WITH_STRINGS" | first_line_matching '\([[:space:]]*\*')"
    [ -z "$n" ] || refuse "logic.cpp line $n uses (*, the function pointer or pointer cast syntax, which is not allowed"
    n="$(printf '%s\n' "$LOGIC_WITH_STRINGS" | first_line_matching '\[\[')"
    [ -z "$n" ] || refuse "logic.cpp line $n uses [[ (an attribute), which is not allowed"

    CHECKED="dsp.dsp, logic.cpp"
else
    CHECKED="dsp.dsp"
fi

# --- ui.json ---------------------------------------------------------------------------------
UI="$PROJECT_DIR/ui.json"
if [ -e "$UI" ] || [ -L "$UI" ]; then
    { [ -f "$UI" ] && [ ! -L "$UI" ]; } || refuse "ui.json must be a plain file, not a link or a directory"
    command -v python3 >/dev/null || refuse "ui.json cannot be checked: python3 is missing (it comes with the Xcode Command Line Tools)"
    if ! problems="$(python3 -I "$SHELL_DIR/tools/check-ui-json.py" "$UI" 2>&1)"; then
        printf '%s\n' "$problems" | sed 's/^/check-project: refused: /' >&2
        exit 1
    fi
    CHECKED="$CHECKED, ui.json"
fi

echo "check-project: ok ($CHECKED_PLUGIN$CHECKED)"
exit 0
