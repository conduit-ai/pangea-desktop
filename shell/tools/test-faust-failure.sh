#!/bin/bash
# What the native build says when Faust does not finish (tools/run-faust.sh). Runs build-module.sh
# and build.sh with a stand-in for faust first on the PATH, which ends as Faust ends at its time
# limit (the alarm signal: exit status 142, nothing written), or on another signal, or with an
# error and no words. For an instrument the voice compiles with the real Faust and only the effect
# section is stopped. Each build must fail at the faust stage with a message that says what
# happened and names the project's own dsp.dsp. A real compile error must still be reported in
# the compiler's words, and an instrument must still build as a module. And Faust runs with simple
# names (-sn, decision 78): a program that calls one function on its own result 26 deep, which
# Faust 2.88.0 takes about 48 s to name without -sn (Drum Forge's trouble in small), must build
# as a module in under 10 s. Needs the real faust (its version and headers) and, for build.sh,
# the tools build.sh checks for; nothing is installed and nothing is built with CMake. About ten
# seconds.
set -u
SHELL_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REAL_FAUST="$(command -v faust)" || { echo "FAIL  faust is not on the PATH"; exit 1; }
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
failures=0

# The stand-in sits in a bin folder of its own, beside a link to the real Faust's headers, which
# the scripts look for next to whatever faust they find.
mkdir -p "$tmp/faust/bin"
ln -s "$(cd "$(dirname "$REAL_FAUST")/.." && pwd)/include" "$tmp/faust/include"
cat > "$tmp/faust/bin/faust" <<EOF
#!/bin/bash
# A stand-in for faust. "faust --version", and every run its mode leaves alone, go to the real one.
case "\${FAKE_FAUST:-}:\$*" in
    *--version*) ;;
    alarm:*|effect-alarm:*mydsp_effect*) kill -ALRM \$\$ ;;   # what faust -t does when the time is up
    kill:*) kill -KILL \$\$ ;;
    silent:*) exit 1 ;;
esac
exec "$REAL_FAUST" "\$@"
EOF
chmod +x "$tmp/faust/bin/faust"

# The projects, copied so that the path in each message is known.
mkdir -p "$tmp/projects"
cp -R "$SHELL_DIR/examples/tape-drift" "$SHELL_DIR/examples/dub-keys" "$tmp/projects/"
cp -R "$SHELL_DIR/examples/tape-drift" "$tmp/projects/broken"
printf 'import("stdfaust.lib");\nprocess = _, _ : no_such_function;\n' > "$tmp/projects/broken/dsp.dsp"
# twice(twice(...(hslider)...)), 26 deep: each call's argument holds the one before twice.
cp -R "$SHELL_DIR/examples/tape-drift" "$tmp/projects/nested"
{
    printf 'import("stdfaust.lib");\ntwice(x) = x + x;\nlevel = '
    for _ in $(seq 26); do printf 'twice('; done
    printf 'hslider("Level", 0, 0, 1, 0.01)'
    for _ in $(seq 26); do printf ')'; done
    printf ' : min(1);\nprocess = *(level), *(level);\n'
} > "$tmp/projects/nested/dsp.dsp"
rm -f "$tmp/projects/nested/ui.json"

# expect <case> <mode> <script> <project> <expectation>...
# Runs the script with the stand-in in <mode> and holds its last line, the JSON summary, to each
# expectation: ok=..., stage=..., effectSection=..., seconds<N, or a piece of text the error must
# contain.
expect() {
    local name="$1" mode="$2" script="$3" project="$tmp/projects/$4"
    shift 4
    local line
    case "$script" in
        build-module.sh) line="$(FAKE_FAUST="$mode" PATH="$tmp/faust/bin:$PATH" \
            bash "$SHELL_DIR/build-module.sh" "$project" "$tmp/modules/$name/v1" 2>/dev/null | tail -1)" ;;
        build.sh) line="$(FAKE_FAUST="$mode" PATH="$tmp/faust/bin:$PATH" PANGEA_BUILD_DIR="$tmp/build-$name" \
            bash "$SHELL_DIR/build.sh" "$project" 2>/dev/null | tail -1)" ;;
    esac
    local problem
    problem="$(LINE="$line" python3 -I -c '
import json, os, sys
try:
    out = json.loads(os.environ["LINE"])
except ValueError:
    sys.exit("the last line is not JSON: " + os.environ["LINE"][:300])
err = out.get("error", "")
for want in sys.argv[1:]:
    key, eq, value = want.partition("=")
    if want.startswith("seconds<"):
        most = float(want[len("seconds<"):])
        took = float(out.get("seconds", 1e9))
        if not took < most:
            sys.exit(f"it took {took:g} s, not under {most:g} s: {err}")
    elif eq and key in ("ok", "stage", "effectSection"):
        got = str(out.get(key)).lower() if key == "ok" else out.get(key)
        if got != value:
            sys.exit(f"{key} is {got!r}, not {value!r}: {err}")
    elif want not in err:
        sys.exit(f"the error does not say {want!r}: {err!r}")
' "$@" 2>&1)"
    if [ -n "$problem" ]; then
        echo "FAIL  $name: $problem"
        failures=$((failures + 1))
    else
        echo "pass  $name: $(LINE="$line" python3 -I -c '
import json, os
o = json.loads(os.environ["LINE"])
section = ", effect section " + o["effectSection"] if "effectSection" in o else ""
took = o.get("seconds")
print(o.get("error") or f"built in {took} s{section}")
')"
    fi
}

TD="$tmp/projects/tape-drift/dsp.dsp"
DK="$tmp/projects/dub-keys/dsp.dsp"
LIMIT="stopped at its time limit of 120 seconds before it had finished compiling"

# build-module.sh, the Pangea plugin's build.
expect module-time-limit alarm build-module.sh tape-drift ok=false stage=faust "$LIMIT $TD."
expect module-killed kill build-module.sh tape-drift ok=false stage=faust "was stopped by signal KILL before it had finished compiling $TD."
expect module-silent silent build-module.sh tape-drift ok=false stage=faust "failed on $TD with exit status 1 and said nothing."
expect module-compile-error "" build-module.sh broken ok=false stage=faust "faust failed: " "no_such_function"
expect module-effect-time-limit effect-alarm build-module.sh dub-keys ok=false stage=faust "$LIMIT the effect section of $DK."
expect module-instrument-builds "" build-module.sh dub-keys ok=true effectSection=built
expect module-simple-names "" build-module.sh nested ok=true "seconds<10"

# build.sh, a plugin built for one project.
expect build-time-limit alarm build.sh tape-drift ok=false stage=faust "$LIMIT $TD."
expect build-effect-time-limit effect-alarm build.sh dub-keys ok=false stage=faust "$LIMIT the effect section of $DK."

if [ "$failures" -gt 0 ]; then
    echo "$failures failed"
    exit 1
fi
echo "all passed"
