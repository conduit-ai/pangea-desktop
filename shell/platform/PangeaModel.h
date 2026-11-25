// Pangea native shell: what the shell learns from the Faust code before anything is built.
// A ControlCollector walks the Faust UI tree once and records every control (label, range,
// metadata, path, the labels of the groups above it) and the hgroup/vgroup tree that holds
// them. An instrument's effect section is a second tree, walked into the same collector
// (beginEffect). The processor uses it to build the plugin parameters; the editor uses it to lay
// out the interface and to match controls with the layout in ui.json. describeLogic does the same
// for a logic module's own controls, from their display hints (PangeaLogicMeta.h).
#pragma once

#include <juce_core/juce_core.h>
#include <faust/gui/UI.h>
#include <faust/gui/meta.h>
#include <faust/gui/PathBuilder.h>

#include "logic_api.h"
#include "PangeaLogicMeta.h"
#include "PangeaSidechain.h"

#include <map>
#include <string>
#include <vector>

namespace pangea
{

struct Control
{
    enum class Kind { slider, numEntry, checkBox, button };

    Kind kind = Kind::slider;
    juce::String id;      // the plugin parameter id, derived from the label; stable across rebuilds
    juce::String label;   // what the Faust code called it, metadata stripped
    juce::String path;    // Faust's full path, e.g. /Poly_Saw/Filter/Cutoff; empty for a control only the effect section has
    juce::String effectPath;    // its full path in an instrument's effect section, when that has it (decision 73)
    juce::StringArray groups;   // the labels of the groups above it, outermost (the root) first
    juce::String unit;    // [unit:Hz]
    float init = 0, min = 0, max = 1, step = 0;
    bool logScale = false;      // [scale:log]
    bool expScale = false;      // [scale:exp]
    bool knob = false;          // [style:knob]
    juce::String style;         // the whole [style:...] value, e.g. menu{'4:1':0;'8:1':1}
    bool logicDriven = false;   // [logic:1]: set by the logic module, not a plugin parameter
    bool voiceDriven = false;   // freq, gain, gate (or key, vel, velocity): set per voice from MIDI
    bool hidden = false;        // [hidden:1]
    bool tempoDriven = false;   // [pangea:bpm]: set from the host's tempo every block (decision 55)
    bool playDriven = false;    // [pangea:playing]: 1 while the host's transport plays, 0 while it is stopped (decision 123)
    bool beatDriven = false;    // [pangea:beat]: the host's song position in beats at the start of each block (decision 123)
    bool fractionDriven = false; // [pangea:beatfrac]: that position's fraction of a beat, exact (decision 124)
    int order = -1;             // [n] ordering prefix, if any
    int playhead = -1;          // [playhead:N], 0 to 7: the display slot that lights this step (decision 89), or -1

    /** Set by the host before every block: never a plugin parameter, never drawn. */
    bool hostDriven() const { return tempoDriven || playDriven || beatDriven || fractionDriven; }
    bool isParameter() const { return ! logicDriven && ! voiceDriven && ! hidden && ! hostDriven(); }
    bool isToggle() const { return kind == Kind::checkBox || kind == Kind::button; }

    // How a layout names this control: the labels of the groups below the root and the
    // control's own label, joined by "/". The same as controlKey() in src/lib/ui/keys.ts:
    // the root is dropped because the browser's compile and this build name it differently.
    juce::String key() const
    {
        juce::StringArray parts;
        for (int i = 1; i < groups.size(); ++i)
            parts.add (groups[i]);
        parts.add (label);
        return parts.joinIntoString ("/");
    }
};

struct Group
{
    enum class Kind { horizontal, vertical, tab };

    struct Item
    {
        bool isGroup = false;
        int index = 0;   // into Group::groups or the control list
    };

    Kind kind = Kind::vertical;
    juce::String label;
    int order = -1;
    std::vector<Group> groups;
    std::vector<Item> items;   // children in declaration order

    int countParameters (const std::vector<Control>& controls) const
    {
        int n = 0;
        for (const auto& item : items)
            n += item.isGroup ? groups[(size_t) item.index].countParameters (controls)
                              : (controls[(size_t) item.index].isParameter() ? 1 : 0);
        return n;
    }
};

// Reads the Faust metadata block (declare name, declare options "[nvoices:8]" and so on).
struct MetaCollector : public Meta
{
    std::map<std::string, std::string> values;
    void declare (const char* key, const char* value) override { values[key] = value; }

    // "[nvoices:8]" inside the options metadata, or 0 when the code declares no polyphony.
    int numVoices() const
    {
        auto it = values.find ("options");
        if (it == values.end())
            return 0;
        const auto options = juce::String (it->second);
        const auto start = options.indexOf ("[nvoices:");
        if (start < 0)
            return 0;
        const auto end = options.indexOf (start, "]");
        return end < 0 ? 0 : options.substring (start + 9, end).trim().getIntValue();
    }

