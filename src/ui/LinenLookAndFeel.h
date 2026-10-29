#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <melatonin_blur/melatonin_blur.h>

#include "Palette.h"

namespace linen
{

/** The embedded typefaces, loaded once per process. */
struct Typefaces
{
    Typefaces();

    juce::Typeface::Ptr serifLight, serifMedium, serifItalic, sans, sansMedium;
};

/** Font helpers. Kerning is a fraction of the font height (0.08 = +0.08em). */
namespace fonts
{
    juce::Font serif (float height, float kerning = 0.0f);
    juce::Font serifMedium (float height, float kerning = 0.0f);
    juce::Font serifItalic (float height, float kerning = 0.0f);
    juce::Font sans (float height, float kerning = 0.08f);
    juce::Font sansMedium (float height, float kerning = 0.08f);
}

class LinenLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LinenLookAndFeel();

    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;
    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getPopupMenuFont() override;

private:
    juce::SharedResourcePointer<Typefaces> typefaces;
};

/** A champagne-metal rotary knob with its own caption and value readout.
    Its arc lights cobalt while you touch it. Owns its shadows, since
    melatonin caches them per object. */
class LinenKnob final : public juce::Slider
{
public:
    /** A live light around the knob: halo strength (0..1), its colour, and how
        far the champagne body is tinted toward that colour (0..1). */
    struct Glow
    {
        float halo = 0.0f;
        juce::Colour colour;
        float tint = 0.0f;
    };

    LinenKnob (juce::String caption, float knobDiameter);

    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    float getKnobDiameter() const noexcept { return diameter; }

    /** Bipolar knobs (gains) draw their value arc out from the centre. */
    void setBipolar (bool shouldBeBipolar) { bipolar = shouldBeBipolar; repaint(); }

    /** Gives the knob a live halo, animated on VBlank (Silk, Pain). */
    void setGlow (std::function<Glow()> source);

private:
    juce::Rectangle<float> knobArea() const;

    juce::String caption;
    float diameter;
    bool hovering = false;
    bool bipolar = false;

    std::function<Glow()> glowSource;
    Glow glow;
    std::unique_ptr<juce::VBlankAttachment> glowTicker;

    melatonin::DropShadow bodyShadow {
        { linen::palette::espresso.withAlpha (0.20f), 14, { 0, 7 } },
        { linen::palette::espresso.withAlpha (0.12f), 3, { 0, 1 } }
    };
    melatonin::InnerShadow wellShadow {
        { linen::palette::espresso.withAlpha (0.14f), 6, { 0, 2 } }
    };
};

/** A small capsule toggle: hairline outline when off; champagne with a lit
    cobalt LED when on. */
class LinenPill final : public juce::ToggleButton
{
public:
    explicit LinenPill (const juce::String& caption);

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

    /** Width that fits the caption. */
    int getIdealWidth() const;
};

/** A section's power switch: a small round button with a power glyph, lit
    cobalt while the section is in. */
class LinenPower final : public juce::ToggleButton
{
public:
    LinenPower();

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
};

/** A continuous value shown as a small pill (e.g. a shelf's frequency): drag
    up or down, or scroll, to change it; double-click to reset. A Slider
    underneath, so it attaches to a parameter like a knob. */
class LinenValuePill final : public juce::Slider
{
public:
    LinenValuePill();

    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    bool hovering = false;
};

/** Two-way switch (e.g. SOFT | HARD) bound to a two-choice parameter. */
class LinenSegmented final : public juce::Component,
                             public juce::SettableTooltipClient
{
public:
    LinenSegmented (juce::RangedAudioParameter& param, juce::StringArray labels);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    int getIdealWidth() const;

private:
    juce::StringArray labels;
    juce::ParameterAttachment attachment;
    int selected = 0;
};

/** A stepped switch shown as a small pill with its value, like a console's
    frequency selector: click for the list, scroll or drag to step. */
class LinenStepper final : public juce::Component,
                           public juce::SettableTooltipClient
{
public:
    explicit LinenStepper (juce::RangedAudioParameter& param);

    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void step (int delta);
    int numSteps() const;

    juce::RangedAudioParameter& parameter;
    juce::ParameterAttachment attachment;
    int index = 0, dragStartIndex = 0;
    bool hovering = false, dragged = false;
};

} // namespace linen
