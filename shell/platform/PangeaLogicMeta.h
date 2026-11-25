// Pangea native shell: a logic module's display hints (LogicParam::meta in logic_api.h,
// docs/midi-tools.md "Controls"), read the way src/lib/logic/meta.ts reads them, so the web
// panel and the plugin agree on what each control is and on its layout key.
//
//   [style:knob]  [style:menu{1/16;1/8;dotted 1/8}]  [style:bars]  [style:generate]
//   [group:Left hand]  [playhead:0]  [unit:ms]
//
// - pairs are [key:value], the key a letter then letters, digits, _ or -, the value anything
//   up to the next ]; whatever is not a pair is ignored (parseMeta)
// - the last style wins; an unknown style is a knob (styleOf, itemFor)
// - the first group wins, trimmed; an empty group is no group (logicTree)
// - a menu's labels are split at ; and trimmed, empty ones dropped (menuLabels)
// - a control's layout key is "<group>/<name>", or its name alone (src/lib/ui/keys.ts)
//
// std only, so tools/test-notes.sh can test it with a bare compiler.
#pragma once

#include <string>
#include <vector>

namespace pangea::logicmeta
{

enum class Style { knob, menu, bars, generate };

struct Hints
{
    Style style = Style::knob;
    std::vector<std::string> labels;   // a menu's labels, for min, min + 1 and so on
    std::string group;                 // empty: the control is in no group
    std::string unit;
    int playhead = -1;                 // [playhead:N], the display slot 0..7 lighting this bar; -1 for none
    std::string xy;                    // [xy:x] or [xy:y]: across or up the Felt look's XY pad; empty for neither
};

struct Pair
{
    std::string key, value;
};

namespace detail
{
    inline bool isSpace (char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }
    inline bool isLetter (char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
    inline bool isWord (char c) { return isLetter (c) || (c >= '0' && c <= '9') || c == '_'; }

    inline std::string trim (const std::string& s)
    {
        size_t a = 0, b = s.size();
        while (a < b && isSpace (s[a])) ++a;
        while (b > a && isSpace (s[b - 1])) --b;
        return s.substr (a, b - a);
    }
} // namespace detail

/** "[a:b][c:d]" into its pairs, in order. The same matches as parseMeta's regular expression. */
inline std::vector<Pair> pairs (const char* meta)
{
    using namespace detail;
    std::vector<Pair> out;
    if (meta == nullptr)
        return out;
    const std::string s (meta);
    size_t i = 0;
    while (i < s.size())
    {
        if (s[i] != '[')
        {
            ++i;
            continue;
        }
        size_t j = i + 1;
        while (j < s.size() && isSpace (s[j])) ++j;
        const size_t keyStart = j;
        if (j < s.size() && isLetter (s[j]))
        {
            ++j;
            while (j < s.size() && (isWord (s[j]) || s[j] == '-')) ++j;
        }
        const size_t keyEnd = j;
        while (j < s.size() && isSpace (s[j])) ++j;
        if (keyEnd > keyStart && j < s.size() && s[j] == ':')
        {
            const size_t valueStart = j + 1;
            const size_t close = s.find (']', valueStart);
            if (close != std::string::npos)
            {
                out.push_back ({ s.substr (keyStart, keyEnd - keyStart), trim (s.substr (valueStart, close - valueStart)) });
                i = close + 1;
                continue;
            }
        }
        ++i;
    }
    return out;
}

/** The labels of "menu{1/16;1/8;dotted 1/8}", or false for any other style (menuLabels). */
inline bool menuLabels (const std::string& style, std::vector<std::string>& labels)
{
    using namespace detail;
    const std::string s = trim (style);
    if (s.compare (0, 4, "menu") != 0)
        return false;
    size_t j = 4;
    while (j < s.size() && isSpace (s[j])) ++j;
    if (j >= s.size() || s[j] != '{' || s.back() != '}')
        return false;
    const std::string inside = s.substr (j + 1, s.size() - j - 2);
    if (inside.find ('}') != std::string::npos)
        return false;
    labels.clear();
    size_t start = 0;
    while (start <= inside.size())
    {
        size_t end = inside.find (';', start);
        if (end == std::string::npos)
            end = inside.size();
        const auto label = trim (inside.substr (start, end - start));
        if (! label.empty())
            labels.push_back (label);
        start = end + 1;
    }
    return true;
}

/** Everything the panel needs to know about one of the module's controls. */
inline Hints parse (const char* meta)
{
    Hints h;
    std::string style = "knob";
    bool haveGroup = false, haveUnit = false, havePlayhead = false, haveXy = false;
    for (const auto& p : pairs (meta))
    {
        if (p.key == "style")
            style = p.value;   // the last one wins
        else if (p.key == "group" && ! haveGroup)
        {
            haveGroup = true;
            h.group = p.value;
        }
        else if (p.key == "unit" && ! haveUnit)
        {
            haveUnit = true;
            h.unit = p.value;
        }
        else if (p.key == "playhead" && ! havePlayhead)
        {
            havePlayhead = true;
            const auto& v = p.value;
            if (v.size() == 1 && v[0] >= '0' && v[0] <= '7')
                h.playhead = v[0] - '0';
        }
        else if (p.key == "xy" && ! haveXy)
        {
            // As the site reads it (libraryControl): the first, trimmed, x or y in either case.
            haveXy = true;
            if (p.value == "x" || p.value == "X")
                h.xy = "x";
            else if (p.value == "y" || p.value == "Y")
                h.xy = "y";
        }
    }
    if (menuLabels (style, h.labels))
        h.style = Style::menu;
    else if (style == "bars")
        h.style = Style::bars;
    else if (style == "generate")
        h.style = Style::generate;
    else
        h.style = Style::knob;
    return h;
}

/** How a layout names the control: its group and its name joined by "/", as controlKey does. */
inline std::string key (const std::string& group, const std::string& name)
{
    return group.empty() ? name : group + "/" + name;
}

} // namespace pangea::logicmeta