    // `declare sidechain "stereo";` (2) or `declare sidechain "mono";` (1): the inputs after the
    // sound's two that are a sidechain (PangeaSidechain.h). 0 for anything else.
    int sidechainChannels() const
    {
        auto it = values.find ("sidechain");
        return it == values.end() ? 0 : sidechainChannelsFor (it->second);
    }
};

/**
 * The channels of a processor's sidechain bus (its second input bus) that this block's buffer
 * really holds: 0 while the bus is off or missing, and never past the buffer's own channels, for
 * a host that passes fewer than the layout it agreed to. At most 2. A template so this header
 * needs no more of JUCE than it has: `Processor` is a juce::AudioProcessor, `Buffer` an AudioBuffer.
 */
template <typename Processor, typename Buffer>
int sidechainChannelsPassed (const Processor& processor, const Buffer& buffer)
{
    if (processor.getBusCount (true) < 2)
        return 0;
    const auto* bus = processor.getBus (true, 1);
    if (bus == nullptr || ! bus->isEnabled())
        return 0;
    const int first = processor.getChannelIndexInProcessBlockBuffer (true, 1, 0);
    return std::max (0, std::min ({ 2, bus->getNumberOfChannels(), buffer.getNumChannels() - first }));
}

// Walks the Faust UI tree. Faust calls declare() for a zone before the add*() that creates it.
struct ControlCollector : public UI, public PathBuilder
{
    Group root;
    std::vector<Control> controls;
    std::map<FAUSTFLOAT*, std::map<std::string, std::string>> meta;
    std::map<std::string, std::string> pendingGroupMeta;   // declare (nullptr, ...) before a box opens

    ControlCollector() { root.kind = Group::Kind::vertical; stack.push_back (&root); }

    /**
     * The tree walked next is an instrument's effect section, the stage its voices share (decision
     * 73). The site stores the voice's controls followed by the effect's and names each by its
     * key, so the effect's tree joins the voice's here: its root group is the voice's root, a
     * group with the label of one already there is that group, and a control that is the same
     * definition as one the voice has (its address, kind, range and start) is that control, used
     * in both places.
     */
    void beginEffect()
    {
        inEffect = true;
        // What the voice declared is in its controls by now. The map is keyed by a zone's address,
        // and the effect's zones may be at addresses the voice's had (the Pangea plugin frees the
        // voice it reads before it makes the effect), so none of it may be left for them to find.
        meta.clear();
        pendingGroupMeta.clear();
    }

    void openTabBox (const char* label) override { open (label, Group::Kind::tab); }
    void openHorizontalBox (const char* label) override { open (label, Group::Kind::horizontal); }
    void openVerticalBox (const char* label) override { open (label, Group::Kind::vertical); }
    void closeBox() override
    {
        popLabel();
        if (! groupLabels.isEmpty())
            groupLabels.remove (groupLabels.size() - 1);
        if (stack.size() > 1)
            stack.pop_back();
    }

    void addButton (const char* l, FAUSTFLOAT* z) override { add (Control::Kind::button, l, z, 0, 0, 1, 1); }
    void addCheckButton (const char* l, FAUSTFLOAT* z) override { add (Control::Kind::checkBox, l, z, 0, 0, 1, 1); }
    void addVerticalSlider (const char* l, FAUSTFLOAT* z, FAUSTFLOAT i, FAUSTFLOAT mn, FAUSTFLOAT mx, FAUSTFLOAT s) override { add (Control::Kind::slider, l, z, i, mn, mx, s); }
    void addHorizontalSlider (const char* l, FAUSTFLOAT* z, FAUSTFLOAT i, FAUSTFLOAT mn, FAUSTFLOAT mx, FAUSTFLOAT s) override { add (Control::Kind::slider, l, z, i, mn, mx, s); }
    void addNumEntry (const char* l, FAUSTFLOAT* z, FAUSTFLOAT i, FAUSTFLOAT mn, FAUSTFLOAT mx, FAUSTFLOAT s) override { add (Control::Kind::numEntry, l, z, i, mn, mx, s); }
    void addHorizontalBargraph (const char*, FAUSTFLOAT*, FAUSTFLOAT, FAUSTFLOAT) override {}
    void addVerticalBargraph (const char*, FAUSTFLOAT*, FAUSTFLOAT, FAUSTFLOAT) override {}
    void addSoundfile (const char*, const char*, Soundfile**) override {}

    void declare (FAUSTFLOAT* zone, const char* key, const char* value) override
    {
        if (zone != nullptr)
            meta[zone][key] = value;
        else
            pendingGroupMeta[key] = value;
    }

