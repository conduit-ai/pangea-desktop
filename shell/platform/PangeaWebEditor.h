// Pangea native shell: the interface drawn by the component library in a web view (a test, built only
// with -DPANGEA_WEB_EDITOR=ON, build.sh --web-editor).
//
// The page (platform/web: index.html, host.js, the library's pangea.js and pangea.css, and the fonts
// the five looks use) is compiled into the plugin as data and served to the web view from memory, so
// nothing is loaded from the network. The editor hands the page the plugin's controls, their current
// values and the project's ui.json; the page draws the plugin with Pangea.render and calls back:
//   setParam (id, value, dragging)  a control moved; a drag stays one gesture for the host
//   endGestures ()                  the pointer came up: end every open gesture
//   report (height, pageMs, error)  the drawn height (the editor takes it), when the page drew, any error
//   openSite ()                     the footer's link was pressed: open Pangea in the system's browser
//   setBypass (on)                  the header's bypass button was pressed: ask for this (see below)
// Changes from the host (automation, a preset, the host's own controls) go to the page 30 times a
// second as a "params" event holding only the values that changed.
//
// Every plugin has one bypass and Pangea supplies it (PangeaBypass.h; decisions 74 and 75): a
// parameter of the plugin's, the one its processor answers getBypassParameter with. The page is
// handed `bypass: { on }` with its controls, so it draws the header bar: the plugin's name on the
// left and the bypass button at the far right. A press calls setBypass, which moves the parameter
// as one gesture for the host, like a control. The page never switches its own button: the
// editor tells it, with a "bypass" event, whenever the parameter is not where the page was last
// told, whoever moved it (the page's own press, automation, the host's own bypass button), and
// again each time the page says it has drawn. The web view shows a blank page while the window is
// hidden, and what was sent then is lost: the page that comes back is as it was left, or drawn
// afresh from what it was first given, and says it has drawn either way (host.js).
//
// The footer reads "Built by <name> with Pangea" and is a link to Pangea. The name is the project
// owner's and the address is the site's, both from the project's plugin.json (Credit). The page is
// told the address but never chooses what opens: openSite takes nothing, and the web view itself
// never leaves the panel's page (PanelBrowser).
// Each opening is logged to ~/Library/Logs/Pangea/webview-test.log with how long it took.
//
// The Pangea plugin (PangeaHost.h) shows it too, as its "Web panel": the parameters come through a
// ParamSource, a plugin's own state or the host's slots, and are read in their own units through the
// parameter itself, so the same editor serves both.
//
// A logic module's own controls (a MIDI tool's) go to the page with the Faust controls, each with its
// hint as `style` (a menu in Faust's menu form, "bars", "generate", "knob") and a bar's playhead slot,
// and the module's display slots go to the page as "display" events, so the page lights the bar of
// the step playing now (web/mapping.js, playheadLighter).
//
// A MIDI tool's notes (docs/felt-look.md). The editor is given the processor's ring of the
// notes it sends out (notes::Heard), tells the page it will send them (`notes: true`), so that the
// Felt look draws its chord map and its notes, and on its timer, at most 30 times a second, sends
// what came as a "notes" event (`events`: [on, note, velocity, channel] fours). When the tool
// stops playing (the transport stopped, or Pangea's bypass on) it sends "quiet".
//
// The plugin's own meters and step lights (decision 91). The page is handed the tree of the sound
// code that runs (`faust: { kind, tree }`, PangeaMeters.h) and finds the meters and the step
// displays in it by the site's rules, so it draws what the builder draws for the same code. A Faust
// control that marks a step ([playhead:N]) goes to the page with its `playhead`, as a MIDI tool's bar
// does. The values go to the page as a "meters" event, `values` by address, on the same timer, and
// only when one changed: the value Faust computed for the last sample of the latest block, or null
// where nothing of the plugin is heard. Nothing is heard while Pangea's bypass is on (the sound code
// runs on unheard, as on the site), and when no block has come for a quarter of a second: the host
// has stopped calling the plugin (its own bypass button, a track it stopped running), so the last
// value would be a frozen one. A null rests the meter and darkens the steps. A value Faust made
// infinite goes as 1e30 or -1e30, which the page places as the site places an infinity, and one
// that is not a number as null: JSON carries neither.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <cmath>
#include <cstring>
#include "PangeaMeters.h"
#include "PangeaModel.h"
#include "PangeaNotes.h"
#include "PangeaParamSource.h"
#include "PangeaWebData.h"

