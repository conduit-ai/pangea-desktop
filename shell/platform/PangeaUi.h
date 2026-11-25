// Pangea native shell: the plugin interface its creator designed, read from ui.json.
//
// The format is src/lib/ui/schema.ts (the source of truth); the layout arithmetic is
// src/lib/ui/layout.ts. ui.json reaches the plugin as data only: build.sh copies it next to
// the generated code and CMake compiles its bytes into a byte array (juce_add_binary_data).
// shell/check-project.sh and the desktop app refuse a ui.json that breaks the format before
// anything is built; this reader checks every field again anyway, because a plugin must
// never trust what it is handed. Whatever is wrong falls back on its own: a bad colour to the
// default colour, an unknown style to vintage, a bad layout item is skipped and a bad layout
// gives the automatic arrangement. Nothing here throws.
#pragma once

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>

#include <cmath>
#include <vector>

namespace pangea::ui
{

/** The looks a panel can take (docs/interface-styles.md). */
enum class Style { vintage, minimal, pedal, playful, retro };
/** A control's size on the panel. */
enum class Size { s, m, l };

constexpr int maxBytes = 64 * 1024;
constexpr int layoutWidth = 720;
constexpr int layoutMinHeight = 120;
constexpr int layoutMaxHeight = 1600;
constexpr int layoutGrid = 8;
constexpr int layoutMaxItems = 128;
constexpr int layoutPadding = 24;   // LAYOUT_PADDING
constexpr int layoutGap = 16;       // LAYOUT_GAP
constexpr int titleMaxLength = 60, keyMaxLength = 200, labelMaxLength = 40;

inline int knobDiameter (Size size)
{
    switch (size)
    {
        case Size::s: return 48;
        case Size::l: return 88;
        case Size::m: break;
    }
    return 64;
}

struct CellSize { int w = 0, h = 0; };

/** A control's cell: the knob, its label under it and its value under that (cellSize in layout.ts). */
inline CellSize cellSize (Size size)
{
    const int d = knobDiameter (size);
    return { juce::jmax (d + 32, 88), d + 48 };
}

/** Math.round (n / 8) * 8, for the non-negative numbers a layout holds. */
inline int snap (int n) { return (int) std::floor ((double) n / layoutGrid + 0.5) * layoutGrid; }

struct Palette
{
    juce::Colour bg, panel, accent, text;
};

/** DEFAULT_UI's palette (the Cream swatch). */
inline Palette defaultPalette()
{
    return { juce::Colour (0xffeee5d3), juce::Colour (0xffe3d8c2), juce::Colour (0xffc8843a), juce::Colour (0xff2a2620) };
}

struct Item
{
    juce::String key;
    int x = 0, y = 0;
    Size size = Size::m;
    juce::String label;   // empty: the control's own label
    bool hidden = false;
};

struct Layout
{
    int height = layoutMinHeight;
    std::vector<Item> items;
};

/** What the editor draws: every field already checked, or its fallback. */
struct Spec
{
    juce::String title;                  // empty: the plugin's name
    Palette palette = defaultPalette();
    Style style = Style::vintage;
    bool hasLayout = false;              // false: the automatic arrangement
    Layout layout;
    juce::StringArray notes;             // what fell back and why, for the render tool and the log
};

inline const char* styleName (Style s)
{
    switch (s)
    {
        case Style::minimal: return "minimal";
        case Style::pedal: return "pedal";
        case Style::playful: return "playful";
        case Style::retro: return "retro";
        case Style::vintage: break;
    }
    return "vintage";
}

namespace detail
{
    // The characters JavaScript's String.prototype.trim removes, which is what zod's .trim() does.
    inline bool isJsSpace (juce::juce_wchar c)
    {
        return c == 0x09 || c == 0x0a || c == 0x0b || c == 0x0c || c == 0x0d || c == 0x20 || c == 0xa0
            || c == 0x1680 || (c >= 0x2000 && c <= 0x200a) || c == 0x2028 || c == 0x2029 || c == 0x202f
            || c == 0x205f || c == 0x3000 || c == 0xfeff;
    }

    inline juce::String jsTrim (const juce::String& s)
    {
        auto start = s.getCharPointer();
        while (! start.isEmpty() && isJsSpace (*start))
            ++start;
        auto end = start.findTerminatingNull();
        while (end > start)
        {
            auto before = end;
            --before;
            if (! isJsSpace (*before))
                break;
            end = before;
        }
        return juce::String (start, end);
    }

    /** A string's length as zod 4 measures it for the schema's limits: in Unicode code points. */
    inline int codePoints (const juce::String& s) { return s.length(); }

    /** A whole number within [lo, hi]: an int, or a double with nothing after the point. Not a bool. */
    inline bool wholeNumber (const juce::var& v, int lo, int hi, int& out)
    {
        if (v.isBool() || ! (v.isInt() || v.isInt64() || v.isDouble()))
            return false;
        const double d = (double) v;
        if (! std::isfinite (d) || d != std::floor (d) || d < lo || d > hi)
            return false;
        out = (int) d;
        return true;
    }

