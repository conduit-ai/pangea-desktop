// Pangea native shell: a plugin's own meters and step lights in its web panel (decision 91).
//
// On the site a meter on a panel shows the plugin's own sound: the site finds the version's meters
// in the compiler's tree of its sound code and moves each from the value the playing plugin reports
// (decisions 76, 88 and 89). A plugin in a DAW does the same with two things made here, once, on the
// message thread, when its sound code is made ready:
//
// - The tree (MeterTree), as the compiler's JSON has it: each item's type, label, address, hints
//   ([unit:dB] and the rest, as a list of pairs), range and items. One root for each stage, in
//   order: a plugin's one, or an instrument's voice and then its effect section, as a version stores
//   them (decision 22). The editor hands it to the page, and the page finds the meters and the step
//   displays in it by the site's own rules (faustMeters, web/mapping.js; a test holds those to the
//   site's). So nothing here decides which bar graph is a meter, or what kind of meter.
// - The taps (MeterTaps): every bar graph whose value is what is heard, by the one rule the page
//   and the site share (none in an instrument's voice, which would show one voice of a chord, and
//   only the first item at an address), with the place in the running sound code where Faust writes
//   its value. A page draws a subset of them; the others cost a store a block and nothing more.
//
// The audio thread calls capture at the end of every block it runs the sound code for: it copies
// each bar graph's value, the one Faust computed for the block's last sample, into a slot of its
// own, and counts the block. That is a relaxed atomic store per bar graph and one atomic add: no
// lock, no memory made, no wait, whether a window is open or not. The editor reads the slots and the
// count on its timer while its window is open (PangeaWebEditor.h), and sends the page the values,
// or nothing where nothing is heard.
#pragma once

#include <juce_core/juce_core.h>
#include <faust/gui/UI.h>
#include <faust/gui/PathBuilder.h>

