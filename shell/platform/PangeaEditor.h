// Pangea native shell: the plugin's interface, drawn the way its creator designed it on the web.
//
// The look and the arrangement come from the project's ui.json, compiled into the plugin as
// data (PangeaUi.h reads and checks it). The five styles are drawn from docs/interface-styles.md
// with simple shapes, as the web panel (src/components/plugin/) draws them. With a layout, each
// control sits where the person put it, at the size they chose, under the label they gave it;
// without one, the controls are arranged automatically in panels that follow the Faust groups.
// Everything is drawn in layout units (the panel is 720 wide when it has a layout) and scaled to
// the window, which the host or the corner handle can resize at a fixed shape.
//
// A logic module's own controls are proper controls too (docs/midi-tools.md, "Controls"): each is
// keyed "<group>/<name>" like a Faust control, so the layout places it, and drawn by its hint as a
// knob, a menu (rows or a drop-down), a level bar (lit by the module's playhead) or a variation
// number with Generate, back and forward.
//
// Every plugin has one bypass and Pangea supplies it (PangeaBypass.h; decisions 74 and 75). Here
// it is a plain switch at the far right of the title row, the same in all five looks, attached to
// the parameter the processor answers getBypassParameter with. While the plugin is bypassed the
// controls are drawn fainter, as the web panels draw them, and stay usable.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PangeaModel.h"
#include "PangeaNotes.h"
#include "PangeaUi.h"
#include "PangeaParamSource.h"

namespace pangea
{

/** Everything the five styles need, worked out once from the checked ui.json. */
struct Look
{
    ui::Style style = ui::Style::vintage;
    ui::Palette palette = ui::defaultPalette();
    juce::Colour fill;     // the panel's own colour
    juce::Colour ink;      // text, lines and pointers (the phosphor on a retro screen)
    juce::Colour accent;   // the accent (the phosphor on a retro screen)
    juce::Colour disc;     // the vintage knob's face, and the dot of a switch that is on
    juce::Colour stage;    // around the panel: Pangea's empty stage, or near black when the panel is as dark

    static constexpr juce::uint32 stageColour = 0xff31393b;
    static constexpr juce::uint32 darkStageColour = 0xff141718;
    static constexpr juce::uint32 screen = 0xff0b1a12;         // the retro screen, whatever bg says
    static constexpr juce::uint32 phosphorFallback = 0xff8fbf9a;

    static Look from (const ui::Spec& spec)
    {
        Look look;
        look.style = spec.style;
        look.palette = spec.palette;
        look.fill = spec.style == ui::Style::minimal ? spec.palette.panel : spec.palette.bg;
        look.ink = spec.palette.text;
        look.accent = spec.palette.accent;
        look.disc = spec.palette.bg.interpolatedWith (juce::Colours::white, 0.55f);
        if (spec.style == ui::Style::retro)
        {
            // The phosphor is the accent, unless the accent is too dark to read on the screen.
            look.fill = juce::Colour (screen);
            const auto readable = contrast (spec.palette.accent, look.fill) >= 3.0;
            look.ink = look.accent = readable ? spec.palette.accent : juce::Colour (phosphorFallback);
        }
        look.stage = contrast (look.fill, juce::Colour (stageColour)) >= 1.3 ? juce::Colour (stageColour) : juce::Colour (darkStageColour);
        return look;
    }

    /** The WCAG contrast ratio of two colours, from 1 to 21. */
    static double contrast (juce::Colour a, juce::Colour b)
    {
        auto luminance = [] (juce::Colour c)
        {
            auto channel = [] (juce::uint8 v)
            {
                const double s = v / 255.0;
                return s <= 0.03928 ? s / 12.92 : std::pow ((s + 0.055) / 1.055, 2.4);
            };
            return 0.2126 * channel (c.getRed()) + 0.7152 * channel (c.getGreen()) + 0.0722 * channel (c.getBlue());
        };
        const double la = luminance (a), lb = luminance (b);
        return (juce::jmax (la, lb) + 0.05) / (juce::jmin (la, lb) + 0.05);
    }

    bool upperCase() const { return style == ui::Style::vintage || style == ui::Style::pedal || style == ui::Style::retro; }
    bool labelAbove() const { return style == ui::Style::pedal; }
    juce::String cased (const juce::String& s) const { return upperCase() ? s.toUpperCase() : s; }

    // The web uses Fraunces and Inter; here the system's own serif (Georgia), Helvetica Neue and
    // Menlo stand in. Letter spacing is given in em, as the spec gives it.
    static juce::Font face (const juce::String& name, const juce::String& weight, float size, float em = 0.0f)
    {
        // JUCE's kerning factor is a share of the font's height, which is about 1.2 em for these faces.
        return juce::Font (juce::FontOptions (name, weight, 1.0f).withPointHeight (size).withKerningFactor (em / 1.2f));
    }
    static juce::Font sans (float size, const juce::String& weight, float em = 0.0f) { return face ("Helvetica Neue", weight, size, em); }
    static juce::Font serif (float size, float em = 0.0f) { return face ("Georgia", "Bold", size, em); }
    static juce::Font mono (float size, bool bold = false) { return face ("Menlo", bold ? "Bold" : "Regular", size); }

    juce::Font titleFont() const
    {
        switch (style)
        {
            case ui::Style::minimal: return sans (18, "Medium");
            case ui::Style::pedal: return sans (24, "Condensed Black", 0.04f);
            case ui::Style::playful: return sans (22, "Bold");
            case ui::Style::retro: return mono (18, true);
            case ui::Style::vintage: break;
        }
        return serif (22, 0.06f);
    }

    juce::Font subtitleFont() const { return mono (12); }

    juce::Font labelFont() const
    {
        switch (style)
        {
            case ui::Style::minimal: return sans (12, "Regular");
            case ui::Style::pedal: return sans (11, "Bold", 0.06f);
            case ui::Style::playful: return sans (13, "Medium");
            case ui::Style::retro: return mono (11);
            case ui::Style::vintage: break;
        }
        return sans (12, "Bold", 0.08f);
    }

    juce::Font valueFont() const { return mono (style == ui::Style::retro ? 12.0f : 11.0f); }

    /** The name of a Faust group over its panel in the automatic arrangement. */
    juce::Font groupFont() const
    {
        if (style == ui::Style::retro) return mono (11);
        return upperCase() ? sans (11, "Bold", 0.08f) : sans (12, "Medium");
    }

    juce::Colour valueColour() const
    {
        switch (style)
        {
            case ui::Style::minimal: return ink.withAlpha (0.7f);
            case ui::Style::playful: case ui::Style::retro: return ink;
            case ui::Style::vintage: case ui::Style::pedal: break;
        }
        return ink.withAlpha (0.75f);
    }

    float panelRadius() const
    {
        switch (style)
        {
            case ui::Style::minimal: return 12.0f;
            case ui::Style::pedal: return 20.0f;
            case ui::Style::playful: return 24.0f;
            case ui::Style::retro: return 10.0f;
            case ui::Style::vintage: break;
        }
        return 14.0f;
    }

    /** The title row above the controls, in layout units. */
    int headerHeight() const { return style == ui::Style::pedal ? 60 : 52; }
};

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    /** The space around a knob inside its slider, for the vintage shadow. */
    static constexpr int knobMargin = 6;

    explicit LookAndFeel (const Look& l) : look (l)
    {
        setColour (juce::Label::textColourId, look.ink);
        setColour (juce::Label::textWhenEditingColourId, look.ink);
        setColour (juce::Label::backgroundWhenEditingColourId, look.fill);
        setColour (juce::Label::outlineWhenEditingColourId, look.accent);
        setColour (juce::TextEditor::textColourId, look.ink);
        setColour (juce::TextEditor::backgroundColourId, look.fill);
        setColour (juce::TextEditor::highlightColourId, look.accent.withAlpha (0.35f));
        setColour (juce::TextEditor::highlightedTextColourId, look.ink);
        setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        setColour (juce::TextEditor::focusedOutlineColourId, look.accent);
        setColour (juce::CaretComponent::caretColourId, look.ink);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        // A menu control's list (MenuCell) opens in the panel's own colours.
        setColour (juce::PopupMenu::backgroundColourId, look.fill);
        setColour (juce::PopupMenu::textColourId, look.ink);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, look.accent);
        setColour (juce::PopupMenu::highlightedTextColourId, look.style == ui::Style::retro ? look.fill : look.palette.bg);
    }

    juce::Font getPopupMenuFont() override { return look.style == ui::Style::retro ? Look::mono (14.0f) : Look::sans (14.0f, "Regular"); }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                           float startAngle, float endAngle, juce::Slider&) override
    {
        const auto area = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced ((float) knobMargin);
        const float radius = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f;
        const auto centre = area.getCentre();
        const float angle = startAngle + juce::jlimit (0.0f, 1.0f, pos) * (endAngle - startAngle);
        switch (look.style)
        {
            case ui::Style::minimal: minimalKnob (g, centre, radius, startAngle, angle); break;
            case ui::Style::pedal: pedalKnob (g, centre, radius, angle); break;
            case ui::Style::playful: playfulKnob (g, centre, radius, startAngle, angle); break;
            case ui::Style::retro: retroKnob (g, centre, radius, startAngle, endAngle, pos); break;
            case ui::Style::vintage: vintageKnob (g, centre, radius, angle); break;
        }
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool down) override
    {
        const auto area = button.getLocalBounds().toFloat().reduced ((float) knobMargin);
        const bool on = button.getToggleState();
        switch (look.style)
        {
            case ui::Style::pedal: footswitch (g, area, on, down); break;
            case ui::Style::retro: bracketSwitch (g, area, on, highlighted || down); break;
            case ui::Style::minimal: pill (g, area, on, down, 7.0f, 1.0f); break;
            case ui::Style::playful: pill (g, area, on, down, 12.0f, 1.5f); break;
            case ui::Style::vintage: pill (g, area, on, down, 7.0f, 1.5f); break;
        }
    }

    void drawCornerResizer (juce::Graphics& g, int w, int h, bool over, bool dragging) override
    {
        g.setColour (juce::Colours::white.withAlpha (over || dragging ? 0.6f : 0.3f));
        for (float i = 0.3f; i < 1.0f; i += 0.3f)
            g.drawLine ((float) w * i, (float) h, (float) w, (float) h * i, 1.2f);
    }