    // The voice controls Faust's polyphony convention reserves: freq or key, gain or vel or velocity, gate.
    bool hasVoiceControls() const
    {
        bool freq = false, gate = false;
        for (const auto& c : controls)
        {
            if (c.path.endsWith ("/freq") || c.path.endsWith ("/key")) freq = true;
            if (c.path.endsWith ("/gate")) gate = true;
        }
        return freq && gate;
    }

private:
    std::vector<Group*> stack;
    juce::StringArray groupLabels;   // the labels of the open groups, as Faust reports them
    bool inEffect = false;

    void open (const char* label, Group::Kind kind)
    {
        pushLabel (label);
        auto* parent = stack.back();
        if (inEffect)
        {
            // The effect's root is the voice's root, whatever each is called; below it, by label.
            for (auto& existing : parent->groups)
                if (stack.size() == 1 || existing.label == juce::String::fromUTF8 (label))
                {
                    groupLabels.add (existing.label);
                    pendingGroupMeta.clear();
                    stack.push_back (&existing);
                    return;
                }
        }
        groupLabels.add (juce::String::fromUTF8 (label));
        Group g;
        g.kind = kind;
        g.label = juce::String::fromUTF8 (label);
        g.order = orderFrom (pendingGroupMeta);
        pendingGroupMeta.clear();
        parent->groups.push_back (g);
        parent->items.push_back ({ true, (int) parent->groups.size() - 1 });
        stack.push_back (&parent->groups.back());
    }

    /** The display slot a [playhead:N] hint names, 0 to 7, or -1: one figure, as the site reads it
        (playheadSlot in src/lib/faust/controls.ts). */
    static int playheadSlot (const std::string& raw)
    {
        const auto s = juce::String::fromUTF8 (raw.c_str()).trim();
        return s.length() == 1 && s[0] >= '0' && s[0] <= '7' ? (int) (s[0] - '0') : -1;
    }

    static int orderFrom (const std::map<std::string, std::string>& m)
    {
        for (const auto& kv : m)
            if (! kv.first.empty() && juce::String (kv.first).containsOnly ("0123456789"))
                return juce::String (kv.first).getIntValue();
        return -1;
    }

    void add (Control::Kind kind, const char* label, FAUSTFLOAT* zone, FAUSTFLOAT init, FAUSTFLOAT mn, FAUSTFLOAT mx, FAUSTFLOAT step)
    {
        Control c;
        c.kind = kind;
        c.label = juce::String::fromUTF8 (label);
        const auto fullPath = juce::String (buildPath (label));
        c.groups = groupLabels;
        c.init = init; c.min = mn; c.max = mx; c.step = step;
        if (inEffect)
        {
            // One definition that the voice and the effect both use is one control with two
            // homes: it has one address in both, as it has in the builder. Two definitions that
            // only share a key stay two controls: the same label with another range or start
            // (neither may ever be set from the other's range), or the same label under another
            // root (Faust makes a program's only group its root, and a key leaves the root out).
            const auto key = c.key();
            for (auto& other : controls)
                if (other.path == fullPath && other.effectPath.isEmpty() && other.key() == key && other.kind == kind
                    && juce::exactlyEqual (other.init, (float) init) && juce::exactlyEqual (other.min, (float) mn)
                    && juce::exactlyEqual (other.max, (float) mx) && juce::exactlyEqual (other.step, (float) step))
                {
                    other.effectPath = fullPath;
                    return;
                }
            c.effectPath = fullPath;
        }
        else
            c.path = fullPath;

        auto& m = meta[zone];
        if (m.count ("unit")) c.unit = juce::String (m["unit"]);
        c.logScale = m.count ("scale") && m["scale"] == "log";
        c.expScale = m.count ("scale") && m["scale"] == "exp";
        c.knob = m.count ("style") && juce::String (m["style"]).startsWith ("knob");
        if (m.count ("style")) c.style = juce::String::fromUTF8 (m["style"].c_str());
        c.logicDriven = m.count ("logic") && m["logic"] == "1";
        c.hidden = m.count ("hidden") && m["hidden"] == "1";
        c.tempoDriven = m.count ("pangea") && m["pangea"] == "bpm";
        c.playDriven = m.count ("pangea") && m["pangea"] == "playing";
        c.beatDriven = m.count ("pangea") && m["pangea"] == "beat";
        c.fractionDriven = m.count ("pangea") && m["pangea"] == "beatfrac";
        c.order = orderFrom (m);
        if (m.count ("playhead"))
            c.playhead = playheadSlot (m["playhead"]);
        for (const char* suffix : { "/freq", "/key", "/gain", "/vel", "/velocity", "/gate" })
            if (fullPath.endsWith (suffix))
                c.voiceDriven = true;

        c.id = makeId (c);
        controls.push_back (c);
        auto* parent = stack.back();
        parent->items.push_back ({ false, (int) controls.size() - 1 });
    }

