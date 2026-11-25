# Checks a project's ui.json against the interface format before anything is built.
#
#   python3 -I shell/tools/check-ui-json.py <ui.json>
#
# The format is projectUiSchema in src/lib/ui/schema.ts (the source of truth). The same rules
# are in desktop/src-tauri/src/safety.rs (check_ui_json) and, as field by field fallbacks, in
# shell/platform/PangeaUi.h. Prints "ok" and exits 0, or prints one plain line per problem, each
# starting with "ui.json", and exits 1. Unknown fields are allowed and ignored, as zod allows them.
import json
import math
import re
import sys

MAX_BYTES = 64 * 1024
STYLES = ["vintage", "minimal", "pedal", "playful", "retro"]
SIZES = ["s", "m", "l"]
KNOB_STYLES = ["round", "arc"]
LAYOUT_WIDTH = 720
LAYOUT_MIN_HEIGHT = 120
LAYOUT_MAX_HEIGHT = 1600
LAYOUT_MAX_ITEMS = 128
MAX_PROBLEMS = 20
COLOUR = re.compile(r"#[0-9a-fA-F]{6}")
# What JavaScript's String.prototype.trim removes, which is what zod's .trim() does.
JS_SPACE = (
    "\t\n\v\f\r \u00a0\u1680\u2000\u2001\u2002\u2003\u2004\u2005\u2006\u2007\u2008\u2009\u200a"
    "\u2028\u2029\u202f\u205f\u3000\ufeff"
)


def length(s):
    """A string's length as zod 4 measures it: in Unicode code points, not UTF-16 units."""
    return len(s)


def shown(value):
    """A short, printable rendering of a value for a message."""
    text = json.dumps(value, ensure_ascii=True)
    return text if len(text) <= 40 else text[:40] + "..."


def is_number(v):
    return isinstance(v, (int, float)) and not isinstance(v, bool)


def is_whole(v, lo, hi):
    if not is_number(v):
        return False
    if isinstance(v, float) and (not math.isfinite(v) or not v.is_integer()):
        return False
    return lo <= v <= hi


def check_text(problems, where, v, lo, hi, trim):
    if not isinstance(v, str):
        problems.append("%s must be text (got %s)" % (where, shown(v)))
        return
    n = length(v.strip(JS_SPACE) if trim else v)
    if n < lo or n > hi:
        problems.append("%s must be %d to %d characters%s (it has %d)" % (where, lo, hi, " after trimming spaces" if trim else "", n))


def check_ui(ui):
    """The problems with one parsed object, each naming the field it is about."""
    problems = []

    if "title" not in ui:
        problems.append("title is missing")
    else:
        check_text(problems, "title", ui["title"], 1, 60, True)

    palette = ui.get("palette")
    if "palette" not in ui:
        problems.append("palette is missing")
    elif not isinstance(palette, dict):
        problems.append("palette must be an object with bg, panel, accent and text (got %s)" % shown(palette))
    else:
        for name in ["bg", "panel", "accent", "text"]:
            if name not in palette:
                problems.append("palette.%s is missing" % name)
            elif not isinstance(palette[name], str) or not COLOUR.fullmatch(palette[name]):
                problems.append("palette.%s must be a colour as #rrggbb (got %s)" % (name, shown(palette[name])))

    if "knobStyle" not in ui:
        problems.append("knobStyle is missing")
    elif not isinstance(ui["knobStyle"], str) or ui["knobStyle"] not in KNOB_STYLES:
        problems.append("knobStyle must be round or arc (got %s)" % shown(ui["knobStyle"]))

    if "style" in ui and (not isinstance(ui["style"], str) or ui["style"] not in STYLES):
        problems.append("style must be one of %s (got %s)" % (", ".join(STYLES), shown(ui["style"])))

    if "layout" in ui:
        check_layout(problems, ui["layout"])
    return problems