private:
    const Look& look;

    static void line (juce::Graphics& g, juce::Point<float> from, juce::Point<float> to, float width)
    {
        juce::Path p;
        p.startNewSubPath (from);
        p.lineTo (to);
        g.strokePath (p, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    static void arc (juce::Graphics& g, juce::Point<float> c, float r, float from, float to, float width)
    {
        if (to <= from + 0.001f)
            return;
        juce::Path p;
        p.addCentredArc (c.x, c.y, r, r, 0.0f, from, to, true);
        g.strokePath (p, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    static juce::Rectangle<float> circle (juce::Point<float> c, float r) { return { c.x - r, c.y - r, 2 * r, 2 * r }; }

    // A disc of bg mixed towards white, a rim in text at 25% with a 2 unit shadow straight below, a pointer.
    void vintageKnob (juce::Graphics& g, juce::Point<float> c, float r, float angle) const
    {
        const auto rim = look.ink.withAlpha (0.25f);
        const auto body = circle (c, r - 0.5f);
        g.setColour (rim);
        g.fillEllipse (body.translated (0.0f, 2.0f));
        g.setColour (look.disc);
        g.fillEllipse (body);
        g.setColour (rim);
        g.drawEllipse (body, 1.0f);
        g.setColour (look.ink);
        line (g, c, c.getPointOnCircumference (r * 0.75f, angle), 3.0f);
    }

    // No body: a track circle, the value as an accent arc from the start, and a dot at the value.
    void minimalKnob (juce::Graphics& g, juce::Point<float> c, float r, float start, float angle) const
    {
        const float track = r - 4.0f;
        g.setColour (look.ink.withAlpha (0.2f));
        g.drawEllipse (circle (c, track), 2.0f);
        g.setColour (look.accent);
        arc (g, c, track, start, angle, 3.0f);
        g.fillEllipse (circle (c.getPointOnCircumference (track, angle), 4.0f));
    }

    // A near black disc with a lighter rim, a highlight across the top, a light pointer.
    void pedalKnob (juce::Graphics& g, juce::Point<float> c, float r, float angle) const
    {
        const auto body = circle (c, r - 0.5f);
        g.setColour (juce::Colour (0xff1c1c1c));
        g.fillEllipse (body);
        g.setColour (juce::Colour (0xff3a3a3a));
        g.drawEllipse (body, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        arc (g, c, r * 0.7f, -juce::MathConstants<float>::pi / 3.0f, juce::MathConstants<float>::pi / 3.0f, r * 0.22f);
        g.setColour (juce::Colour (0xfff2f2f2));
        line (g, c.getPointOnCircumference (r * 0.35f, angle), c.getPointOnCircumference (r * 0.9f, angle), 3.0f);
    }

    // A thick track ring, the value over it in the accent, an accent disc with a dot near its rim.
    void playfulKnob (juce::Graphics& g, juce::Point<float> c, float r, float start, float angle) const
    {
        const float ring = r - 3.0f;
        g.setColour (look.ink.withAlpha (0.15f));
        g.drawEllipse (circle (c, ring), 6.0f);
        g.setColour (look.accent);
        arc (g, c, ring, start, angle, 6.0f);
        const float inner = r - 10.0f;
        g.setColour (look.accent.withAlpha (0.85f));
        g.fillEllipse (circle (c, inner));
        g.setColour (look.palette.bg);
        g.fillEllipse (circle (c.getPointOnCircumference (inner - 7.0f, angle), 3.0f));
    }

    // Eleven ticks on the travel; those up to the value lit in the phosphor colour.
    void retroKnob (juce::Graphics& g, juce::Point<float> c, float r, float start, float end, float pos) const
    {
        for (int i = 0; i <= 10; ++i)
        {
            const float f = (float) i / 10.0f;
            const float a = start + f * (end - start);
            g.setColour (f <= pos + 0.0001f ? look.ink : look.ink.withAlpha (0.2f));
            line (g, c.getPointOnCircumference (r * 0.6f, a), c.getPointOnCircumference (r * 0.94f, a), 3.0f);
        }
        g.setColour (look.ink.withAlpha (0.35f));
        g.fillEllipse (circle (c, 2.0f));
    }

    // A 44 by 24 pill: outlined when off, filled with the accent when on, the dot moved right.
    void pill (juce::Graphics& g, juce::Rectangle<float> area, bool on, bool down, float radius, float outline) const
    {
        const auto shape = juce::Rectangle<float> (44.0f, 24.0f).withCentre (area.getCentre());
        if (on)
        {
            g.setColour (look.accent);
            g.fillRoundedRectangle (shape, radius);
        }
        else
        {
            g.setColour (look.ink.withAlpha (0.25f));
            g.drawRoundedRectangle (shape.reduced (outline * 0.5f), radius, outline);
        }
        const float dotX = on ? shape.getRight() - 12.0f : shape.getX() + 12.0f;
        g.setColour (on ? look.disc : look.ink.withAlpha (0.45f));
        g.fillEllipse (circle ({ dotX, shape.getCentreY() }, 8.0f));
        if (down)
        {
            g.setColour (look.ink.withAlpha (0.08f));
            g.fillRoundedRectangle (shape, radius);
        }
    }

    // A metal footswitch with an LED above it.
    void footswitch (juce::Graphics& g, juce::Rectangle<float> area, bool on, bool down) const
    {
        const auto c = area.getCentre().translated (0.0f, 7.0f);
        const auto foot = circle (c, 14.0f);
        g.setColour (juce::Colour (down ? 0xffb4b4b4 : 0xffc8c8c8));
        g.fillEllipse (foot);
        g.setColour (juce::Colour (0xff9a9a9a));
        g.drawEllipse (foot.reduced (1.0f), 2.0f);
        g.drawEllipse (circle (c, 8.0f), 1.0f);
        const juce::Point<float> led { c.x, foot.getY() - 8.0f - 3.0f };
        if (on)
        {
            g.setColour (look.accent.withAlpha (0.3f));
            g.fillEllipse (circle (led, 6.0f));
        }
        g.setColour (on ? look.accent : look.ink.withAlpha (0.2f));
        g.fillEllipse (circle (led, 3.0f));
    }

    // [ON ] or [OFF] on the screen.
    void bracketSwitch (juce::Graphics& g, juce::Rectangle<float> area, bool on, bool active) const
    {
        g.setColour (on ? look.ink : look.ink.withAlpha (active ? 0.6f : 0.4f));
        g.setFont (Look::mono (15.0f, true));
        g.drawText (on ? "[ON ]" : "[OFF]", area, juce::Justification::centred, false);
    }
};

/** Where the parts of a cell go: the control at the top, its label under it and its value under
    that, in a cell of cellSize (size). The pedal puts the label above the control instead. */
struct CellGeometry
{
    static int controlTop (const Look& look) { return look.labelAbove() ? 18 : 0; }

    static juce::Rectangle<int> control (const Look& look, ui::Size size, int width)
    {
        const int d = ui::knobDiameter (size);
        return { (width - d) / 2, controlTop (look), d, d };
    }

    static juce::Rectangle<int> name (const Look& look, ui::Size size, int width)
    {
        const int d = ui::knobDiameter (size);
        return look.labelAbove() ? juce::Rectangle<int> (0, 0, width, 16) : juce::Rectangle<int> (0, d + 4, width, 16);
    }

    static juce::Rectangle<int> value (const Look& look, ui::Size size, int width)
    {
        const int d = ui::knobDiameter (size);
        return { 0, controlTop (look) + d + (look.labelAbove() ? 6 : 22), width, 18 };
    }
};

/** One control: a knob or a switch, its label and its value, in a cell of cellSize (size). */
class ControlCell : public juce::Component
{
public:
    enum class Kind { knob, toggle, momentary };

    ControlCell (const ParamSource& state, const Look& l, const juce::String& id,
                 const juce::String& label, const juce::String& unitText, Kind kind, ui::Size s)
        : look (l), size (s), name (label), unit (unitText)
    {
        auto* parameter = state.getParameter (id);
        if (kind == Kind::knob)
        {
            slider = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox);
            slider->setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
            slider->setTitle (label);
            addAndMakeVisible (*slider);
            if (parameter != nullptr)
                sliderAttachment = std::make_unique<juce::SliderParameterAttachment> (*parameter, *slider, state.undoManager);
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
                slider->setDoubleClickReturnValue (true, ranged->convertFrom0to1 (ranged->getDefaultValue()));
            slider->onValueChange = [this] { updateValue(); };
            // Double-click the value to type one.
            value.setEditable (false, true, false);
            value.onTextChange = [this]
            {
                slider->setValue (slider->getValueFromText (value.getText()), juce::sendNotificationSync);
                updateValue();
            };
        }
        else
        {
            button = std::make_unique<juce::ToggleButton>();
            button->setTitle (label);
            addAndMakeVisible (*button);
            if (parameter != nullptr)
                buttonAttachment = std::make_unique<juce::ButtonParameterAttachment> (*parameter, *button, state.undoManager);
            if (kind == Kind::momentary)
            {
                // A Faust button is on while it is held.
                button->setClickingTogglesState (false);
                button->onStateChange = [this]
                {
                    const bool down = button->getState() == juce::Button::buttonDown;
                    if (down != pressed)
                    {
                        pressed = down;
                        button->setToggleState (down, juce::sendNotificationSync);
                    }
                    updateValue();
                };
            }
            else
                button->onStateChange = [this] { updateValue(); };
        }
        value.setJustificationType (juce::Justification::centred);
        value.setFont (look.valueFont());
        value.setColour (juce::Label::textColourId, look.valueColour());
        value.setMinimumHorizontalScale (0.7f);
        addAndMakeVisible (value);
        updateValue();
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (look.ink);
        g.setFont (look.labelFont());
        g.drawFittedText (look.cased (name), CellGeometry::name (look, size, getWidth()), juce::Justification::centred, 1, 0.7f);

        if (look.style == ui::Style::playful && value.getText().isNotEmpty())
        {
            const auto area = CellGeometry::value (look, size, getWidth()).toFloat();
            const float textWidth = juce::GlyphArrangement::getStringWidth (look.valueFont(), value.getText());
            const auto pillArea = juce::Rectangle<float> (juce::jmin (area.getWidth(), textWidth + 16.0f), 18.0f).withCentre (area.getCentre());
            g.setColour (look.ink.withAlpha (0.1f));
            g.fillRoundedRectangle (pillArea, 9.0f);
        }
    }

    void resized() override
    {
        const auto knob = CellGeometry::control (look, size, getWidth()).expanded (LookAndFeel::knobMargin);
        if (slider != nullptr) slider->setBounds (knob);
        if (button != nullptr) button->setBounds (knob);
        value.setBounds (CellGeometry::value (look, size, getWidth()).reduced (2, 0));
    }

private:
    const Look& look;
    ui::Size size;
    juce::String name, unit;
    bool pressed = false;
    std::unique_ptr<juce::Slider> slider;
    std::unique_ptr<juce::ToggleButton> button;
    std::unique_ptr<juce::SliderParameterAttachment> sliderAttachment;
    std::unique_ptr<juce::ButtonParameterAttachment> buttonAttachment;
    juce::Label value;

    void updateValue()
    {
        juce::String text;
        if (slider != nullptr)
            text = slider->getTextFromValue (slider->getValue()) + (unit.isNotEmpty() ? " " + unit : juce::String());
        else if (look.style != ui::Style::retro)   // the retro switch is its own readout
            text = look.cased (button->getToggleState() ? "On" : "Off");
        if (text != value.getText())
        {
            value.setText (text, juce::dontSendNotification);
            repaint();
        }
    }
};

inline ControlCell::Kind kindOf (const Control& c)
{
    if (c.kind == Control::Kind::button) return ControlCell::Kind::momentary;
    if (c.kind == Control::Kind::checkBox) return ControlCell::Kind::toggle;
    return ControlCell::Kind::knob;
}

// ---- The logic module's own controls: bars, menus and the variation number ---------------------

/**
 * The shared part of a logic control's cell: its parameter (through a ParameterAttachment, so the
 * host, automation and the panel stay in step), its label, and the cell's geometry. The value is
 * the number the module reads, in the control's own range.
 */
class LogicCell : public juce::Component
{
public:
    LogicCell (const ParamSource& state, const Look& l, const LogicControl& c, const juce::String& label, ui::Size s)
        : look (l), control (c), size (s), name (label)
    {
        setTitle (label);
        value = c.init;
        if (auto* parameter = state.getParameter (c.id))
            attachment = std::make_unique<juce::ParameterAttachment> (*parameter, [this] (float v)
            {
                value = v;
                repaint();
            }, state.undoManager);
    }

    /** The derived cell calls this once it is built, so the first value is drawn. */
    void start()
    {
        if (attachment != nullptr)
            attachment->sendInitialUpdate();
    }

protected:
    const Look& look;
    LogicControl control;
    ui::Size size;
    juce::String name;
    float value = 0.0f;
    std::unique_ptr<juce::ParameterAttachment> attachment;

    float legal (float v) const
    {
        v = juce::jlimit (control.min, control.max, v);
        if (control.step >= 1.0f)
            v = control.min + control.step * (float) juce::roundToInt ((v - control.min) / control.step);
        return juce::jlimit (control.min, control.max, v);
    }

    void set (float v)
    {
        v = legal (v);
        value = v;
        if (attachment != nullptr)
            attachment->setValueAsCompleteGesture (v);
        repaint();
    }

    float proportion() const { return juce::jlimit (0.0f, 1.0f, (value - control.min) / (control.max - control.min)); }

    juce::Rectangle<int> controlArea() const { return CellGeometry::control (look, size, getWidth()); }

    void paintName (juce::Graphics& g) const
    {
        g.setColour (look.ink);
        g.setFont (look.labelFont());
        g.drawFittedText (look.cased (name), CellGeometry::name (look, size, getWidth()), juce::Justification::centred, 1, 0.7f);
    }

    /** The value under the label, as a knob shows it (the playful pill included). */
    void paintValue (juce::Graphics& g, const juce::String& text) const
    {
        if (text.isEmpty())
            return;
        const auto area = CellGeometry::value (look, size, getWidth()).reduced (2, 0);
        if (look.style == ui::Style::playful)
        {
            const float textWidth = juce::GlyphArrangement::getStringWidth (look.valueFont(), text);
            const auto pillArea = juce::Rectangle<float> (juce::jmin ((float) area.getWidth(), textWidth + 16.0f), 18.0f)
                                      .withCentre (area.toFloat().getCentre());
            g.setColour (look.ink.withAlpha (0.1f));
            g.fillRoundedRectangle (pillArea, 9.0f);
        }
        g.setColour (look.valueColour());
        g.setFont (look.valueFont());
        g.drawFittedText (text, area, juce::Justification::centred, 1, 0.7f);
    }

    /** The colour that reads on the accent: the screen on a retro panel, otherwise the palette's
        background or its text, whichever stands out more from the accent. */
    juce::Colour onAccent() const
    {
        if (look.style == ui::Style::retro)
            return look.fill;
        const auto bg = look.palette.bg, text = look.palette.text;
        return Look::contrast (bg, look.accent) >= Look::contrast (text, look.accent) ? bg : text;
    }
};

/**
 * [style:bars]: a vertical level bar, dragged up and down (a click sets it where it lands,
 * a double-click puts it back). A bar with [playhead:N] is lit while the module's display slot N
 * shows its place in its group, as Pattern Player lights the accent playing now.
 */
class BarCell : public LogicCell
{
public:
    BarCell (const ParamSource& state, const Look& l, const LogicControl& c, const juce::String& label, ui::Size s)
        : LogicCell (state, l, c, label, s)
    {
        start();
    }

    int slot() const { return control.playhead; }
    int position() const { return control.position; }
    bool isLit() const { return lit; }

    void setLit (bool shouldBeLit)
    {
        if (shouldBeLit != lit)
        {
            lit = shouldBeLit;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        paintName (g);
        const auto bar = barArea();
        const float p = proportion();
        const auto inner = bar.reduced (3.0f);   // the well the vintage and pedal levels sit in
        if (lit)
        {
            g.setColour (look.accent);
            g.drawRoundedRectangle (bar.expanded (4.0f), radius() + 4.0f, 2.0f);   // the 2 unit ring, 3 units out
        }
        switch (look.style)
        {
            case ui::Style::vintage:
            {
                const auto rim = look.ink.withAlpha (0.25f);
                g.setColour (rim);
                g.fillRoundedRectangle (bar.translated (0.0f, 2.0f), radius());
                g.setColour (look.disc);
                g.fillRoundedRectangle (bar, radius());
                g.setColour (lit ? look.accent : look.ink.withAlpha (0.75f));
                fillLevel (g, inner, radius() - 2.0f, p);
                g.setColour (rim);
                g.drawRoundedRectangle (bar, radius(), 1.0f);
                break;
            }
            case ui::Style::minimal:
                g.setColour (look.ink.withAlpha (0.12f));
                g.fillRoundedRectangle (bar, radius());
                g.setColour (lit ? look.accent : look.accent.withAlpha (0.45f));
                fillLevel (g, bar, radius(), p);
                break;
            case ui::Style::pedal:
                g.setColour (juce::Colour (0xff1c1c1c));
                g.fillRoundedRectangle (bar, radius());
                g.setColour (juce::Colour (0xff3a3a3a));
                g.drawRoundedRectangle (bar, radius(), 1.0f);
                g.setColour (lit ? look.accent : juce::Colour (0xfff2f2f2).withAlpha (0.8f));
                fillLevel (g, inner, radius() - 2.0f, p);
                break;
            case ui::Style::playful:
                g.setColour (look.ink.withAlpha (0.15f));
                g.fillRoundedRectangle (bar, radius());
                g.setColour (lit ? look.accent : look.accent.withAlpha (0.55f));
                fillLevel (g, bar, radius(), p);
                break;
            case ui::Style::retro:
            {
                // Segments on the screen, lit up to the value; the playhead bar at full brightness.
                const int segments = juce::jmax (6, (int) bar.getHeight() / 6);
                const float h = bar.getHeight() / (float) segments;
                const int litSegments = juce::roundToInt (p * (float) segments);
                for (int i = 0; i < segments; ++i)
                {
                    const auto seg = juce::Rectangle<float> (bar.getX(), bar.getBottom() - (float) (i + 1) * h, bar.getWidth(), h).reduced (0.0f, 1.0f);
                    g.setColour (i < litSegments ? look.ink.withAlpha (lit ? 1.0f : 0.7f) : look.ink.withAlpha (0.2f));
                    g.fillRect (seg);
                }
                break;
            }
        }
        paintValue (g, juce::String (juce::roundToInt (value)));
    }

    // The level: the bar's own shape cut off at the value, so a low value still reads as a level.
    static void fillLevel (juce::Graphics& g, juce::Rectangle<float> shape, float r, float p)
    {
        juce::Graphics::ScopedSaveState saved (g);
        juce::Path level;
        level.addRectangle (shape.withTrimmedTop (shape.getHeight() * (1.0f - p)));
        g.reduceClipRegion (level);
        g.fillRoundedRectangle (shape, juce::jmax (0.0f, r));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (attachment == nullptr || e.mods.isPopupMenu())
            return;
        dragging = true;
        attachment->beginGesture();
        setFrom (e);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging)
            setFrom (e);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragging && attachment != nullptr)
            attachment->endGesture();
        dragging = false;
    }

    void mouseDoubleClick (const juce::MouseEvent&) override { set (control.init); }

private:
    bool lit = false, dragging = false;

    // Narrower than a knob, and inset at the top so the playhead's ring stays inside the cell.
    juce::Rectangle<float> barArea() const
    {
        const auto area = controlArea().toFloat().withTrimmedTop (6.0f).withTrimmedBottom (2.0f);
        return area.withSizeKeepingCentre ((float) juce::roundToInt (area.getWidth() * 0.44f), area.getHeight());
    }

    float radius() const
    {
        switch (look.style)
        {
            case ui::Style::playful: return barArea().getWidth() * 0.5f;
            case ui::Style::retro: return 0.0f;
            case ui::Style::minimal: return 6.0f;
            case ui::Style::vintage: case ui::Style::pedal: break;
        }
        return 5.0f;
    }

    void setFrom (const juce::MouseEvent& e)
    {
        const auto bar = barArea();
        const float p = juce::jlimit (0.0f, 1.0f, (bar.getBottom() - e.position.y) / bar.getHeight());
        const float v = legal (control.min + p * (control.max - control.min));
        value = v;
        attachment->setValueAsPartOfGesture (v);
        repaint();
    }
};

/**
 * [style:menu{a;b;c}]: the labels as a segmented control, one row each, when they fit the cell;
 * otherwise a drop-down that opens a list. The value is min for the first label, min + 1 for the
 * next and so on.
 */
class MenuCell : public LogicCell
{
public:
    MenuCell (const ParamSource& state, const Look& l, const LogicControl& c, const juce::String& label, ui::Size s)
        : LogicCell (state, l, c, label, s)
    {
        start();
    }

    bool isSegmented() const { return segmented; }

    // Rows or a drop-down: decided once the cell knows its width.
    void resized() override { segmented = fitsAsRows(); }

    void paint (juce::Graphics& g) override
    {
        paintName (g);
        const int selected = juce::roundToInt (value - control.min);
        if (segmented)
        {
            const auto rows = rowAreas();
            paintFrame (g, rows.front().getUnion (rows.back()));
            for (int i = 0; i < (int) rows.size(); ++i)
                paintRow (g, rows[(size_t) i], control.labels[i], i == selected);
            return;
        }
        const auto box = boxArea();
        paintFrame (g, box);
        const auto text = control.labelFor (value).isNotEmpty() ? control.labelFor (value) : juce::String (juce::roundToInt (value));
        g.setColour (look.style == ui::Style::pedal ? juce::Colour (0xfff2f2f2) : look.ink);
        g.setFont (rowFont());
        g.drawFittedText (look.style == ui::Style::retro ? text.toUpperCase() : text, box.reduced (8.0f, 0.0f).withTrimmedRight (12.0f).toNearestInt(),
                          juce::Justification::centredLeft, 1, 0.7f);
        // The caret: a small triangle pointing down.
        const auto c = juce::Point<float> (box.getRight() - 11.0f, box.getCentreY());
        juce::Path caret;
        caret.addTriangle (c.x - 4.0f, c.y - 2.0f, c.x + 4.0f, c.y - 2.0f, c.x, c.y + 3.0f);
        g.setColour (look.style == ui::Style::pedal ? juce::Colour (0xfff2f2f2).withAlpha (0.7f) : look.ink.withAlpha (0.6f));
        g.fillPath (caret);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (control.labels.isEmpty())
            return;
        if (segmented)
        {
            const auto rows = rowAreas();
            for (int i = 0; i < (int) rows.size(); ++i)
                if (rows[(size_t) i].contains (e.position))
                    set (control.min + (float) i);
            return;
        }
        juce::PopupMenu menu;
        const int selected = juce::roundToInt (value - control.min);
        for (int i = 0; i < control.labels.size(); ++i)
            menu.addItem (i + 1, control.labels[i], true, i == selected);
        juce::Component::SafePointer<MenuCell> self (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (getWidth()),
                            [self] (int chosen)
                            {
                                if (self != nullptr && chosen > 0)
                                    self->set (self->control.min + (float) (chosen - 1));
                            });
    }

private:
    bool segmented = false;

    static int maxRows (ui::Size s) { return s == ui::Size::s ? 3 : (s == ui::Size::l ? 5 : 4); }

    juce::Font rowFont() const
    {
        if (look.style == ui::Style::retro) return Look::mono (size == ui::Size::s ? 10.0f : 11.0f);
        return Look::sans (size == ui::Size::s ? 10.0f : 11.0f, look.style == ui::Style::playful ? "Medium" : "Regular");
    }

    float rowsWidth() const { return (float) getWidth() - 12.0f; }

    bool fitsAsRows() const
    {
        const int n = control.labels.size();
        if (n == 0 || n > maxRows (size))
            return false;
        for (const auto& label : control.labels)
            if (juce::GlyphArrangement::getStringWidth (rowFont(), look.style == ui::Style::retro ? label.toUpperCase() : label) > rowsWidth() - 12.0f)
                return false;
        return true;
    }

    std::vector<juce::Rectangle<float>> rowAreas() const
    {
        const auto area = controlArea().toFloat();
        const int n = control.labels.size();
        const float rowHeight = juce::jmin (20.0f, area.getHeight() / (float) juce::jmax (1, n));
        const float top = area.getCentreY() - rowHeight * (float) n * 0.5f;
        std::vector<juce::Rectangle<float>> rows;
        for (int i = 0; i < n; ++i)
            rows.push_back ({ ((float) getWidth() - rowsWidth()) * 0.5f, top + (float) i * rowHeight, rowsWidth(), rowHeight });
        return rows;
    }

    juce::Rectangle<float> boxArea() const
    {
        const auto area = controlArea().toFloat();
        return juce::Rectangle<float> (rowsWidth(), size == ui::Size::s ? 24.0f : 28.0f).withCentre ({ (float) getWidth() * 0.5f, area.getCentreY() });
    }

    float cornerRadius (juce::Rectangle<float> r) const
    {
        switch (look.style)
        {
            case ui::Style::playful: return juce::jmin (12.0f, r.getHeight() * 0.5f);
            case ui::Style::retro: return 2.0f;
            case ui::Style::minimal: return 8.0f;
            case ui::Style::vintage: case ui::Style::pedal: break;
        }
        return 6.0f;
    }

    void paintFrame (juce::Graphics& g, juce::Rectangle<float> r) const
    {
        const float radius = cornerRadius (r);
        switch (look.style)
        {
            case ui::Style::vintage:
                g.setColour (look.ink.withAlpha (0.25f));
                g.fillRoundedRectangle (r.translated (0.0f, 2.0f), radius);
                g.setColour (look.disc);
                g.fillRoundedRectangle (r, radius);
                g.setColour (look.ink.withAlpha (0.25f));
                g.drawRoundedRectangle (r, radius, 1.0f);
                break;
            case ui::Style::minimal:
                g.setColour (look.ink.withAlpha (0.08f));
                g.fillRoundedRectangle (r, radius);
                break;
            case ui::Style::pedal:
                g.setColour (juce::Colour (0xff1c1c1c));
                g.fillRoundedRectangle (r, radius);
                g.setColour (juce::Colour (0xff3a3a3a));
                g.drawRoundedRectangle (r, radius, 1.0f);
                break;
            case ui::Style::playful:
                g.setColour (look.ink.withAlpha (0.1f));
                g.fillRoundedRectangle (r, radius);
                break;
            case ui::Style::retro:
                g.setColour (look.ink.withAlpha (0.5f));
                g.drawRoundedRectangle (r, radius, 1.0f);
                break;
        }
    }

    void paintRow (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& label, bool selected) const
    {
        const auto inner = r.reduced (2.0f, 1.0f);
        const auto text = look.style == ui::Style::retro ? label.toUpperCase() : label;
        juce::Colour ink = look.ink.withAlpha (0.7f);
        if (look.style == ui::Style::pedal)
            ink = juce::Colour (0xfff2f2f2).withAlpha (selected ? 1.0f : 0.55f);
        if (selected)
        {
            switch (look.style)
            {
                case ui::Style::pedal:
                    // An LED beside the chosen label, as on a selector switch.
                    g.setColour (look.accent);
                    g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ inner.getX() + 6.0f, inner.getCentreY() }));
                    break;
                case ui::Style::retro:
                    g.setColour (look.ink);
                    g.fillRect (inner);
                    ink = look.fill;
                    break;
                case ui::Style::vintage: case ui::Style::minimal: case ui::Style::playful:
                    g.setColour (look.accent);
                    g.fillRoundedRectangle (inner, juce::jmax (0.0f, cornerRadius (r) - 2.0f));
                    ink = onAccent();
                    break;
            }
        }
        g.setColour (ink);
        g.setFont (rowFont());
        g.drawFittedText (text, inner.reduced (look.style == ui::Style::pedal ? 10.0f : 2.0f, 0.0f).toNearestInt(),
                          juce::Justification::centred, 1, 0.7f);
    }
};