    // Parameter ids come from the label alone (so "Cutoff" stays "cutoff" whatever group it moves to).
    // Two controls with the same label fall back to the full path.
    juce::String makeId (const Control& c) const
    {
        auto sanitise = [] (const juce::String& s)
        {
            return s.toLowerCase().retainCharacters ("abcdefghijklmnopqrstuvwxyz0123456789");
        };
        auto taken = [this] (const juce::String& candidate)
        {
            for (const auto& other : controls)
                if (other.id == candidate)
                    return true;
            return false;
        };
        auto id = sanitise (c.label);
        if (id.isEmpty())
            id = "control" + juce::String (controls.size() + 1);
        if (! taken (id))
            return id;
        id = sanitise ((c.path.isNotEmpty() ? c.path : c.effectPath).replaceCharacter ('/', '_'));
        // A control of the effect section may have the whole path of a voice control that is
        // another definition, whose id is that path already. Two parameters may never share an
        // id, so the effect's then says where it lives.
        if (inEffect && taken (id))
        {
            const auto base = id + "effect";
            id = base;
            for (int n = 2; taken (id); ++n)
                id = base + juce::String (n);
        }
        return id;
    }
};

/**
 * One of a logic module's own controls (a LogicParam), as the parameters and the editor use it.
 * Its layout key is its group and its name joined by "/", as src/lib/ui/keys.ts makes it for
 * the tree src/lib/logic/meta.ts builds, so a project's ui.json positions it like any control.
 */
struct LogicControl
{
    using Style = logicmeta::Style;

    int index = 0;
    juce::String id;            // the plugin parameter id, "logic" + index (stable: modules add at the end)
    juce::String name;          // the module's name for it
    juce::String group;         // [group:...], empty for none
    juce::String unit;          // [unit:...]
    Style style = Style::knob;
    juce::StringArray labels;   // a menu's labels, for min, min + 1, ...
    int playhead = -1;          // bars: the display slot that lights this bar, or -1
    juce::String xy;            // [xy:x] or [xy:y]: across or up the Felt look's XY pad, empty for neither
    int position = -1;          // bars: its place among the bars of its group that share the slot
    float min = 0, max = 1, init = 0, step = 0;

    juce::String key() const { return group.isEmpty() ? name : group + "/" + name; }
    /** The name a host shows: grouped controls carry their group ("Left hand Accent 1"), as both of
        Two Hands' hands have an Accent 1. The layout key stays "Left hand/Accent 1". */
    juce::String hostName() const { return group.isEmpty() ? name : group + " " + name; }

    /** A menu's label for a value, or an empty string when the value has none. */
    juce::String labelFor (float value) const
    {
        const int i = juce::roundToInt (value - min);
        return (style == Style::menu && i >= 0 && i < labels.size()) ? labels[i] : juce::String();
    }
};

/** The module's controls in declaration order, with their hints read and their bars counted. */
inline std::vector<LogicControl> describeLogic (const LogicParam* params, int count)
{
    std::vector<LogicControl> out;
    for (int i = 0; i < count; ++i)
    {
        const auto& p = params[i];
        const auto hints = logicmeta::parse (p.meta);
        LogicControl c;
        c.index = i;
        c.id = "logic" + juce::String (i);
        c.name = juce::String::fromUTF8 (p.name != nullptr ? p.name : "");
        if (c.name.isEmpty())
            c.name = "Control " + juce::String (i + 1);
        c.group = juce::String::fromUTF8 (hints.group.c_str());
        c.unit = juce::String::fromUTF8 (hints.unit.c_str());
        c.style = hints.style;
        c.xy = juce::String (hints.xy);
        for (const auto& label : hints.labels)
            c.labels.add (juce::String::fromUTF8 (label.c_str()));
        c.min = std::isfinite (p.min) ? p.min : 0.0f;
        c.max = std::isfinite (p.max) && p.max > c.min ? p.max : c.min + 1.0f;
        c.init = std::isfinite (p.init) ? juce::jlimit (c.min, c.max, p.init) : c.min;
        c.step = std::isfinite (p.step) && p.step > 0 ? p.step : 0.0f;
        if (c.style == LogicControl::Style::menu || c.style == LogicControl::Style::generate)
            c.step = 1.0f;   // whole numbers, whatever the module said
        if (c.style == LogicControl::Style::bars)
        {
            c.playhead = hints.playhead;
            int before = 0;
            for (const auto& other : out)
                if (other.style == LogicControl::Style::bars && other.group == c.group && other.playhead == c.playhead)
                    ++before;
            c.position = before;
        }
        out.push_back (c);
    }
    return out;
}

} // namespace pangea
