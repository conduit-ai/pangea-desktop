#!/bin/bash
# Runs check-project.sh over the examples: the good ones must pass, the bad ones must be refused,
# and, where a reason is given, refused for that reason.
set -u
SHELL_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
failures=0
expect_pass() {
    if out="$(bash "$SHELL_DIR/check-project.sh" "$SHELL_DIR/examples/$1" 2>&1)"; then echo "pass  $1: $out"
    else echo "FAIL  $1 should pass but was refused: $out"; failures=$((failures + 1)); fi
}
# expect_refusal <dir under examples, or an absolute path> [text every refusal must contain]...
expect_refusal() {
    local dir="$1" name="$1"; shift
    case "$dir" in /*) name="$(basename "$dir")" ;; *) dir="$SHELL_DIR/examples/$dir" ;; esac
    if out="$(bash "$SHELL_DIR/check-project.sh" "$dir" 2>&1)"; then
        echo "FAIL  $name should be refused but passed: $out"; failures=$((failures + 1)); return
    fi
    local want
    for want in "$@"; do
        if ! printf '%s' "$out" | grep -qF -- "$want"; then
            echo "FAIL  $name was refused, but not for \"$want\": $out"; failures=$((failures + 1)); return
        fi
    done
    echo "pass  $name refused: $(printf '%s' "$out" | tr '\n' ' ' | sed 's/check-project: refused: //g' | cut -c1-400)"
}
for good in tape-drift poly-saw dub-keys acid-line stomp-fuzz glass-chorus midi-steps two-hands checks/good-comment checks/good-ui; do expect_pass "$good"; done
for bad in bad-project checks/bad-continuation checks/bad-string checks/bad-attribute checks/bad-mdoc checks/bad-string-slash checks/bad-multiline-string checks/bad-cr checks/bad-apostrophe checks/bad-nul; do expect_refusal "$bad"; done

# ui.json: one example per kind of problem, each refused with a message per problem.
expect_refusal checks/bad-ui-json "ui.json is not valid JSON"
expect_refusal checks/bad-ui-colour "palette.accent must be a colour as #rrggbb" "palette.text must be a colour as #rrggbb"
expect_refusal checks/bad-ui-style "knobStyle must be round or arc" "style must be one of vintage, minimal, pedal, playful, retro"
expect_refusal checks/bad-ui-layout "layout.width must be 720" "layout.height must be a whole number from 120 to 1600" \
    "layout.items[0].x must be a whole number" "layout.items[0].y must be a whole number" "layout.items[0].size must be s, m or l" \
    "layout.items[1].x must be a whole number from 0 to 720"
expect_refusal checks/bad-ui-strings "title must be 1 to 60 characters" "layout.items[0].key must be 1 to 200 characters" \
    "layout.items[1].label must be 1 to 40 characters"
expect_refusal checks/bad-ui-types "title must be text" "palette must be an object" "style must be one of" "layout.items must be a list"

tmp="$(mktemp -d)"
make_case() {   # make_case <name> : a project directory with a harmless dsp.dsp; prints its path
    mkdir -p "$tmp/$1"; printf 'import("stdfaust.lib");\nprocess = _, _;\n' > "$tmp/$1/dsp.dsp"; echo "$tmp/$1"
}
good_head='"title": "T", "palette": { "bg": "#000000", "panel": "#000000", "accent": "#ffffff", "text": "#ffffff" }, "knobStyle": "round"'

# Larger than 64 KB, though otherwise valid.
d="$(make_case bad-ui-big)"
python3 -I -c 'import json, sys; json.dump({"title": "Big", "palette": {"bg": "#000000", "panel": "#000000", "accent": "#ffffff", "text": "#ffffff"}, "knobStyle": "round", "padding": "x" * 70000}, open(sys.argv[1], "w"))' "$d/ui.json"
expect_refusal "$d" "ui.json is larger than 64 KB"
# 129 items, one more than the format allows.
d="$(make_case bad-ui-items)"
python3 -I -c 'import json, sys; json.dump({"title": "Many", "palette": {"bg": "#000000", "panel": "#000000", "accent": "#ffffff", "text": "#ffffff"}, "knobStyle": "round", "layout": {"width": 720, "height": 1600, "items": [{"key": "K%d" % i, "x": 0, "y": 0, "size": "s"} for i in range(129)]}}, open(sys.argv[1], "w"))' "$d/ui.json"
expect_refusal "$d" "layout.items may hold at most 128 items"
# Exactly 128 items is fine.
d="$(make_case good-ui-128)"
python3 -I -c 'import json, sys; json.dump({"title": "Many", "palette": {"bg": "#000000", "panel": "#000000", "accent": "#ffffff", "text": "#ffffff"}, "knobStyle": "round", "layout": {"width": 720, "height": 1600, "items": [{"key": "K%d" % i, "x": 0, "y": 0, "size": "s"} for i in range(128)]}}, open(sys.argv[1], "w"))' "$d/ui.json"
if out="$(bash "$SHELL_DIR/check-project.sh" "$d" 2>&1)"; then echo "pass  good-ui-128: $out"; else echo "FAIL  good-ui-128 should pass: $out"; failures=$((failures + 1)); fi
# Lengths are code points, as zod 4 counts them: 60 emoji (120 UTF-16 units) make a 60 character title.
d="$(make_case good-ui-emoji)"
python3 -I -c 'import json, sys; json.dump({"title": "\U0001F39B" * 60, "palette": {"bg": "#000000", "panel": "#000000", "accent": "#ffffff", "text": "#ffffff"}, "knobStyle": "round"}, open(sys.argv[1], "w"))' "$d/ui.json"
if out="$(bash "$SHELL_DIR/check-project.sh" "$d" 2>&1)"; then echo "pass  good-ui-emoji: $out"; else echo "FAIL  good-ui-emoji should pass: $out"; failures=$((failures + 1)); fi
d="$(make_case bad-ui-emoji)"
python3 -I -c 'import json, sys; json.dump({"title": "\U0001F39B" * 61, "palette": {"bg": "#000000", "panel": "#000000", "accent": "#ffffff", "text": "#ffffff"}, "knobStyle": "round"}, open(sys.argv[1], "w"))' "$d/ui.json"
expect_refusal "$d" "title must be 1 to 60 characters after trimming spaces (it has 61)"
# Not an object, a JSON constant that is not JSON, a lone surrogate, bytes that are not UTF-8, a link.
d="$(make_case bad-ui-array)"; printf '[1, 2, 3]\n' > "$d/ui.json"
expect_refusal "$d" "ui.json must hold one JSON object"
d="$(make_case bad-ui-nan)"; printf '{ %s, "layout": { "width": 720, "height": NaN, "items": [] } }\n' "$good_head" > "$d/ui.json"
expect_refusal "$d" "ui.json is not valid JSON"
d="$(make_case bad-ui-surrogate)"; printf '{ %s, "style": "\\ud800" }\n' "$good_head" > "$d/ui.json"
expect_refusal "$d" "lone surrogate"
d="$(make_case bad-ui-latin1)"; printf '{ %s, "style": "\351" }\n' "$good_head" > "$d/ui.json"
expect_refusal "$d" "ui.json is not UTF-8 text"
d="$(make_case bad-ui-link)"; printf '{ %s }\n' "$good_head" > "$tmp/elsewhere.json"; ln -s "$tmp/elsewhere.json" "$d/ui.json"
expect_refusal "$d" "ui.json must be a plain file"
d="$(make_case bad-ui-missing)"; printf '{ "palette": { "bg": "#000000" } }\n' > "$d/ui.json"
expect_refusal "$d" "title is missing" "palette.panel is missing" "knobStyle is missing"

# plugin.json: a MIDI tool needs a second plugin code that differs from the first, and a logic module.
midi_case() {   # midi_case <name> <python expression changing p> : midi-steps with plugin.json edited; prints its path
    mkdir -p "$tmp/$1"; cp "$SHELL_DIR/examples/midi-steps/dsp.dsp" "$SHELL_DIR/examples/midi-steps/logic.cpp" "$tmp/$1/"
    python3 -I -c 'import json, sys; p = json.load(open(sys.argv[1])); exec(sys.argv[3]); json.dump(p, open(sys.argv[2], "w"))' \
        "$SHELL_DIR/examples/midi-steps/plugin.json" "$tmp/$1/plugin.json" "$2"
    echo "$tmp/$1"
}
d="$(midi_case midi-no-fx 'del p["fxPluginCode"]')"
expect_refusal "$d" "plugin.json: fxPluginCode must be four characters, the first upper case"
d="$(midi_case midi-same-fx 'p["fxPluginCode"] = p["pluginCode"]')"
expect_refusal "$d" "fxPluginCode must differ from pluginCode"
d="$(midi_case midi-bad-fx 'p["fxPluginCode"] = "mstx"')"
expect_refusal "$d" "fxPluginCode must be four characters, the first upper case (got 'mstx')"
d="$(midi_case midi-same-bundle 'p["fxBundleId"] = p["bundleId"]')"
expect_refusal "$d" "fxBundleId must differ from bundleId"
d="$(midi_case midi-unknown-kind 'p["kind"] = "arpeggiator"')"
expect_refusal "$d" "kind must be effect, instrument or midi"
d="$(midi_case midi-no-logic 'pass')"; rm "$d/logic.cpp"
expect_refusal "$d" "a MIDI tool is its logic module"
d="$(midi_case midi-own-fx-bundle 'p["fxBundleId"] = "com.pangea.midi-steps-fx"')"
if out="$(bash "$SHELL_DIR/check-project.sh" "$d" 2>&1)"; then echo "pass  midi-own-fx-bundle: $out"; else echo "FAIL  midi-own-fx-bundle should pass: $out"; failures=$((failures + 1)); fi
# An effect or an instrument ignores the MIDI tool fields, as before.
d="$(midi_case effect-with-fx 'p["kind"] = "effect"; p["fxPluginCode"] = p["pluginCode"]')"
if out="$(bash "$SHELL_DIR/check-project.sh" "$d" 2>&1)"; then echo "pass  effect-with-fx: $out"; else echo "FAIL  effect-with-fx should pass: $out"; failures=$((failures + 1)); fi

# The bad logic file alone must also be refused (bad-project's dsp is refused first, so test the logic on its own).
cp "$SHELL_DIR/examples/tape-drift/dsp.dsp" "$tmp/"; cp "$SHELL_DIR/examples/bad-project/logic.cpp" "$tmp/"
if out="$(bash "$SHELL_DIR/check-project.sh" "$tmp" 2>&1)"; then echo "FAIL  bad-project/logic.cpp with a good dsp should be refused but passed: $out"; failures=$((failures + 1))
else echo "pass  bad-project/logic.cpp refused: $out"; fi
rm -rf "$tmp"
if [ "$failures" -eq 0 ]; then echo "all checks behaved"; else echo "$failures failures"; exit 1; fi