/**
 * [style:generate]: a whole-number variation. Its number, back and forward either side (one
 * down, one up), and Generate under it, which moves to the next number. 0 means no variation.
 */
class GenerateCell : public LogicCell
{
public:
    GenerateCell (const ParamSource& state, const Look& l, const LogicControl& c, const juce::String& label, ui::Size s)
        : LogicCell (state, l, c, label, s)
    {
        start();
    }

    void paint (juce::Graphics& g) override
    {
        paintName (g);
        const auto number = numberArea(), back = backArea(), forward = forwardArea(), button = buttonArea();

        // The number, in a small window on the vintage and pedal panels.
        const auto window = number.withSizeKeepingCentre (number.getWidth() - 2.0f * arrowWidth() - 4.0f, number.getHeight());
        switch (look.style)
        {
            case ui::Style::vintage:
                g.setColour (look.disc);
                g.fillRoundedRectangle (window, 5.0f);
                g.setColour (look.ink.withAlpha (0.25f));
                g.drawRoundedRectangle (window, 5.0f, 1.0f);
                break;
            case ui::Style::pedal:
                g.setColour (juce::Colour (0xff1c1c1c));
                g.fillRoundedRectangle (window, 5.0f);
                break;
            case ui::Style::playful:
                g.setColour (look.ink.withAlpha (0.1f));
                g.fillRoundedRectangle (window, window.getHeight() * 0.5f);
                break;
            case ui::Style::minimal: case ui::Style::retro:
                break;
        }
        g.setColour (look.style == ui::Style::pedal ? look.accent : look.ink);
        g.setFont (Look::mono (size == ui::Size::s ? 14.0f : (size == ui::Size::l ? 20.0f : 17.0f), true));
        g.drawFittedText (juce::String (juce::roundToInt (value)), window.toNearestInt(), juce::Justification::centred, 1, 0.6f);

        paintArrow (g, back, false, pressedPart == Part::back);
        paintArrow (g, forward, true, pressedPart == Part::forward);
        paintButton (g, button, pressedPart == Part::generate);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        pressedPart = partAt (e.position);
        repaint();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        const auto part = pressedPart;
        pressedPart = Part::none;
        repaint();
        if (part == Part::none || partAt (e.position) != part)
            return;
        const float v = (float) juce::roundToInt (value);
        if (part == Part::back)
            set (v - 1.0f);
        else if (part == Part::forward)
            set (v + 1.0f);
        else
            set (v + 1.0f > control.max ? juce::jmax (control.min, 1.0f) : v + 1.0f);   // the next variation, round again past the last
    }

private:
    enum class Part { none, back, forward, generate };
    Part pressedPart = Part::none;