    /** "#rrggbb", exactly. */
    inline bool colour (const juce::var& v, juce::Colour& out)
    {
        if (! v.isString())
            return false;
        const auto s = v.toString();
        if (s.length() != 7 || s[0] != '#' || ! s.substring (1).containsOnly ("0123456789abcdefABCDEF"))
            return false;
        out = juce::Colour ((juce::uint32) (0xff000000u | (juce::uint32) s.substring (1).getHexValue32()));
        return true;
    }

    /** Text whose length, after trimming when asked, is within [lo, hi]. */
    inline bool text (const juce::var& v, int lo, int hi, bool trim, juce::String& out)
    {
        if (! v.isString())
            return false;
        const auto s = trim ? jsTrim (v.toString()) : v.toString();
        const int n = codePoints (s);
        if (n < lo || n > hi)
            return false;
        out = s;
        return true;
    }

    inline bool style (const juce::var& v, Style& out)
    {
        if (! v.isString())
            return false;
        const auto s = v.toString();
        for (auto candidate : { Style::vintage, Style::minimal, Style::pedal, Style::playful, Style::retro })
            if (s == styleName (candidate))
            {
                out = candidate;
                return true;
            }
        return false;
    }

    inline bool size (const juce::var& v, Size& out)
    {
        if (! v.isString())
            return false;
        const auto s = v.toString();
        if (s == "s") { out = Size::s; return true; }
        if (s == "m") { out = Size::m; return true; }
        if (s == "l") { out = Size::l; return true; }
        return false;
    }

    /** A short, printable rendering of a value for a note. */
    inline juce::String shown (const juce::var& v)
    {
        auto s = juce::JSON::toString (v, juce::JSON::FormatOptions {}.withSpacing (juce::JSON::Spacing::none));
        s = s.retainCharacters (" !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~");
        return s.length() > 40 ? s.substring (0, 40) + "..." : s;
    }