namespace pangea
{

/** Who built the plugin and where Pangea is, for the panel's footer: "Built by <name> with Pangea",
    a link to Pangea. Read from a project's plugin.json: `owner.name` and `site`, as the site writes
    them (src/lib/projects/queries.ts, pluginJson), and nothing else of it. */
struct Credit
{
    juce::String creator;                                    // empty when plugin.json names nobody: the page says "Pangea artist"
    juce::String site { "https://pangea.audio" };            // Pangea's own address, when plugin.json gives none

    /** From the bytes of a plugin.json. Whatever is missing or malformed keeps its value above. */
    static Credit fromPluginJson (const void* data, size_t size)
    {
        Credit c;
        if (data == nullptr || size == 0)
            return c;
        const auto json = juce::JSON::parse (juce::String::fromUTF8 ((const char*) data, (int) size));
        if (! json.isObject())
            return c;
        if (const auto owner = json.getProperty ("owner", {}); owner.isObject())
            if (const auto name = owner.getProperty ("name", {}); name.isString())
                c.creator = name.toString().trim().substring (0, 200);   // the page keeps it to one line and 60 characters
        if (const auto address = json.getProperty ("site", {}); address.isString() && isWebAddress (address.toString()))
            c.site = address.toString();
        return c;
    }

    /** An http or https address made of a host, a port and a plain path: all a browser is ever handed. */
    static bool isWebAddress (const juce::String& s)
    {
        if (! (s.startsWith ("https://") || s.startsWith ("http://")) || s.length() > 200)
            return false;
        const auto rest = s.fromFirstOccurrenceOf ("://", false, false);
        return rest.isNotEmpty() && juce::CharacterFunctions::isLetterOrDigit (rest[0])
            && rest.containsOnly ("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789.-_~:/");
    }
};

/** The web view shows the panel and nothing else. Anything that would take it to another page or
    open a window (the footer's link, followed in a way the page does not catch, such as Open Link
    in the web view's own menu) is handed to `leaving` and never loaded. */
class PanelBrowser : public juce::WebBrowserComponent
{
public:
    PanelBrowser (const Options& options, std::function<void (const juce::String&)> onLeaving)
        : juce::WebBrowserComponent (options), leaving (std::move (onLeaving)) {}

    bool pageAboutToLoad (const juce::String& url) override
    {
        // The panel's own page, and the blank page JUCE shows while the editor is hidden.
        if (url.startsWith (getResourceProviderRoot()) || url == "about:blank")
            return true;
        if (leaving != nullptr)
            leaving (url);
        return false;
    }