    juce::Rectangle<float> area() const
    {
        const auto c = controlArea().toFloat();
        return { 4.0f, c.getY(), (float) getWidth() - 8.0f, c.getHeight() };
    }
    float arrowWidth() const { return size == ui::Size::s ? 18.0f : 22.0f; }
    juce::Rectangle<float> numberArea() const { auto a = area(); return a.removeFromTop (a.getHeight() * 0.5f).reduced (0.0f, 2.0f); }
    juce::Rectangle<float> backArea() const { return numberArea().withWidth (arrowWidth()); }
    juce::Rectangle<float> forwardArea() const { auto n = numberArea(); return n.withTrimmedLeft (n.getWidth() - arrowWidth()); }
    juce::Rectangle<float> buttonArea() const
    {
        auto a = area();
        a.removeFromTop (a.getHeight() * 0.5f);
        return a.reduced (4.0f, 3.0f).withSizeKeepingCentre (a.getWidth() - 8.0f, juce::jmin (24.0f, a.getHeight() - 6.0f));
    }

    Part partAt (juce::Point<float> p) const
    {
        if (backArea().contains (p)) return Part::back;
        if (forwardArea().contains (p)) return Part::forward;
        if (buttonArea().contains (p)) return Part::generate;
        return Part::none;
    }

