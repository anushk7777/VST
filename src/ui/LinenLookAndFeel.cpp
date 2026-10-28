#include "LinenLookAndFeel.h"
#include "Palette.h"

#include <BinaryData.h>

namespace linen
{

namespace
{
    juce::Typeface::Ptr load (const char* data, int size)
    {
        return juce::Typeface::createSystemTypefaceFor (data, (size_t) size);
    }

    juce::Font make (const juce::Typeface::Ptr& face, float height, float kerning)
    {
        return juce::Font (juce::FontOptions (face).withHeight (height).withKerningFactor (kerning));
    }

    constexpr float startAngle = juce::MathConstants<float>::pi * -0.75f;
    constexpr float endAngle   = juce::MathConstants<float>::pi * 0.75f;
}

Typefaces::Typefaces()
    : serifLight  (load (LinenBinary::CormorantGaramondLight_ttf,  LinenBinary::CormorantGaramondLight_ttfSize)),
      serifMedium (load (LinenBinary::CormorantGaramondMedium_ttf, LinenBinary::CormorantGaramondMedium_ttfSize)),
      serifItalic (load (LinenBinary::CormorantGaramondItalic_ttf, LinenBinary::CormorantGaramondItalic_ttfSize)),
      sans        (load (LinenBinary::InterRegular_ttf,            LinenBinary::InterRegular_ttfSize)),
      sansMedium  (load (LinenBinary::InterMedium_ttf,             LinenBinary::InterMedium_ttfSize))
{
}

namespace fonts
{
    juce::Font serif (float h, float k)       { return make (juce::SharedResourcePointer<Typefaces>()->serifLight, h, k); }
    juce::Font serifMedium (float h, float k) { return make (juce::SharedResourcePointer<Typefaces>()->serifMedium, h, k); }
    juce::Font serifItalic (float h, float k) { return make (juce::SharedResourcePointer<Typefaces>()->serifItalic, h, k); }
    juce::Font sans (float h, float k)        { return make (juce::SharedResourcePointer<Typefaces>()->sans, h, k); }
    juce::Font sansMedium (float h, float k)  { return make (juce::SharedResourcePointer<Typefaces>()->sansMedium, h, k); }
}

//==============================================================================
LinenLookAndFeel::LinenLookAndFeel()
{
    using namespace palette;

    setColour (juce::ResizableWindow::backgroundColourId, alabaster);
    setColour (juce::PopupMenu::backgroundColourId, alabaster);
    setColour (juce::PopupMenu::textColourId, espresso);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, cashmere);
    setColour (juce::PopupMenu::highlightedTextColourId, espresso);
    setColour (juce::TooltipWindow::backgroundColourId, alabaster);
    setColour (juce::TooltipWindow::textColourId, espresso);
    setColour (juce::TooltipWindow::outlineColourId, champagne);
    setColour (juce::Label::textColourId, espresso);
    setColour (juce::TextEditor::backgroundColourId, alabaster);
    setColour (juce::TextEditor::textColourId, espresso);
    setColour (juce::TextEditor::highlightColourId, champagneLight);
    setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::focusedOutlineColourId, champagne);
    setColour (juce::CaretComponent::caretColourId, espresso);
}

juce::Typeface::Ptr LinenLookAndFeel::getTypefaceForFont (const juce::Font& font)
{
    if (font.getTypefacePtr() != nullptr)
        return font.getTypefacePtr();

    return typefaces->sans;
}

juce::Font LinenLookAndFeel::getLabelFont (juce::Label&)
{
    return fonts::sans (13.0f, 0.02f);
}

juce::Font LinenLookAndFeel::getPopupMenuFont()
{
    return fonts::sans (14.0f, 0.02f);
}

//==============================================================================
LinenKnob::LinenKnob (juce::String captionText, float knobDiameter)
    : juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox),
      caption (std::move (captionText)),
      diameter (knobDiameter)
{
    setRotaryParameters (startAngle + juce::MathConstants<float>::twoPi, endAngle + juce::MathConstants<float>::twoPi, true);
    setMouseDragSensitivity (220);
    setVelocityModeParameters (0.6, 1, 0.0, true, juce::ModifierKeys::shiftModifier);
    setPopupDisplayEnabled (false, false, nullptr);
    setTitle (caption);
}