    void newWindowAttemptingToLoad (const juce::String& url) override
    {
        if (leaving != nullptr)
            leaving (url);
    }

private:
    std::function<void (const juce::String&)> leaving;
};

class WebEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    WebEditor (juce::AudioProcessor& p, const ParamSource& s, const std::vector<Control>& controls,
               const juce::String& name, const void* uiData, size_t uiSize,
               const std::vector<LogicControl>& logic = {}, const notes::Display* displayState = nullptr,
               const Credit& builtBy = {}, const MeterTaps* meterTaps = nullptr, notes::Heard* heardNotes = nullptr)
        : AudioProcessorEditor (p), state (s), pluginName (name), credit (builtBy), display (displayState), heard (heardNotes),
          meters (meterTaps != nullptr && meterTaps->kind() != "midi" ? meterTaps : nullptr),
          bypassParam (p.getBypassParameter()), opened (juce::Time::getMillisecondCounterHiRes()),
          browser (makeOptions (controls, logic, uiData, uiSize), [this] (const juce::String& url) { leaving (url); })
    {
        if (meters != nullptr)
        {
            for (size_t i = 0; i < meters->size(); ++i)
                meterIds.emplace_back (meters->address (i));
            meterBlocks = meters->blockCount();   // only a block after this one says the plugin is heard
        }
        if (heard != nullptr)
        {
            heardFrom = heard->end();
            heard->listeners.fetch_add (1);
        }
        addAndMakeVisible (browser);
        setSize (width, 360);
        browser.goToURL (juce::WebBrowserComponent::getResourceProviderRoot());
        startTimerHz (30);
    }

    ~WebEditor() override
    {
        stopTimer();
        endGestures();
        if (heard != nullptr)
            heard->listeners.fetch_sub (1);
    }

    void resized() override { browser.setBounds (getLocalBounds()); }

    /** True once the page has drawn the plugin and said how tall it is. */
    bool hasDrawn() const noexcept { return reported; }
    /** The web view, for tools/host-check to talk to the page. */
    juce::WebBrowserComponent& getBrowser() noexcept { return browser; }
    /** Who built the plugin and Pangea's address, as the page was given them. */
    const Credit& getCredit() const noexcept { return credit; }
    /** Called with the address in place of the system's browser when the footer's link is pressed:
        for tools/host-check, which must not open a browser. */
    std::function<void (const juce::String&)> onOpenSite;

private:
    static constexpr int width = 720;   // Pangea's panel width; the library scales to it (one unit = width / 900)

    ParamSource state;
    juce::String pluginName;
    Credit credit;                             // who built it and Pangea's address, for the footer
    const notes::Display* display = nullptr;   // a logic module's display slots, for the playheads
    std::array<int, notes::Display::numSlots> shown {};   // what the page was last sent
    bool showSent = false;
    notes::Heard* heard = nullptr;   // a MIDI tool's notes as it sends them out, for the chord map and the notes
    std::uint64_t heardFrom = 0;     // where the next read of them starts
    bool heardPlaying = false;       // whether the tool played at the last look, to say "quiet" when it stops
    // The sound code's bar graphs (PangeaMeters.h), null for a MIDI tool, which has none. What the
    // page was last sent of them, the block count when the timer last saw it move, and when.
    const MeterTaps* meters = nullptr;
    std::vector<juce::Identifier> meterIds;
    std::vector<float> metersShown;
    std::vector<bool> metersShownNull;
    bool metersSent = false;
    juce::uint32 meterBlocks = 0;
    double meterBlockAt = 0.0;
    static constexpr double quietMs = 250.0;   // no block for this long: the host has stopped calling the plugin
    std::vector<std::pair<juce::String, juce::RangedAudioParameter*>> watched;
    std::map<juce::String, float> last;
    juce::StringArray active;   // parameters with an open gesture
    juce::AudioProcessorParameter* bypassParam = nullptr;   // Pangea's bypass, the plugin's own parameter
    bool bypassShown = false, bypassSent = false;           // what the page was last told, and whether it has been told
    double opened;
    bool reported = false;
    PanelBrowser browser;   // last: its options are built from the members above

    /** A logic control's hint in the form the page reads: a menu in Faust's menu form. */
    static juce::String logicStyle (const LogicControl& c)
    {
        switch (c.style)
        {
            case LogicControl::Style::bars: return "bars";
            case LogicControl::Style::generate: return "generate";
            case LogicControl::Style::menu:
            {
                if (c.labels.isEmpty())
                    return "knob";
                juce::StringArray items;
                for (int i = 0; i < c.labels.size(); ++i)
                    items.add ("'" + c.labels[i].removeCharacters ("'") + "':" + juce::String (juce::roundToInt (c.min) + i));
                return "menu{" + items.joinIntoString (";") + "}";
            }
            case LogicControl::Style::knob: break;
        }
        return "knob";
    }