    void paintArrow (juce::Graphics& g, juce::Rectangle<float> r, bool pointsRight, bool down) const
    {
        const auto c = r.getCentre();
        const float w = 4.0f, h = 6.0f;
        juce::Path chevron;
        chevron.startNewSubPath (c.x + (pointsRight ? -w * 0.5f : w * 0.5f), c.y - h);
        chevron.lineTo (c.x + (pointsRight ? w * 0.5f : -w * 0.5f), c.y);
        chevron.lineTo (c.x + (pointsRight ? -w * 0.5f : w * 0.5f), c.y + h);
        g.setColour (look.ink.withAlpha (down ? 1.0f : 0.6f));
        g.strokePath (chevron, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void paintButton (juce::Graphics& g, juce::Rectangle<float> r, bool down) const
    {
        const auto text = look.cased ("Generate");
        juce::Colour ink = look.ink;
        switch (look.style)
        {
            case ui::Style::vintage:
                g.setColour (look.ink.withAlpha (0.25f));
                g.fillRoundedRectangle (r.translated (0.0f, down ? 0.0f : 2.0f), r.getHeight() * 0.5f);
                g.setColour (look.disc);
                g.fillRoundedRectangle (r.translated (0.0f, down ? 1.0f : 0.0f), r.getHeight() * 0.5f);
                g.setColour (look.ink.withAlpha (0.25f));
                g.drawRoundedRectangle (r.translated (0.0f, down ? 1.0f : 0.0f), r.getHeight() * 0.5f, 1.0f);
                break;
            case ui::Style::minimal:
                g.setColour (look.accent.withAlpha (down ? 0.25f : 0.12f));
                g.fillRoundedRectangle (r, 7.0f);
                ink = look.accent;
                break;
            case ui::Style::pedal:
                g.setColour (juce::Colour (down ? 0xff2a2a2a : 0xff1c1c1c));
                g.fillRoundedRectangle (r, 6.0f);
                g.setColour (juce::Colour (0xff3a3a3a));
                g.drawRoundedRectangle (r, 6.0f, 1.0f);
                ink = juce::Colour (0xfff2f2f2);
                break;
            case ui::Style::playful:
                g.setColour (down ? look.accent.darker (0.2f) : look.accent);
                g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
                ink = onAccent();
                break;
            case ui::Style::retro:
                ink = look.ink.withAlpha (down ? 1.0f : 0.8f);
                g.setColour (ink);
                g.setFont (Look::mono (size == ui::Size::s ? 10.0f : 11.0f, true));
                g.drawFittedText ("[" + text + "]", r.toNearestInt(), juce::Justification::centred, 1, 0.6f);
                return;
        }
        g.setColour (ink);
        g.setFont (look.upperCase() ? Look::sans (size == ui::Size::s ? 9.0f : 10.0f, "Bold", 0.06f)
                                    : Look::sans (size == ui::Size::s ? 10.0f : 11.0f, "Medium"));
        g.drawFittedText (text, r.toNearestInt().translated (0, look.style == ui::Style::vintage && down ? 1 : 0),
                          juce::Justification::centred, 1, 0.6f);
    }
};

/** The cell for one of the module's controls, by its style. A menu with no labels is a knob. */
inline std::unique_ptr<juce::Component> makeLogicCell (const ParamSource& state, const Look& look,
                                                       const LogicControl& c, const juce::String& label, ui::Size size)
{
    switch (c.style)
    {
        case LogicControl::Style::bars: return std::make_unique<BarCell> (state, look, c, label, size);
        case LogicControl::Style::generate: return std::make_unique<GenerateCell> (state, look, c, label, size);
        case LogicControl::Style::menu:
            if (! c.labels.isEmpty())
                return std::make_unique<MenuCell> (state, look, c, label, size);
            break;
        case LogicControl::Style::knob: break;
    }
    return std::make_unique<ControlCell> (state, look, c.id, label, c.unit, ControlCell::Kind::knob, size);
}

// The automatic arrangement: a panel for one Faust group, with its children side by side (hgroup)
// or stacked (vgroup), every control in a medium cell. Used when ui.json has no layout.
class GroupPanel : public juce::Component
{
public:
    static constexpr int padding = 12, titleHeight = 22, gap = 12, maxPerRow = 6;

    GroupPanel (const ParamSource& state, const Look& l, const Group& group,
                const std::vector<Control>& controls, bool showTitle)
        : look (l), horizontal (group.kind == Group::Kind::horizontal), title (showTitle ? group.label : juce::String())
    {
        for (const auto& item : group.items)
        {
            if (item.isGroup)
            {
                const auto& child = group.groups[(size_t) item.index];
                if (child.countParameters (controls) == 0)
                    continue;   // a group with nothing to show (voice controls only, or all logic driven)
                children.push_back (std::make_unique<GroupPanel> (state, look, child, controls, child.label.isNotEmpty()));
            }
            else
            {
                const auto& c = controls[(size_t) item.index];
                if (c.isParameter())
                    children.push_back (std::make_unique<ControlCell> (state, look, c.id, c.label, c.unit, kindOf (c), ui::Size::m));
            }
        }
        for (auto& child : children)
            addAndMakeVisible (*child);

        // A vertical group of nothing but controls (a dsp with no groups at all) reads better as rows
        // of knobs than as one knob per line.
        bool onlyControls = true;
        for (auto& child : children)
            if (dynamic_cast<GroupPanel*> (child.get()) != nullptr)
                onlyControls = false;
        if (onlyControls)
            horizontal = true;
        measure();
    }

    // A panel of cells and panels made elsewhere (the logic module's controls and their groups),
    // side by side and wrapping after six.
    GroupPanel (const Look& l, const juce::String& panelTitle, std::vector<std::unique_ptr<juce::Component>> items)
        : look (l), horizontal (true), title (panelTitle), children (std::move (items))
    {
        for (auto& child : children)
            addAndMakeVisible (*child);
        measure();
    }

    int preferredWidth = 0, preferredHeight = 0;

    void paint (juce::Graphics& g) override
    {
        if (title.isEmpty())
            return;
        const auto area = getLocalBounds().toFloat().reduced (0.5f);
        switch (look.style)
        {
            case ui::Style::minimal:
                g.setColour (look.palette.bg);
                g.fillRoundedRectangle (area, 8.0f);
                break;
            case ui::Style::pedal:
                g.setColour (look.ink.withAlpha (0.25f));
                g.drawRoundedRectangle (area, 10.0f, 1.0f);
                break;
            case ui::Style::playful:
                g.setColour (look.ink.withAlpha (0.07f));
                g.fillRoundedRectangle (area, 16.0f);
                break;
            case ui::Style::retro:
                g.setColour (look.ink.withAlpha (0.3f));
                g.drawRoundedRectangle (area, 4.0f, 1.0f);
                break;
            case ui::Style::vintage:
                g.setColour (look.palette.panel);
                g.fillRoundedRectangle (area, 10.0f);
                g.setColour (look.ink.withAlpha (0.12f));
                g.drawRoundedRectangle (area, 10.0f, 1.0f);
                break;
        }
        g.setColour (look.ink.withAlpha (0.65f));
        g.setFont (look.groupFont());
        g.drawText (look.cased (title), getLocalBounds().removeFromTop (titleHeight + 2).reduced (padding, 0).withTrimmedTop (4),
                    juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (title.isEmpty() ? 0 : padding);
        if (title.isNotEmpty())
            area.removeFromTop (titleHeight - padding / 2);

        if (! horizontal)
        {
            // Stacked panels stretch to the width of the widest one; single controls keep their size.
            for (auto& child : children)
            {
                const bool panel = dynamic_cast<GroupPanel*> (child.get()) != nullptr;
                child->setBounds (area.removeFromTop (heightOf (*child)).withWidth (panel ? area.getWidth() : widthOf (*child)));
                area.removeFromTop (gap);
            }
            return;
        }

        int x = area.getX(), y = area.getY(), rowHeight = 0, count = 0;
        for (auto& child : children)
        {
            const int w = widthOf (*child), h = heightOf (*child);
            if (count == maxPerRow)
            {
                x = area.getX();
                y += rowHeight + gap;
                rowHeight = 0;
                count = 0;
            }
            child->setBounds (x, y, w, h);
            x += w + gap;
            rowHeight = juce::jmax (rowHeight, h);
            ++count;
        }
    }

private:
    const Look& look;
    bool horizontal;
    juce::String title;
    std::vector<std::unique_ptr<juce::Component>> children;

    static int widthOf (juce::Component& c)
    {
        if (auto* panel = dynamic_cast<GroupPanel*> (&c)) return panel->preferredWidth;
        return ui::cellSize (ui::Size::m).w;
    }

    static int heightOf (juce::Component& c)
    {
        if (auto* panel = dynamic_cast<GroupPanel*> (&c)) return panel->preferredHeight;
        return ui::cellSize (ui::Size::m).h;
    }

    void measure()
    {
        int w = 0, h = 0;
        if (horizontal)
        {
            int rowW = 0, rowH = 0, count = 0;
            for (auto& child : children)
            {
                if (count == maxPerRow)
                {
                    w = juce::jmax (w, rowW - gap);
                    h += rowH + gap;
                    rowW = rowH = count = 0;
                }
                rowW += widthOf (*child) + gap;
                rowH = juce::jmax (rowH, heightOf (*child));
                ++count;
            }
            w = juce::jmax (w, rowW - gap);
            h += rowH;
        }
        else
        {
            for (auto& child : children)
            {
                w = juce::jmax (w, widthOf (*child));
                h += heightOf (*child) + gap;
            }
            h -= children.empty() ? 0 : gap;
        }
        const int frame = title.isEmpty() ? 0 : padding * 2;
        preferredWidth = w + frame;
        preferredHeight = h + frame + (title.isEmpty() ? 0 : titleHeight - padding / 2);
    }
};

/**
 * Across the foot of the panel when the step budget stopped the logic module (PangeaLogicGuard.h,
 * decision 46): why, and how to start it again. In the panel's ink, so it reads in every look.
 */
class StoppedBanner : public juce::Component
{
public:
    static constexpr int height = 30;

    explicit StoppedBanner (const Look& l) : look (l) { setInterceptsMouseClicks (false, false); }

    /** The words for a reason (pangea::logic::Stop as a number); empty for 0, running. */
    static juce::String words (int why)
    {
        switch (why)
        {
            case 1: return "Stopped: it ran for too long. Reopen the project to start it again.";
            case 2: return "Stopped: it called itself too deeply. Reopen the project to start it again.";
            case 3: return "Not started: this Mac could not give it a step budget.";
            default: return {};
        }
    }

    void show (int why)
    {
        const auto text = words (why);
        if (text != shown)
        {
            shown = text;
            setTitle (text);
            setVisible (text.isNotEmpty());
            repaint();
        }
    }

    const juce::String& text() const noexcept { return shown; }

    void paint (juce::Graphics& g) override
    {
        const auto area = getLocalBounds().toFloat();
        g.setColour (look.ink);
        g.fillRoundedRectangle (area, juce::jmin (6.0f, look.panelRadius()));
        g.setColour (look.fill);
        g.setFont (look.labelFont());
        g.drawFittedText (shown, getLocalBounds().reduced (10, 0), juce::Justification::centred, 1, 0.8f);
    }

private:
    const Look& look;
    juce::String shown;
};

/**
 * Pangea's bypass: a plain switch for the far right of the title row. The power sign the web
 * panels use, lit the same way: filled with the accent while the plugin is in, an outline while
 * it is bypassed. One drawing in the look's own colours serves all five looks. Its toggle state
 * is the parameter's: on means bypassed.
 */
class BypassSwitch : public juce::Button
{
public:
    /** In layout units: the disc that is drawn, as wide as the library's button, and the switch
        itself, which has a little room to press around the disc. */
    static constexpr int disc = 22, size = 28;

    explicit BypassSwitch (const Look& l) : juce::Button ("Bypass"), look (l)
    {
        setClickingTogglesState (true);
        setTitle ("Bypass");
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        const bool bypassed = getToggleState();
        const auto centre = getLocalBounds().toFloat().getCentre();
        const float radius = 0.5f * (float) disc;
        const auto face = juce::Rectangle<float> ((float) disc, (float) disc).withCentre (centre);
        juce::Colour sign;
        if (bypassed)
        {
            sign = look.ink.withAlpha (highlighted || down ? 0.85f : 0.6f);
            g.setColour (sign);
            g.drawEllipse (face.reduced (0.75f), 1.5f);
        }
        else
        {
            g.setColour (highlighted || down ? look.accent.brighter (0.15f) : look.accent);
            g.fillEllipse (face);
            // The colour that reads on the accent: the screen on a retro panel, otherwise the
            // palette's background or its text, whichever stands out more.
            const auto bg = look.palette.bg, text = look.palette.text;
            sign = look.style == ui::Style::retro ? look.fill
                                                  : (Look::contrast (bg, look.accent) >= Look::contrast (text, look.accent) ? bg : text);
            g.setColour (sign);
        }
        // The power sign: a ring open at the top, and a stroke through the opening.
        const float ring = radius * 0.48f, gap = 0.62f;
        const juce::PathStrokeType stroke (1.7f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        juce::Path arc;
        arc.addCentredArc (centre.x, centre.y, ring, ring, 0.0f, gap, juce::MathConstants<float>::twoPi - gap, true);
        g.strokePath (arc, stroke);
        juce::Path stem;
        stem.startNewSubPath (centre.x, centre.y - radius * 0.62f);
        stem.lineTo (centre.x, centre.y - radius * 0.08f);
        g.strokePath (stem, stroke);
    }

private:
    const Look& look;
};

/** The panel itself, in layout units: the stage around it, its face, the title row and the controls. */
class Faceplate : public juce::Component
{
public:
    static constexpr int stageMargin = 12;
    /** Between the bypass switch and what is drawn to its left in the title row. */
    static constexpr int bypassGap = 10;

    /** `withBypass` keeps the far right of the title row for Pangea's bypass switch. */
    Faceplate (const Look& l, const juce::String& titleText, int panelWidth, int panelHeight, bool withBypass = false)
        : look (l), title (titleText), panelW (panelWidth), panelH (panelHeight), hasBypass (withBypass)
    {
        setSize (panelW + 2 * stageMargin, panelH + 2 * stageMargin);
        setOpaque (true);
    }

    /** The top left corner of the area under the title row, where layout positions start. */
    juce::Point<int> origin() const { return { stageMargin, stageMargin + look.headerHeight() }; }

    /** Where the bypass switch sits: the far right of the title row, level with the title. Its
        disc's edge is where the row's text ends. */
    juce::Rectangle<int> bypassBounds() const
    {
        const int rowTop = stageMargin + 8, rowHeight = look.headerHeight() - 8;
        const int inset = (BypassSwitch::size - BypassSwitch::disc) / 2;   // the room around the disc
        return { stageMargin + panelW - ui::layoutPadding - BypassSwitch::size + inset,
                 rowTop + (rowHeight - BypassSwitch::size) / 2, BypassSwitch::size, BypassSwitch::size };
    }

    /** How much of the title row's right end the switch takes from what is drawn beside it. */
    int bypassRoom() const { return hasBypass ? BypassSwitch::disc + bypassGap : 0; }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (look.stage);
        const auto panel = juce::Rectangle<float> ((float) stageMargin, (float) stageMargin, (float) panelW, (float) panelH);
        const float radius = look.panelRadius();
        g.setColour (look.fill);
        g.fillRoundedRectangle (panel, radius);

        switch (look.style)
        {
            case ui::Style::vintage:
                g.setColour (look.ink.withAlpha (0.12f));
                g.drawRoundedRectangle (panel.reduced (0.5f), radius, 1.0f);
                break;
            case ui::Style::pedal:
            {
                g.setColour (look.ink.withAlpha (0.3f));
                g.drawRoundedRectangle (panel.reduced (1.5f), radius - 1.5f, 3.0f);
                for (auto corner : { panel.getTopLeft(), panel.getTopRight(), panel.getBottomLeft(), panel.getBottomRight() })
                {
                    const juce::Point<float> screw { corner.x + (corner.x < panel.getCentreX() ? 12.0f : -12.0f),
                                                     corner.y + (corner.y < panel.getCentreY() ? 12.0f : -12.0f) };
                    g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (screw));
                }
                break;
            }
            case ui::Style::retro:
            {
                juce::Graphics::ScopedSaveState saved (g);
                juce::Path face;
                face.addRoundedRectangle (panel, radius);
                g.reduceClipRegion (face);
                g.setColour (look.ink.withAlpha (0.06f));
                for (float y = panel.getY(); y < panel.getBottom(); y += 3.0f)
                    g.fillRect (panel.getX(), y, panel.getWidth(), 1.0f);
                break;
            }
            case ui::Style::minimal:
            case ui::Style::playful:
                break;
        }
        if (look.style == ui::Style::retro)
        {
            g.setColour (juce::Colours::black.withAlpha (0.6f));
            g.drawRoundedRectangle (panel.reduced (1.0f), radius, 2.0f);
        }

        const auto header = panel.withHeight ((float) look.headerHeight()).reduced ((float) ui::layoutPadding, 0.0f).withTrimmedTop (8.0f);
        // The far right of the row is the bypass switch's (the editor places it there). The word
        // "Pangea" sits to its left, and the title has what is left of the row.
        const auto room = (float) bypassRoom();
        g.setColour (look.ink);
        g.setFont (look.titleFont());
        if (look.style == ui::Style::pedal)
        {
            // The pedal's title is centred: it gives up the switch's room on both sides, to stay so.
            g.drawFittedText (look.cased (title), header.reduced (room, 0.0f).toNearestInt(), juce::Justification::centred, 1, 0.8f);
            return;
        }
        const auto subtitle = look.cased ("Pangea");
        const float subtitleWidth = juce::GlyphArrangement::getStringWidth (look.subtitleFont(), subtitle) + 4.0f;
        g.drawFittedText (look.cased (title), header.withTrimmedRight (subtitleWidth + 16.0f + room).toNearestInt(),
                          juce::Justification::centredLeft, 1, 0.8f);
        g.setColour (look.ink.withAlpha (0.7f));
        g.setFont (look.subtitleFont());
        g.drawText (subtitle, header.withTrimmedRight (room), juce::Justification::centredRight, false);
    }

private:
    const Look& look;
    juce::String title;
    int panelW, panelH;
    bool hasBypass;
};

class Editor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    /** The panel's narrowest width in the automatic arrangement, in layout units. */
    static constexpr int minimumAutoWidth = 360;

    /**
     * `logic` are the logic module's own controls (describeLogic); `display` is what its display
     * events last showed, read to light the playhead bars. Both may be empty.
     */
    Editor (juce::AudioProcessor& owner, const ParamSource& state, const Group& root,
            const std::vector<Control>& controls, const juce::String& pluginName, const void* uiData, size_t uiSize,
            const std::vector<LogicControl>& logic = {}, const notes::Display* displayState = nullptr)
        : AudioProcessorEditor (owner), spec (ui::parse (uiData, uiSize)), look (Look::from (spec)), lookAndFeel (look),
          display (displayState), bypassParameter (dynamic_cast<juce::RangedAudioParameter*> (owner.getBypassParameter()))
    {
        setLookAndFeel (&lookAndFeel);
        const auto title = spec.title.isNotEmpty() ? spec.title : pluginName;
        if (spec.hasLayout)
            buildLayout (state, controls, logic, title);
        else
            buildAutomatic (state, root, controls, logic, title);
        addAndMakeVisible (*faceplate);

        // Pangea's bypass: the switch at the far right of the title row, attached to the plugin's
        // bypass parameter, so the host, automation and the panel stay in step.
        if (bypassParameter != nullptr)
        {
            bypassSwitch = std::make_unique<BypassSwitch> (look);
            bypassSwitch->setBounds (faceplate->bypassBounds());
            faceplate->addAndMakeVisible (*bypassSwitch);
            bypassSwitch->onStateChange = [this] { showBypassed (bypassSwitch->getToggleState()); };
            bypassAttachment = std::make_unique<juce::ButtonParameterAttachment> (*bypassParameter, *bypassSwitch, state.undoManager);
            showBypassed (bypassSwitch->getToggleState());
        }

        // Resizable by the host and the corner handle, keeping the panel's shape.
        naturalWidth = faceplate->getWidth();
        naturalHeight = faceplate->getHeight();

        // Hidden until the module is stopped, over the foot of the panel.
        stoppedBanner = std::make_unique<StoppedBanner> (look);
        faceplate->addChildComponent (*stoppedBanner);
        const int inset = Faceplate::stageMargin + 8;
        stoppedBanner->setBounds (inset, naturalHeight - inset - StoppedBanner::height,
                                  naturalWidth - 2 * inset, StoppedBanner::height);
        // It opens at its natural size, or smaller when that would be taller than a laptop screen.
        const double scale = juce::jmin (1.0, 820.0 / naturalHeight);
        const double smallest = juce::jmin (0.5, scale);
        setResizable (true, true);
        setResizeLimits (juce::roundToInt (naturalWidth * smallest), juce::roundToInt (naturalHeight * smallest),
                         naturalWidth * 2, naturalHeight * 2);
        getConstrainer()->setFixedAspectRatio ((double) naturalWidth / (double) naturalHeight);
        setSize (juce::roundToInt (naturalWidth * scale), juce::roundToInt (naturalHeight * scale));

        // The playheads follow the module's display slots, 30 times a second; the stopped message
        // needs only a glance now and then.
        timerCallback();
        if (display != nullptr)
            startTimerHz (bars.empty() ? 4 : 30);
    }

    ~Editor() override
    {
        stopTimer();
        bypassAttachment.reset();
        setLookAndFeel (nullptr);
    }

    /** What ui.json said, as checked: for the render tool and anyone debugging a look. */
    const ui::Spec& getSpec() const noexcept { return spec; }
    int getNaturalWidth() const noexcept { return naturalWidth; }
    int getNaturalHeight() const noexcept { return naturalHeight; }
    int getNumCells() const noexcept { return numCells; }
    /** How many playhead bars are lit now (for the render tool). */
    int getNumLit() const
    {
        int n = 0;
        for (auto* bar : bars)
            n += bar->isLit() ? 1 : 0;
        return n;
    }
    /** The stopped message the panel shows, or empty (for the render tool). */
    juce::String getStoppedText() const { return stoppedBanner != nullptr ? stoppedBanner->text() : juce::String(); }
    /** What the web panels fade a bypassed plugin's controls to. */
    static constexpr float bypassedAlpha = 0.45f;
    /** Pangea's bypass switch in the title row, or null when the plugin has no bypass (for the render tool). */
    juce::Button* getBypassSwitch() const noexcept { return bypassSwitch.get(); }
    /** How strongly the controls are drawn, read from the controls themselves: 1 in full, less
        while the plugin is bypassed (for the render tool). The strongest of them is answered, so
        a control left in full while the rest are faint shows as in full. */
    float getControlsAlpha() const
    {
        float strongest = 0.0f;
        bool any = false;
        auto read = [&] (const juce::Component* holder)
        {
            if (holder == nullptr)
                return;
            strongest = any ? juce::jmax (strongest, holder->getAlpha()) : holder->getAlpha();
            any = true;
        };
        read (body.get());
        read (logicBody.get());
        for (const auto& cell : cells)
            read (cell.get());
        return any ? strongest : (shownBypassed ? bypassedAlpha : 1.0f);
    }
    /** True while the panel draws every control fainter, for a bypassed plugin (for the render tool). */
    bool showsBypassed() const { return getControlsAlpha() < 1.0f; }

    void paint (juce::Graphics& g) override { g.fillAll (look.stage); }

    void resized() override
    {
        faceplate->setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) naturalWidth));
    }