juce::Rectangle<float> LinenKnob::knobArea() const
{
    auto area = getLocalBounds().toFloat();
    return area.removeFromTop (diameter + 12.0f).withSizeKeepingCentre (diameter, diameter);
}

void LinenKnob::mouseEnter (const juce::MouseEvent& e)
{
    hovering = true;
    juce::Slider::mouseEnter (e);
    repaint();
}

void LinenKnob::mouseExit (const juce::MouseEvent& e)
{
    hovering = false;
    juce::Slider::mouseExit (e);
    repaint();
}

void LinenKnob::paint (juce::Graphics& g)
{
    using namespace palette;

    const auto area = knobArea();
    const auto centre = area.getCentre();
    const float r = area.getWidth() * 0.5f;
    const float proportion = (float) valueToProportionOfLength (getValue());
    const float angle = startAngle + proportion * (endAngle - startAngle);
    const bool active = hovering || isMouseButtonDown();

    // Value track: a hairline ring with the set amount drawn in deep champagne.
    {
        const float arcR = r - 1.5f;
        juce::Path track;
        track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour (ink (0.10f));
        g.strokePath (track, juce::PathStrokeType (1.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        juce::Path value;
        const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
        value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
        g.setColour (champagneDeep.withAlpha (active ? 1.0f : 0.85f));
        g.strokePath (value, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Recessed well the knob sits in.
    const float wellR = r - 7.0f;
    juce::Path well;
    well.addEllipse (centre.x - wellR, centre.y - wellR, wellR * 2.0f, wellR * 2.0f);
    g.setColour (cashmere);
    g.fillPath (well);
    wellShadow.render (g, well);

    // Champagne body.
    const float bodyR = wellR - 5.0f;
    const auto body = juce::Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (centre);
    juce::Path bodyPath;
    bodyPath.addEllipse (body);
    bodyShadow.render (g, bodyPath);

    g.setGradientFill (juce::ColourGradient (champagneLight.brighter (0.06f), body.getX() + bodyR * 0.4f, body.getY(),
                                             champagne.darker (0.08f), body.getRight() - bodyR * 0.3f, body.getBottom(), false));
    g.fillPath (bodyPath);

    // Brushed metal: fine concentric rings.
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (bodyPath);
        for (float rr = bodyR; rr > 2.0f; rr -= 1.4f)
        {
            const bool light = (int) (rr * 3.7f) % 2 == 0;
            g.setColour (light ? juce::Colours::white.withAlpha (0.05f) : espresso.withAlpha (0.025f));
            g.drawEllipse (juce::Rectangle<float> (rr * 2.0f, rr * 2.0f).withCentre (centre), 0.6f);
        }

        // Soft specular sheen from the top-left light.
        juce::ColourGradient sheen (juce::Colours::white.withAlpha (0.45f), centre.x - bodyR * 0.35f, body.getY(),
                                    juce::Colours::white.withAlpha (0.0f), centre.x, centre.y + bodyR * 0.1f, true);
        g.setGradientFill (sheen);
        g.fillEllipse (body);
    }

    g.setColour (champagneDeep.withAlpha (0.55f));
    g.drawEllipse (body.reduced (0.4f), 0.8f);

    // Slightly concave cap.
    const float capR = bodyR * 0.62f;
    const auto cap = juce::Rectangle<float> (capR * 2.0f, capR * 2.0f).withCentre (centre);
    g.setGradientFill (juce::ColourGradient (champagne.withAlpha (0.55f), cap.getCentreX(), cap.getY(),
                                             champagneLight.brighter (0.1f).withAlpha (0.65f), cap.getCentreX(), cap.getBottom(), false));
    g.fillEllipse (cap);
    g.setColour (juce::Colours::white.withAlpha (0.35f));
    g.drawEllipse (cap.translated (0.0f, 0.6f), 0.7f);

    // Pointer.
    const auto tip = centre.getPointOnCircumference (bodyR * 0.78f, angle);
    g.setColour (espresso.withAlpha (0.85f));
    g.fillEllipse (juce::Rectangle<float> (4.2f, 4.2f).withCentre (tip));

    // Caption, or the value while hovering/dragging.
    auto textArea = getLocalBounds().toFloat().withTrimmedTop (area.getBottom() + 4.0f - getLocalBounds().toFloat().getY());
    const auto text = active ? getTextFromValue (getValue()) : caption.toUpperCase();
    g.setColour (active ? espresso : ink (0.62f));
    g.setFont (fonts::sansMedium (11.0f, active ? 0.04f : 0.16f));
    g.drawText (text, textArea.removeFromTop (16.0f), juce::Justification::centred, false);
}

//==============================================================================
namespace
{
    juce::Font pillFont() { return fonts::sansMedium (9.5f, 0.18f); }

    float textWidth (const juce::String& text)
    {
        return juce::GlyphArrangement::getStringWidth (pillFont(), text);
    }
}

LinenPill::LinenPill (const juce::String& caption)
    : juce::ToggleButton (caption)
{
    setClickingTogglesState (true);
}

int LinenPill::getIdealWidth() const
{
    return (int) std::ceil (textWidth (getButtonText().toUpperCase())) + 26;
}

void LinenPill::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    using namespace palette;

    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    const float radius = r.getHeight() * 0.5f;
    const bool on = getToggleState();

    if (on)
    {
        g.setGradientFill (juce::ColourGradient (champagneLight, r.getX(), r.getY(), champagne, r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, radius);
        g.setColour (champagneDeep.withAlpha (0.8f));
        g.drawRoundedRectangle (r, radius, 0.8f);
    }
    else
    {
        g.setColour (highlighted ? cashmere : cashmere.withAlpha (0.0f));
        g.fillRoundedRectangle (r, radius);
        g.setColour (champagneDeep.withAlpha (highlighted ? 0.75f : 0.45f));
        g.drawRoundedRectangle (r, radius, 0.8f);
    }

    g.setColour (on ? espresso : ink (highlighted ? 0.75f : 0.55f));
    g.setFont (pillFont());
    g.drawText (getButtonText().toUpperCase(), getLocalBounds(), juce::Justification::centred, false);
}

LinenSegmented::LinenSegmented (juce::RangedAudioParameter& param, juce::StringArray labelsToShow)
    : labels (std::move (labelsToShow)),
      attachment (param, [this] (float v) { selected = juce::roundToInt (v); repaint(); })
{
    attachment.sendInitialUpdate();
}

int LinenSegmented::getIdealWidth() const
{
    float w = 0.0f;
    for (const auto& l : labels)
        w += textWidth (l.toUpperCase()) + 24.0f;
    return (int) std::ceil (w);
}

void LinenSegmented::paint (juce::Graphics& g)
{
    using namespace palette;

    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    const float radius = r.getHeight() * 0.5f;
    const float segment = r.getWidth() / (float) labels.size();

    g.setColour (champagneDeep.withAlpha (0.45f));
    g.drawRoundedRectangle (r, radius, 0.8f);

    for (int i = 0; i < labels.size(); ++i)
    {
        auto seg = r.withX (r.getX() + segment * (float) i).withWidth (segment);

        if (i == selected)
        {
            g.setGradientFill (juce::ColourGradient (champagneLight, seg.getX(), seg.getY(), champagne, seg.getX(), seg.getBottom(), false));
            g.fillRoundedRectangle (seg.reduced (1.5f), (seg.getHeight() - 3.0f) * 0.5f);
        }

        g.setColour (i == selected ? espresso : ink (0.5f));
        g.setFont (pillFont());
        g.drawText (labels[i].toUpperCase(), seg, juce::Justification::centred, false);
    }
}

void LinenSegmented::mouseDown (const juce::MouseEvent& e)
{
    const int index = juce::jlimit (0, labels.size() - 1, (int) (e.position.x / ((float) getWidth() / (float) labels.size())));
    if (index != selected)
        attachment.setValueAsCompleteGesture ((float) index);
}

} // namespace linen