    juce::WebBrowserComponent::Options makeOptions (const std::vector<Control>& controls, const std::vector<LogicControl>& logic,
                                                    const void* uiData, size_t uiSize)
    {
        juce::Array<juce::var> list;
        juce::DynamicObject::Ptr values = new juce::DynamicObject();
        for (const auto& c : controls)
        {
            auto* param = state.getParameter (c.id);
            if (! c.isParameter() || param == nullptr)
                continue;
            juce::DynamicObject::Ptr o = new juce::DynamicObject();
            juce::Array<juce::var> groups;
            for (const auto& g : c.groups)
                groups.add (g);
            o->setProperty ("id", c.id);
            o->setProperty ("label", c.label);
            o->setProperty ("groups", groups);
            o->setProperty ("unit", c.unit);
            o->setProperty ("min", c.min);
            o->setProperty ("max", c.max);
            o->setProperty ("step", c.step);
            o->setProperty ("init", c.init);
            o->setProperty ("kind", kindName (c.kind));
            o->setProperty ("logScale", c.logScale);
            o->setProperty ("style", c.style);
            if (c.playhead >= 0)
                o->setProperty ("playhead", c.playhead);   // a step of the sound code's own sequencer (decision 89)
            list.add (juce::var (o.get()));

            watched.emplace_back (c.id, param);
            last[c.id] = ParamSource::realValue (*param);
            values->setProperty (c.id, last[c.id]);
        }

        // The logic module's own controls, in its order, in a group under the plugin's name as the
        // web's tree has them (src/lib/logic/meta.ts, logicTree).
        for (const auto& c : logic)
        {
            auto* param = state.getParameter (c.id);
            if (param == nullptr)
                continue;
            juce::DynamicObject::Ptr o = new juce::DynamicObject();
            juce::Array<juce::var> groups;
            groups.add (pluginName);
            if (c.group.isNotEmpty())
                groups.add (c.group);
            const bool whole = c.style == LogicControl::Style::menu || c.style == LogicControl::Style::generate;
            o->setProperty ("id", c.id);
            o->setProperty ("label", c.name);
            o->setProperty ("groups", groups);
            o->setProperty ("unit", c.unit);
            o->setProperty ("min", c.min);
            o->setProperty ("max", c.max);
            o->setProperty ("step", c.step);
            o->setProperty ("init", c.init);
            o->setProperty ("kind", whole ? "numEntry" : "slider");
            o->setProperty ("logScale", false);
            o->setProperty ("style", logicStyle (c));
            if (c.style == LogicControl::Style::bars && c.playhead >= 0)
                o->setProperty ("playhead", c.playhead);
            if (c.xy.isNotEmpty())
                o->setProperty ("xy", c.xy);   // the XY pad's pair, as the site sends it (docs/felt-look.md)
            list.add (juce::var (o.get()));

            watched.emplace_back (c.id, param);
            last[c.id] = ParamSource::realValue (*param);
            values->setProperty (c.id, last[c.id]);
        }

        juce::DynamicObject::Ptr data = new juce::DynamicObject();
        data->setProperty ("name", pluginName);
        data->setProperty ("controls", list);
        data->setProperty ("values", juce::var (values.get()));
        data->setProperty ("ui", juce::JSON::parse (juce::String::fromUTF8 ((const char*) uiData, (int) uiSize)));
        // A MIDI tool's notes go to the page: the Felt look then draws its chord map and notes.
        if (heard != nullptr)
            data->setProperty ("notes", true);
        // The footer: who built the plugin, and Pangea's address for its link.
        data->setProperty ("creator", credit.creator);
        data->setProperty ("site", credit.site);
        // The sound code's tree, in which the page finds the meters and the step displays (decision
        // 90). Only when it has a bar graph that is heard: with none, the page would find nothing.
        if (meters != nullptr && meters->size() > 0 && meters->soundTree().isArray())
        {
            juce::DynamicObject::Ptr faust = new juce::DynamicObject();
            faust->setProperty ("kind", meters->kind());
            faust->setProperty ("tree", meters->soundTree());
            data->setProperty ("faust", juce::var (faust.get()));
        }
        // Pangea's bypass: with it the page draws its header bar, the name and the bypass button.
        if (bypassParam != nullptr)
        {
            bypassShown = isBypassed();
            juce::DynamicObject::Ptr bypass = new juce::DynamicObject();
            bypass->setProperty ("on", bypassShown);
            data->setProperty ("bypass", juce::var (bypass.get()));
        }

        using Completion = juce::WebBrowserComponent::NativeFunctionCompletion;
        auto options = juce::WebBrowserComponent::Options{}
            .withNativeIntegrationEnabled()
            .withResourceProvider ([] (const juce::String& url) { return resource (url); })
            .withInitialisationData ("pangea", juce::var (data.get()))
            .withNativeFunction ("setParam", [this] (const juce::Array<juce::var>& a, Completion done)
            {
                if (a.size() >= 3)
                    set (a[0].toString(), (float) a[1], (bool) a[2]);
                done ({});
            })
            .withNativeFunction ("endGestures", [this] (const juce::Array<juce::var>&, Completion done)
            {
                endGestures();
                done ({});
            })
            .withNativeFunction ("report", [this] (const juce::Array<juce::var>& a, Completion done)
            {
                report (a);
                done ({});
            })
            .withNativeFunction ("openSite", [this] (const juce::Array<juce::var>&, Completion done)
            {
                openSite();
                done ({});
            })
            .withNativeFunction ("setBypass", [this] (const juce::Array<juce::var>& a, Completion done)
            {
                if (a.size() >= 1 && a[0].isBool())
                    setBypass ((bool) a[0]);
                done ({});
            });
       #if JUCE_WINDOWS
        options = options.withBackend (juce::WebBrowserComponent::Options::Backend::webview2)
                         .withWinWebView2Options (juce::WebBrowserComponent::Options::WinWebView2{}
                             .withUserDataFolder (juce::File::getSpecialLocation (juce::File::tempDirectory)
                                                      .getChildFile ("Pangea WebView")));
       #endif
        return options;
    }