private:
    ui::Spec spec;
    Look look;
    LookAndFeel lookAndFeel;
    const notes::Display* display = nullptr;
    std::unique_ptr<Faceplate> faceplate;
    std::unique_ptr<StoppedBanner> stoppedBanner;
    std::unique_ptr<GroupPanel> body, logicBody;
    std::vector<std::unique_ptr<juce::Component>> cells;
    std::vector<BarCell*> bars;   // the bars a playhead lights, wherever they sit
    int naturalWidth = 0, naturalHeight = 0, numCells = 0;

    // Pangea's bypass: the plugin's parameter, the switch in the title row and what ties the two.
    juce::RangedAudioParameter* bypassParameter = nullptr;
    std::unique_ptr<BypassSwitch> bypassSwitch;
    std::unique_ptr<juce::ButtonParameterAttachment> bypassAttachment;
    bool shownBypassed = false;

    // Bypassed, the controls are drawn fainter and stay usable. The title row is left as it is.
    void showBypassed (bool bypassed)
    {
        if (bypassed == shownBypassed)
            return;
        shownBypassed = bypassed;
        const float alpha = bypassed ? bypassedAlpha : 1.0f;
        if (body != nullptr) body->setAlpha (alpha);
        if (logicBody != nullptr) logicBody->setAlpha (alpha);
        for (auto& cell : cells)
            cell->setAlpha (alpha);
    }

    void timerCallback() override
    {
        const bool playing = display != nullptr && display->playing.load (std::memory_order_relaxed);
        for (auto* bar : bars)
            bar->setLit (playing && bar->slot() >= 0 && display->position (bar->slot()) == bar->position());
        if (display != nullptr && stoppedBanner != nullptr)
            stoppedBanner->show (display->stopped.load (std::memory_order_relaxed));
    }

    std::unique_ptr<juce::Component> logicCell (const ParamSource& state, const LogicControl& c,
                                                const juce::String& label, ui::Size size)
    {
        auto cell = makeLogicCell (state, look, c, label, size);
        if (auto* bar = dynamic_cast<BarCell*> (cell.get()); bar != nullptr && bar->slot() >= 0)
            bars.push_back (bar);
        return cell;
    }

    // The labels the web panel leaves off for the voice allocator (VOICE_CONTROL_LABELS).
    static bool isVoiceLabel (const juce::String& label) { return label == "freq" || label == "gain" || label == "gate"; }

    void buildAutomatic (const ParamSource& state, const Group& root, const std::vector<Control>& controls,
                         const std::vector<LogicControl>& logic, const juce::String& title)
    {
        // Faust wraps everything in one top-level group named after the plugin: skip that frame.
        const Group* top = &root;
        while (top->items.size() == 1 && top->items[0].isGroup)
            top = &top->groups[0];

        const int faustCount = top->countParameters (controls);
        int w = 0, h = 0;
        if (faustCount > 0)
        {
            body = std::make_unique<GroupPanel> (state, look, *top, controls, false);
            w = body->preferredWidth;
            h = body->preferredHeight;
        }
        if (! logic.empty())
        {
            // The module's controls as meta.ts's logicTree holds them: those in no group side by side,
            // each [group:...] in its own titled panel where its first control appears.
            std::vector<std::unique_ptr<juce::Component>> items;
            std::vector<juce::String> groupOrder;
            std::map<juce::String, std::vector<std::unique_ptr<juce::Component>>> grouped;
            for (const auto& c : logic)
            {
                auto cell = logicCell (state, c, c.name, ui::Size::m);
                if (c.group.isEmpty())
                {
                    items.push_back (std::move (cell));
                    continue;
                }
                if (grouped.find (c.group) == grouped.end())
                {
                    groupOrder.push_back (c.group);
                    items.push_back (nullptr);   // the group's place, filled below
                }
                grouped[c.group].push_back (std::move (cell));
            }
            size_t next = 0;
            for (auto& item : items)
                if (item == nullptr)
                {
                    const auto& name = groupOrder[next++];
                    item = std::make_unique<GroupPanel> (look, name, std::move (grouped[name]));
                }
            logicBody = std::make_unique<GroupPanel> (look, juce::String(), std::move (items));
            w = juce::jmax (w, logicBody->preferredWidth);
            h += (h > 0 ? GroupPanel::gap * 2 : 0) + logicBody->preferredHeight;
        }

        const bool withBypass = bypassParameter != nullptr;
        const float titleWidth = juce::GlyphArrangement::getStringWidth (look.titleFont(), look.cased (title))
                               + juce::GlyphArrangement::getStringWidth (look.subtitleFont(), "PANGEA") + 16.0f
                               + (withBypass ? (float) (BypassSwitch::disc + Faceplate::bypassGap) : 0.0f);   // the bypass switch, at the row's far right
        const int panelW = juce::jmax (minimumAutoWidth, w + 2 * ui::layoutPadding, (int) std::ceil (titleWidth) + 2 * ui::layoutPadding);
        const int top0 = 8;
        const int panelH = look.headerHeight() + top0 + h + ui::layoutPadding;
        faceplate = std::make_unique<Faceplate> (look, title, panelW, panelH, withBypass);

        const auto o = faceplate->origin();
        const int x = o.x + (panelW - w) / 2;
        int y = o.y + top0;
        if (body != nullptr)
        {
            body->setBounds (x, y, body->preferredWidth, body->preferredHeight);
            faceplate->addAndMakeVisible (*body);
            y = body->getBottom() + GroupPanel::gap * 2;
        }
        if (logicBody != nullptr)
        {
            logicBody->setBounds (x, y, logicBody->preferredWidth, logicBody->preferredHeight);
            faceplate->addAndMakeVisible (*logicBody);
        }
        numCells = faustCount + (int) logic.size();
    }

    void buildLayout (const ParamSource& state, const std::vector<Control>& controls,
                      const std::vector<LogicControl>& logic, const juce::String& title)
    {
        struct Entry { const Control* faust = nullptr; const LogicControl* logic = nullptr; };
        std::map<juce::String, Entry> entries;
        juce::StringArray keys, fresh;
        for (const auto& c : controls)
        {
            if (! c.isParameter())
                continue;
            const auto key = c.key();
            if (keys.contains (key))
                continue;   // two controls with one key: the panel shows the first, as the web panel does
            keys.add (key);
            entries[key].faust = &c;
            if (! isVoiceLabel (c.label))
                fresh.add (key);
        }
        // The logic module's controls by their keys, "<group>/<name>" (src/lib/ui/keys.ts), so the
        // person's layout places them. One whose key is taken (by a Faust control of the same name)
        // still shows, below the rest: its private key starts with a character no layout item holds.
        for (const auto& c : logic)
        {
            auto key = c.key();
            if (keys.contains (key))
                key = juce::String::charToString (1) + "logic/" + c.id;
            keys.add (key);
            fresh.add (key);
            entries[key].logic = &c;
        }

        const auto fitted = ui::fitLayout (spec.layout, keys, fresh);
        faceplate = std::make_unique<Faceplate> (look, title, ui::layoutWidth, look.headerHeight() + fitted.height, bypassParameter != nullptr);
        const auto o = faceplate->origin();
        for (const auto& item : fitted.items)
        {
            const auto it = entries.find (item.key);
            if (item.hidden || it == entries.end())
                continue;
            const auto& e = it->second;
            std::unique_ptr<juce::Component> cell;
            if (e.faust != nullptr)
                cell = std::make_unique<ControlCell> (state, look, e.faust->id, item.label.isNotEmpty() ? item.label : e.faust->label,
                                                      e.faust->unit, kindOf (*e.faust), item.size);
            else
                cell = logicCell (state, *e.logic, item.label.isNotEmpty() ? item.label : e.logic->name, item.size);
            const auto size = ui::cellSize (item.size);
            cell->setBounds (o.x + item.x, o.y + item.y, size.w, size.h);
            faceplate->addAndMakeVisible (*cell);
            cells.push_back (std::move (cell));
        }
        numCells = (int) cells.size();
    }
};

} // namespace pangea