    inline bool readItem (const juce::var& v, Item& item, juce::String& why)
    {
        auto* o = v.getDynamicObject();
        if (o == nullptr || v.isArray()) { why = "is not an object"; return false; }
        if (! text (o->getProperty ("key"), 1, keyMaxLength, false, item.key)) { why = "has no key of 1 to 200 characters"; return false; }
        if (! wholeNumber (o->getProperty ("x"), 0, layoutWidth, item.x)) { why = "has no whole x from 0 to 720"; return false; }
        if (! wholeNumber (o->getProperty ("y"), 0, layoutMaxHeight, item.y)) { why = "has no whole y from 0 to 1600"; return false; }
        if (! size (o->getProperty ("size"), item.size)) { why = "has no size s, m or l"; return false; }
        if (o->hasProperty ("label") && ! text (o->getProperty ("label"), 1, labelMaxLength, true, item.label))
        {
            why = "has a label that is not 1 to 40 characters";
            return false;
        }
        if (o->hasProperty ("hidden"))
        {
            const auto h = o->getProperty ("hidden");
            if (! h.isBool()) { why = "has a hidden flag that is not true or false"; return false; }
            item.hidden = (bool) h;
        }
        return true;
    }
} // namespace detail

/** Reads ui.json's bytes. Every field is checked; whatever is wrong falls back on its own. */
inline Spec parse (const void* data, size_t size)
{
    using namespace detail;
    Spec spec;
    if (data == nullptr || size == 0)
    {
        spec.notes.add ("no ui.json: the default look and the automatic arrangement");
        return spec;
    }
    if (size > (size_t) maxBytes)
    {
        spec.notes.add ("ui.json is larger than 64 KB: the default look and the automatic arrangement");
        return spec;
    }

    juce::var root;
    const auto result = juce::JSON::parse (juce::String::fromUTF8 (static_cast<const char*> (data), (int) size), root);
    auto* o = root.getDynamicObject();
    if (result.failed() || o == nullptr || root.isArray())
    {
        spec.notes.add ("ui.json is not one JSON object: the default look and the automatic arrangement");
        return spec;
    }
    if (o->getProperties().isEmpty())
    {
        spec.notes.add ("ui.json is empty: the default look and the automatic arrangement");
        return spec;
    }

    if (! text (o->getProperty ("title"), 1, titleMaxLength, true, spec.title))
    {
        spec.title = {};
        spec.notes.add ("title is missing or not 1 to 60 characters: the plugin's name instead");
    }

    const auto palette = o->getProperty ("palette");
    auto* p = palette.getDynamicObject();
    const auto defaults = defaultPalette();
    const std::pair<const char*, juce::Colour Palette::*> colours[] = {
        { "bg", &Palette::bg }, { "panel", &Palette::panel }, { "accent", &Palette::accent }, { "text", &Palette::text }
    };
    for (const auto& [name, member] : colours)
    {
        juce::Colour c;
        if (p != nullptr && ! palette.isArray() && colour (p->getProperty (name), c))
            spec.palette.*member = c;
        else
        {
            spec.palette.*member = defaults.*member;
            spec.notes.add ("palette." + juce::String (name) + " is not a colour as #rrggbb: the default colour instead");
        }
    }

    if (o->hasProperty ("style") && ! style (o->getProperty ("style"), spec.style))
    {
        spec.style = Style::vintage;
        spec.notes.add ("style " + shown (o->getProperty ("style")) + " is not one of the five looks: vintage instead");
    }

    if (! o->hasProperty ("layout"))
        return spec;

    // The layout as a whole: anything wrong here and the automatic arrangement is used.
    const auto layout = o->getProperty ("layout");
    auto* l = layout.getDynamicObject();
    int width = 0;
    juce::String whyNot;
    if (l == nullptr || layout.isArray())
        whyNot = "is not an object";
    else if (! wholeNumber (l->getProperty ("width"), layoutWidth, layoutWidth, width))
        whyNot = "width is not 720";
    else if (! wholeNumber (l->getProperty ("height"), layoutMinHeight, layoutMaxHeight, spec.layout.height))
        whyNot = "height is not a whole number from 120 to 1600";
    else if (! l->getProperty ("items").isArray())
        whyNot = "items is not a list";
    else if (l->getProperty ("items").getArray()->size() > layoutMaxItems)
        whyNot = "has more than 128 items";
    if (whyNot.isNotEmpty())
    {
        spec.layout = {};
        spec.notes.add ("layout " + whyNot + ": the automatic arrangement instead");
        return spec;
    }

    // Each item on its own: a bad one is skipped and its control is placed with the new ones.
    spec.hasLayout = true;
    const auto& items = *l->getProperty ("items").getArray();
    for (int i = 0; i < items.size(); ++i)
    {
        Item item;
        juce::String why;
        if (readItem (items[i], item, why))
            spec.layout.items.push_back (item);
        else
            spec.notes.add ("layout item " + juce::String (i) + " " + why + ": skipped");
    }
    return spec;
}

// ---- The layout arithmetic of src/lib/ui/layout.ts --------------------------------------------

/** Keeps an item inside the panel (clampItem). */
inline Item clampItem (Item item)
{
    const auto cell = cellSize (item.size);
    item.x = juce::jmax (0, juce::jmin (layoutWidth - cell.w, snap (item.x)));
    item.y = juce::jmax (0, juce::jmin (layoutMaxHeight - cell.h, snap (item.y)));
    return item;
}

/** The panel's height for these items: the lowest visible cell, plus padding (layoutHeight). */
inline int layoutHeight (const std::vector<Item>& items)
{
    int bottom = 0;
    for (const auto& item : items)
        if (! item.hidden)
            bottom = juce::jmax (bottom, item.y + cellSize (item.size).h);
    return juce::jmax (layoutMinHeight, juce::jmin (layoutMaxHeight, snap (bottom + layoutPadding)));
}

/** Places cells in rows from y0, wrapping at the panel's width (placeInRows). */
inline std::vector<Item> placeInRows (const juce::StringArray& keys, int y0, Size size = Size::m)
{
    const auto cell = cellSize (size);
    const int usable = layoutWidth - 2 * layoutPadding;
    const int perRow = juce::jmax (1, (usable + layoutGap) / (cell.w + layoutGap));
    std::vector<Item> out;
    for (int index = 0; index < keys.size(); ++index)
    {
        Item item;
        item.key = keys[index];
        item.size = size;
        item.x = layoutPadding + (index % perRow) * (cell.w + layoutGap);
        item.y = y0 + (index / perRow) * (cell.h + layoutGap);
        out.push_back (clampItem (item));
    }
    return out;
}

/**
 * The person's layout fitted to the controls this build has (carryLayout): items whose
 * control is here keep their place, size, label and hidden flag; items for controls that
 * are gone are dropped; controls the layout does not name are placed in rows below the rest.
 * `keys` are the controls the panel can show, in the compiler's order, without repeats.
 */
inline Layout fitLayout (const Layout& layout, const juce::StringArray& keys, const juce::StringArray& freshKeys)
{
    std::vector<Item> kept;
    juce::StringArray placed;
    for (const auto& item : layout.items)
        if (keys.contains (item.key) && ! placed.contains (item.key))
        {
            kept.push_back (clampItem (item));
            placed.add (item.key);
        }

    juce::StringArray fresh;
    for (const auto& key : freshKeys)
        if (! placed.contains (key) && ! fresh.contains (key))
            fresh.add (key);

    const int below = kept.empty() ? layoutPadding : layoutHeight (kept) - layoutPadding + layoutGap;
    Layout out;
    out.items = kept;
    for (const auto& item : placeInRows (fresh, snap (below)))
        if ((int) out.items.size() < layoutMaxItems)
            out.items.push_back (item);
    out.height = juce::jmin (layoutMaxHeight, juce::jmax (layout.height, layoutHeight (out.items)));
    return out;
}

} // namespace pangea::ui