    static juce::String kindName (Control::Kind k)
    {
        switch (k)
        {
            case Control::Kind::checkBox: return "checkBox";
            case Control::Kind::button: return "button";
            case Control::Kind::numEntry: return "numEntry";
            case Control::Kind::slider: break;
        }
        return "slider";
    }

    /** The notes a MIDI tool sent out since the last look, and "quiet" when it stops playing. */
    void sendNotes()
    {
        if (heard == nullptr)
            return;
        juce::Array<juce::var> events;
        heardFrom = heard->read (heardFrom, [&events] (int on, int note, int velocity, int channel)
        {
            events.add (on);
            events.add (note);
            events.add (velocity);
            events.add (channel);
        });
        if (! events.isEmpty())
        {
            juce::DynamicObject::Ptr message = new juce::DynamicObject();
            message->setProperty ("events", events);
            browser.emitEventIfBrowserIsVisible ("notes", juce::var (message.get()));
        }
        const bool playing = display != nullptr && display->playing.load (std::memory_order_relaxed);
        if (heardPlaying && ! playing)
            browser.emitEventIfBrowserIsVisible ("quiet", juce::var());
        heardPlaying = playing;
    }

    // The page's files, from the data compiled into the plugin. Only the file name counts.
    static std::optional<juce::WebBrowserComponent::Resource> resource (const juce::String& url)
    {
        const auto path = url.upToFirstOccurrenceOf ("?", false, false);
        const auto file = path == "/" ? juce::String ("index.html") : path.fromLastOccurrenceOf ("/", false, false);
        for (int i = 0; i < PangeaWebData::namedResourceListSize; ++i)
        {
            if (file != PangeaWebData::originalFilenames[i])
                continue;
            int size = 0;
            const auto* bytes = PangeaWebData::getNamedResource (PangeaWebData::namedResourceList[i], size);
            if (bytes == nullptr)
                break;
            std::vector<std::byte> data ((size_t) size);
            std::memcpy (data.data(), bytes, (size_t) size);
            return juce::WebBrowserComponent::Resource { std::move (data), mimeFor (file) };
        }
        return std::nullopt;
    }