#include <atomic>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace pangea
{

/** Walks the Faust UI tree of each stage of a plugin's sound code: see the top of this file. */
class MeterTree : public UI, public PathBuilder
{
public:
    /** The tree walked next is a stage. `heard` is false for an instrument's voice, whose bar graphs
        are never tapped, and true for anything that plays as it is: an effect, or an instrument's
        effect section. The stage's addresses join those of the stages before it. */
    void beginStage (bool heard)
    {
        stageHeard = heard;
        stack.clear();
        pendingGroup.clear();
        hints.clear();
    }

    /** The roots of the stages walked, in order, as the page takes them. */
    juce::var tree() const { return juce::var (roots); }

    /** A bar graph whose value is heard, and where the running sound code writes it. */
    struct Tap
    {
        juce::String address;
        FAUSTFLOAT* zone = nullptr;
    };
    const std::vector<Tap>& taps() const noexcept { return tapped; }

    void openTabBox (const char* label) override { open ("tgroup", label); }
    void openHorizontalBox (const char* label) override { open ("hgroup", label); }
    void openVerticalBox (const char* label) override { open ("vgroup", label); }
    void closeBox() override
    {
        if (stack.empty())
            return;
        popLabel();
        auto node = std::move (stack.back());
        stack.pop_back();
        node.object->setProperty ("items", node.items);
        add (juce::var (node.object.get()));
    }

    void addButton (const char* label, FAUSTFLOAT* zone) override { control ("button", label, zone, nullptr); }
    void addCheckButton (const char* label, FAUSTFLOAT* zone) override { control ("checkbox", label, zone, nullptr); }
    void addVerticalSlider (const char* label, FAUSTFLOAT* zone, FAUSTFLOAT init, FAUSTFLOAT min, FAUSTFLOAT max, FAUSTFLOAT step) override
    {
        const FAUSTFLOAT range[] { init, min, max, step };
        control ("vslider", label, zone, range);
    }
    void addHorizontalSlider (const char* label, FAUSTFLOAT* zone, FAUSTFLOAT init, FAUSTFLOAT min, FAUSTFLOAT max, FAUSTFLOAT step) override
    {
        const FAUSTFLOAT range[] { init, min, max, step };
        control ("hslider", label, zone, range);
    }
    void addNumEntry (const char* label, FAUSTFLOAT* zone, FAUSTFLOAT init, FAUSTFLOAT min, FAUSTFLOAT max, FAUSTFLOAT step) override
    {
        const FAUSTFLOAT range[] { init, min, max, step };
        control ("nentry", label, zone, range);
    }
    void addHorizontalBargraph (const char* label, FAUSTFLOAT* zone, FAUSTFLOAT min, FAUSTFLOAT max) override { bar ("hbargraph", label, zone, min, max); }
    void addVerticalBargraph (const char* label, FAUSTFLOAT* zone, FAUSTFLOAT min, FAUSTFLOAT max) override { bar ("vbargraph", label, zone, min, max); }
    void addSoundfile (const char* label, const char*, Soundfile**) override
    {
        // It has an address, which a bar graph after it may not take.
        auto o = item ("soundfile", label, nullptr);
        add (juce::var (o.get()));
    }

    void declare (FAUSTFLOAT* zone, const char* key, const char* value) override
    {
        auto pair = std::make_pair (juce::String::fromUTF8 (key), juce::String::fromUTF8 (value));
        if (zone == nullptr)
            pendingGroup.push_back (std::move (pair));   // the group opened next
        else
            hints[zone].push_back (std::move (pair));
    }

private:
    struct Node
    {
        juce::DynamicObject::Ptr object;
        juce::Array<juce::var> items;
    };
    std::vector<Node> stack;
    juce::Array<juce::var> roots;
    std::vector<std::pair<juce::String, juce::String>> pendingGroup;
    std::map<FAUSTFLOAT*, std::vector<std::pair<juce::String, juce::String>>> hints;
    std::set<std::string> taken;   // every address an item of the stages so far has
    std::vector<Tap> tapped;
    bool stageHeard = true;

    static juce::var listOf (const std::vector<std::pair<juce::String, juce::String>>& pairs)
    {
        juce::Array<juce::var> list;
        for (const auto& [key, value] : pairs)
        {
            if (key.isEmpty())
                continue;   // no name to give it
            juce::DynamicObject::Ptr entry = new juce::DynamicObject();
            entry->setProperty (key, value);
            list.add (juce::var (entry.get()));
        }
        return list;
    }

    void add (const juce::var& v)
    {
        if (stack.empty())
            roots.add (v);
        else
            stack.back().items.add (v);
    }

    void open (const char* type, const char* label)
    {
        juce::DynamicObject::Ptr o = new juce::DynamicObject();
        o->setProperty ("type", juce::String (type));
        o->setProperty ("label", juce::String::fromUTF8 (label));
        if (! pendingGroup.empty())
            o->setProperty ("meta", listOf (pendingGroup));
        pendingGroup.clear();
        pushLabel (label);
        stack.push_back ({ o, {} });
    }

    /** An item with an address: its type, label, address and hints. True in `first` when no item
        before it had that address. */
    juce::DynamicObject::Ptr item (const char* type, const char* label, FAUSTFLOAT* zone, bool* first = nullptr)
    {
        const auto address = buildPath (label);
        const bool isFirst = taken.insert (address).second;
        if (first != nullptr)
            *first = isFirst;
        juce::DynamicObject::Ptr o = new juce::DynamicObject();
        o->setProperty ("type", juce::String (type));
        o->setProperty ("label", juce::String::fromUTF8 (label));
        o->setProperty ("address", juce::String::fromUTF8 (address.c_str()));
        if (zone != nullptr)
            if (const auto it = hints.find (zone); it != hints.end())
            {
                o->setProperty ("meta", listOf (it->second));
                hints.erase (it);
            }
        return o;
    }

    void control (const char* type, const char* label, FAUSTFLOAT* zone, const FAUSTFLOAT* range)
    {
        auto o = item (type, label, zone);
        if (range != nullptr)
        {
            o->setProperty ("init", (double) range[0]);
            o->setProperty ("min", (double) range[1]);
            o->setProperty ("max", (double) range[2]);
            o->setProperty ("step", (double) range[3]);
        }
        add (juce::var (o.get()));
    }

    void bar (const char* type, const char* label, FAUSTFLOAT* zone, FAUSTFLOAT min, FAUSTFLOAT max)
    {
        bool first = false;
        auto o = item (type, label, zone, &first);
        o->setProperty ("min", (double) min);
        o->setProperty ("max", (double) max);
        add (juce::var (o.get()));
        if (first && stageHeard && zone != nullptr)
            tapped.push_back ({ o->getProperty ("address").toString(), zone });
    }
};

/** A plugin's bar graphs as they play: see the top of this file. Made once, on the message thread,
    before the audio thread runs the sound code it points into, and never changed after. */
class MeterTaps
{
public:
    static_assert (std::atomic<float>::is_always_lock_free, "a meter's slot must never lock");
    static_assert (std::atomic<juce::uint32>::is_always_lock_free, "the block count must never lock");

    MeterTaps() = default;
    MeterTaps (const MeterTaps&) = delete;
    MeterTaps& operator= (const MeterTaps&) = delete;

    /** Takes what `walked` found. `kind` is the version's: "effect", "instrument" or "midi". */
    void take (const MeterTree& walked, const juce::String& kind)
    {
        tree = walked.tree();
        kindName = kind;
        const auto& taps = walked.taps();
        count = taps.size();
        zones.clear();
        addresses.clear();
        values.reset (count > 0 ? new std::atomic<float>[count] : nullptr);
        for (size_t i = 0; i < count; ++i)
        {
            zones.push_back (taps[i].zone);
            addresses.add (taps[i].address);
            values[i].store (0.0f);
        }
    }

    /** The audio thread, at the end of each block the sound code ran for. */
    void capture() noexcept
    {
        for (size_t i = 0; i < count; ++i)
            values[i].store ((float) *zones[i], std::memory_order_relaxed);
        blocks.fetch_add (1, std::memory_order_relaxed);
    }

    size_t size() const noexcept { return count; }
    const juce::String& address (size_t i) const { return addresses.getReference ((int) i); }
    /** The last value captured for bar graph `i`. */
    float value (size_t i) const noexcept { return values[i].load (std::memory_order_relaxed); }
    /** How many blocks have been captured: it moves while the host runs the plugin. */
    juce::uint32 blockCount() const noexcept { return blocks.load (std::memory_order_relaxed); }
    /** The sound code's tree for the page, as MeterTree makes it, and the version's kind. */
    const juce::var& soundTree() const noexcept { return tree; }
    const juce::String& kind() const noexcept { return kindName; }

private:
    juce::var tree;
    juce::String kindName;
    juce::StringArray addresses;
    std::vector<FAUSTFLOAT*> zones;
    std::unique_ptr<std::atomic<float>[]> values;
    size_t count = 0;
    std::atomic<juce::uint32> blocks { 0 };
};

} // namespace pangea