def check_layout(problems, layout):
    if not isinstance(layout, dict):
        problems.append("layout must be an object with width, height and items (got %s)" % shown(layout))
        return
    width = layout.get("width")
    if not (is_number(width) and width == LAYOUT_WIDTH):
        problems.append("layout.width must be %d (got %s)" % (LAYOUT_WIDTH, shown(width) if "width" in layout else "nothing"))
    if not is_whole(layout.get("height"), LAYOUT_MIN_HEIGHT, LAYOUT_MAX_HEIGHT):
        problems.append("layout.height must be a whole number from %d to %d (got %s)"
                        % (LAYOUT_MIN_HEIGHT, LAYOUT_MAX_HEIGHT, shown(layout["height"]) if "height" in layout else "nothing"))
    items = layout.get("items")
    if not isinstance(items, list):
        problems.append("layout.items must be a list (got %s)" % (shown(items) if "items" in layout else "nothing"))
        return
    if len(items) > LAYOUT_MAX_ITEMS:
        problems.append("layout.items may hold at most %d items (it has %d)" % (LAYOUT_MAX_ITEMS, len(items)))
        return
    for i, item in enumerate(items):
        where = "layout.items[%d]" % i
        if not isinstance(item, dict):
            problems.append("%s must be an object (got %s)" % (where, shown(item)))
            continue
        if "key" not in item:
            problems.append("%s.key is missing" % where)
        else:
            check_text(problems, where + ".key", item["key"], 1, 200, False)
        for axis, hi in (("x", LAYOUT_WIDTH), ("y", LAYOUT_MAX_HEIGHT)):
            if not is_whole(item.get(axis), 0, hi):
                problems.append("%s.%s must be a whole number from 0 to %d (got %s)"
                                % (where, axis, hi, shown(item[axis]) if axis in item else "nothing"))
        if not isinstance(item.get("size"), str) or item.get("size") not in SIZES:
            problems.append("%s.size must be s, m or l (got %s)" % (where, shown(item["size"]) if "size" in item else "nothing"))
        if "label" in item:
            check_text(problems, where + ".label", item["label"], 1, 40, True)
        if "hidden" in item and not isinstance(item["hidden"], bool):
            problems.append("%s.hidden must be true or false (got %s)" % (where, shown(item["hidden"])))


def has_lone_surrogate(v):
    if isinstance(v, str):
        return any(0xD800 <= ord(c) <= 0xDFFF for c in v)
    if isinstance(v, list):
        return any(has_lone_surrogate(x) for x in v)
    if isinstance(v, dict):
        return any(has_lone_surrogate(k) or has_lone_surrogate(x) for k, x in v.items())
    return False


def reject_constant(name):
    raise ValueError("%s is not a JSON number" % name)


def main():
    if len(sys.argv) != 2:
        print("usage: check-ui-json.py <ui.json>")
        return 2
    try:
        with open(sys.argv[1], "rb") as f:
            data = f.read(MAX_BYTES + 1)
    except OSError as e:
        print("ui.json cannot be read: %s" % e.strerror)
        return 1
    if len(data) > MAX_BYTES:
        print("ui.json is larger than 64 KB")
        return 1
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError:
        print("ui.json is not UTF-8 text")
        return 1
    try:
        ui = json.loads(text, parse_constant=reject_constant)
    except ValueError as e:
        print("ui.json is not valid JSON: %s" % str(e).splitlines()[0][:120])
        return 1
    # A lone surrogate escape ("\ud800") is not text; serde_json refuses it, so this does too.
    if has_lone_surrogate(ui):
        print("ui.json holds an escape that is not a character (a lone surrogate)")
        return 1

    if not isinstance(ui, dict):
        print("ui.json must hold one JSON object (got %s)" % shown(ui))
        return 1

    problems = check_ui(ui)
    if not problems:
        print("ok")
        return 0
    for p in problems[:MAX_PROBLEMS]:
        print("ui.json: " + p)
    if len(problems) > MAX_PROBLEMS:
        print("ui.json: and %d more problems" % (len(problems) - MAX_PROBLEMS))
    return 1


if __name__ == "__main__":
    sys.exit(main())