    static juce::String mimeFor (const juce::String& file)
    {
        if (file.endsWith (".html")) return "text/html";
        if (file.endsWith (".js")) return "text/javascript";
        if (file.endsWith (".css")) return "text/css";
        if (file.endsWith (".woff2")) return "font/woff2";
        return "application/octet-stream";
    }

    void set (const juce::String& id, float value, bool dragging)
    {
        auto* p = state.getParameter (id);
        if (p == nullptr)
            return;
        if (! active.contains (id))
        {
            p->beginChangeGesture();
            active.add (id);
        }
        p->setValueNotifyingHost (p->convertTo0to1 (value));
        last[id] = ParamSource::realValue (*p);   // so the timer does not send it straight back
        if (! dragging)
        {
            p->endChangeGesture();
            active.removeString (id);
        }
    }

    void endGestures()
    {
        for (const auto& id : active)
            if (auto* p = state.getParameter (id))
                p->endChangeGesture();
        active.clear();
    }

    bool isBypassed() const { return bypassParam != nullptr && bypassParam->getValue() >= 0.5f; }

    // The header's bypass button was pressed: the parameter moves, as one gesture for the host. The
    // page is not answered here. The timer tells it once the parameter has moved, as it tells it
    // of a change from anywhere else.
    void setBypass (bool on)
    {
        if (bypassParam == nullptr)
            return;
        bypassParam->beginChangeGesture();
        bypassParam->setValueNotifyingHost (on ? 1.0f : 0.0f);
        bypassParam->endChangeGesture();
    }

    // The footer's link: Pangea, in the system's browser. The page sends no address, so nothing
    // it is given (a project's ui.json, a control's name) can choose what opens.
    void openSite()
    {
        if (! Credit::isWebAddress (credit.site))
            return;
        if (onOpenSite != nullptr)
            onOpenSite (credit.site);
        else
            juce::URL (credit.site).launchInDefaultBrowser();
    }

    // The web view was asked for another page. Only the footer's own link opens anything, and it
    // opens in the system's browser.
    void leaving (const juce::String& url)
    {
        if (url == credit.site || url == credit.site + "/")
            openSite();
    }

    void timerCallback() override
    {
        sendNotes();
        juce::DynamicObject::Ptr changed;
        for (const auto& [id, param] : watched)
        {
            const float v = ParamSource::realValue (*param);
            auto& l = last[id];
            if (std::abs (v - l) <= 1.0e-6f)
                continue;
            l = v;
            if (changed == nullptr)
                changed = new juce::DynamicObject();
            changed->setProperty (id, v);
        }
        if (changed != nullptr)
            browser.emitEventIfBrowserIsVisible ("params", juce::var (changed.get()));

        // The bypass, whenever it is not where the page was last told: the page's button, the word
        // beside it and the panel's fade change only on this.
        if (bypassParam != nullptr && reported && (! bypassSent || isBypassed() != bypassShown))
        {
            bypassShown = isBypassed();
            bypassSent = true;
            juce::DynamicObject::Ptr b = new juce::DynamicObject();
            b->setProperty ("on", bypassShown);
            browser.emitEventIfBrowserIsVisible ("bypass", juce::var (b.get()));
        }

        // The display slots, when they change: the page lights each bar whose slot shows its place.
        if (display != nullptr && reported)
        {
            std::array<int, notes::Display::numSlots> now {};
            const bool playing = display->playing.load (std::memory_order_relaxed);
            for (int i = 0; i < notes::Display::numSlots; ++i)
                now[(size_t) i] = playing ? display->position (i) : -1;
            if (! showSent || now != shown)
            {
                shown = now;
                showSent = true;
                juce::Array<juce::var> slots;
                for (auto v : now)
                    slots.add (v);
                juce::DynamicObject::Ptr d = new juce::DynamicObject();
                d->setProperty ("slots", slots);
                browser.emitEventIfBrowserIsVisible ("display", juce::var (d.get()));
            }
        }

        // The meters' values (decision 91), whenever one is not what the page was last told.
        if (meters != nullptr && meters->size() > 0 && reported)
            sendMeters();
    }

