#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <melatonin_blur/melatonin_blur.h>

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
    Owns its shadows, since melatonin caches them per object. */
class LinenKnob final : public juce::Slider
{
public:
    LinenKnob (juce::String caption, float knobDiameter);

    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    float getKnobDiameter() const noexcept { return diameter; }

    /** Bipolar knobs (gains) draw their value arc out from the centre. */
    void setBipolar (bool shouldBeBipolar) { bipolar = shouldBeBipolar; repaint(); }

private:
    juce::Rectangle<float> knobArea() const;

    juce::String caption;
    float diameter;
    bool hovering = false;
    bool bipolar = false;

    melatonin::DropShadow bodyShadow {
        { juce::Colour (0xff221e1c).withAlpha (0.20f), 14, { 0, 7 } },
        { juce::Colour (0xff221e1c).withAlpha (0.12f), 3, { 0, 1 } }
    };
    melatonin::InnerShadow wellShadow {
        { juce::Colour (0xff221e1c).withAlpha (0.14f), 6, { 0, 2 } }
    };
};

} // namespace linen