    /** Whether anything of the plugin is heard now: Pangea's bypass is off, and the host has run the
        plugin within the last quarter of a second. Without a block since the window opened, no. */
    bool meterHeard (double now)
    {
        const auto blocks = meters->blockCount();
        if (blocks != meterBlocks)
        {
            meterBlocks = blocks;
            meterBlockAt = now;
        }
        return meterBlockAt > 0.0 && now - meterBlockAt <= quietMs && ! isBypassed();
    }

    void sendMeters()
    {
        const auto n = meters->size();
        const bool heard = meterHeard (juce::Time::getMillisecondCounterHiRes());
        if (metersShown.size() != n)
        {
            metersShown.assign (n, 0.0f);
            metersShownNull.assign (n, true);
            metersSent = false;
        }
        bool changed = ! metersSent;
        for (size_t i = 0; i < n; ++i)
        {
            const float v = meters->value (i);
            const bool none = ! heard || std::isnan (v);
            if (none != metersShownNull[i] || (! none && ! juce::exactlyEqual (v, metersShown[i])))
                changed = true;
            metersShownNull[i] = none;
            metersShown[i] = none ? 0.0f : v;
        }
        if (! changed)
            return;
        metersSent = true;
        juce::DynamicObject::Ptr values = new juce::DynamicObject();
        for (size_t i = 0; i < n; ++i)
        {
            const float v = metersShown[i];
            values->setProperty (meterIds[i], metersShownNull[i] ? juce::var()
                                                                 : juce::var (std::isinf (v) ? (v > 0 ? 1.0e30 : -1.0e30) : (double) v));
        }
        juce::DynamicObject::Ptr message = new juce::DynamicObject();
        message->setProperty ("values", juce::var (values.get()));
        browser.emitEventIfBrowserIsVisible ("meters", juce::var (message.get()));
    }

    void report (const juce::Array<juce::var>& a)
    {
        // The page has drawn, or has come back after the window was hidden: it is told the bypass
        // again, and the meters' values, since a new drawing's meters wait at rest to be told.
        bypassSent = false;
        metersSent = false;
        const int h = a.size() > 0 ? juce::jlimit (120, 2400, (int) a[0]) : getHeight();
        const auto error = a.size() > 2 ? a[2].toString() : juce::String();
        if (! reported || error.isNotEmpty())
        {
            reported = true;
            const auto host = juce::File::getSpecialLocation (juce::File::hostApplicationPath).getFileName();
            log (pluginName + " in " + host + ": drawn " + juce::String (juce::Time::getMillisecondCounterHiRes() - opened, 0)
                 + " ms after the editor opened (page clock " + juce::String ((double) a[1], 0) + " ms), "
                 + juce::String (width) + " x " + juce::String (h) + (error.isNotEmpty() ? ", error: " + error : juce::String()));
        }
        if (h != getHeight())
            setSize (width, h);
    }

    static void log (const juce::String& line)
    {
        auto file = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Logs/Pangea/webview-test.log");
        file.getParentDirectory().createDirectory();
        file.appendText (juce::Time::getCurrentTime().toISO8601 (true) + " " + line + "\n");
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WebEditor)
};

} // namespace pangea
