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

    setColour (juce::ResizableWindow::backgroundColourId, surface);
    setColour (juce::PopupMenu::backgroundColourId, surface);
    setColour (juce::PopupMenu::textColourId, navy);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, cobalt);
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::TooltipWindow::backgroundColourId, surface);
    setColour (juce::TooltipWindow::textColourId, navy);
    setColour (juce::TooltipWindow::outlineColourId, cobalt);
    setColour (juce::Label::textColourId, navy);
    setColour (juce::TextEditor::backgroundColourId, surface);
    setColour (juce::TextEditor::textColourId, navy);
    setColour (juce::TextEditor::highlightColourId, cobaltLight);
    setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::focusedOutlineColourId, cobalt);
    setColour (juce::CaretComponent::caretColourId, navy);
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

    // Value track: a hairline ring; the set amount in electric cobalt, with a soft glow.
    {
        const float arcR = r - 1.5f;
        juce::Path track;
        track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour (ink (0.09f));
        g.strokePath (track, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        juce::Path value;
        const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
        value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
        g.setColour (cobalt.withAlpha (active ? 0.28f : 0.16f));
        g.strokePath (value, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (active ? electric : cobalt);
        g.strokePath (value, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // The seat the knob sits in.
    const float seatR = r - 6.5f;
    juce::Path seat;
    seat.addEllipse (centre.x - seatR, centre.y - seatR, seatR * 2.0f, seatR * 2.0f);
    g.setColour (well);
    g.fillPath (seat);
    wellShadow.render (g, seat);

    // Body: glossy white porcelain, or cobalt enamel for the featured knobs.
    const float bodyR = seatR - 4.0f;
    const auto body = juce::Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (centre);
    juce::Path bodyPath;
    bodyPath.addEllipse (body);
    bodyShadow.render (g, bodyPath);

    const juce::Point<float> light { centre.x - bodyR * 0.35f, centre.y - bodyR * 0.45f };
    juce::ColourGradient fill (featured ? cobaltLight : porcelain, light, featured ? cobaltDeep : porcelainShade,
                               light.translated (bodyR * 1.55f, bodyR * 1.55f), true);
    fill.addColour (0.45, featured ? cobalt : juce::Colour (0xfff4f7fc));
    g.setGradientFill (fill);
    g.fillPath (bodyPath);

    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (bodyPath);

        // Specular gloss across the top...
        const auto gloss = juce::Rectangle<float> (bodyR * 1.5f, bodyR * 1.0f).withCentre ({ centre.x, centre.y - bodyR * 0.45f });
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (featured ? 0.55f : 0.95f), gloss.getCentreX(), gloss.getY(),
                                                 juce::Colours::white.withAlpha (0.0f), gloss.getCentreX(), gloss.getBottom(), false));
        g.fillEllipse (gloss);

        // ...and a faint reflected light along the bottom edge.
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.0f), centre.x, centre.y + bodyR * 0.55f,
                                                 juce::Colours::white.withAlpha (featured ? 0.28f : 0.6f), centre.x, body.getBottom(), false));
        g.fillEllipse (body.reduced (bodyR * 0.08f).translated (0.0f, bodyR * 0.06f));
    }

    g.setColour (featured ? cobaltDeep.darker (0.3f).withAlpha (0.7f) : rim);
    g.drawEllipse (body.reduced (0.4f), 0.8f);

    // Pointer: a short line, white on enamel, cobalt on porcelain.
    const auto inner = centre.getPointOnCircumference (bodyR * 0.36f, angle);
    const auto tip = centre.getPointOnCircumference (bodyR * 0.78f, angle);
    g.setColour (featured ? juce::Colours::white : (active ? electric : cobalt));
    g.drawLine ({ inner, tip }, featured ? 2.4f : 2.2f);

    // Caption, or the value while hovering/dragging.
    auto textArea = getLocalBounds().toFloat().withTrimmedTop (area.getBottom() + 4.0f - getLocalBounds().toFloat().getY());
    const auto text = active ? getTextFromValue (getValue()) : caption.toUpperCase();
    g.setColour (active ? cobaltDeep : ink (0.6f));
    g.setFont (fonts::sansMedium (11.0f, active ? 0.04f : 0.16f));
    g.drawText (text, textArea.removeFromTop (16.0f), juce::Justification::centred, false);
}

//==============================================================================
namespace
{
    juce::Font pillFont() { return fonts::sansMedium (9.5f, 0.18f); }

    /** Glossy cobalt enamel capsule: lit top, deep bottom, a highlight band. */
    void fillGlossyCobalt (juce::Graphics& g, juce::Rectangle<float> r, float radius)
    {
        using namespace palette;
        juce::ColourGradient enamel (cobaltLight, r.getX(), r.getY(), cobaltDeep, r.getX(), r.getBottom(), false);
        enamel.addColour (0.5, cobalt);
        g.setGradientFill (enamel);
        g.fillRoundedRectangle (r, radius);

        const auto band = r.reduced (radius * 0.45f, 1.5f).withHeight (r.getHeight() * 0.42f);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.45f), band.getX(), band.getY(),
                                                 juce::Colours::white.withAlpha (0.0f), band.getX(), band.getBottom(), false));
        g.fillRoundedRectangle (band, band.getHeight() * 0.5f);
    }

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
        fillGlossyCobalt (g, r, radius);
        g.setColour (cobaltDeep.darker (0.2f));
        g.drawRoundedRectangle (r, radius, 0.8f);
    }
    else
    {
        g.setColour (highlighted ? porcelain : porcelain.withAlpha (0.7f));
        g.fillRoundedRectangle (r, radius);
        g.setColour (highlighted ? cobalt.withAlpha (0.7f) : rim);
        g.drawRoundedRectangle (r, radius, 0.8f);
    }

    g.setColour (on ? juce::Colours::white : (highlighted ? cobaltDeep : ink (0.55f)));
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

    g.setColour (porcelain.withAlpha (0.7f));
    g.fillRoundedRectangle (r, radius);
    g.setColour (rim);
    g.drawRoundedRectangle (r, radius, 0.8f);

    for (int i = 0; i < labels.size(); ++i)
    {
        auto seg = r.withX (r.getX() + segment * (float) i).withWidth (segment);

        if (i == selected)
            fillGlossyCobalt (g, seg.reduced (1.5f), (seg.getHeight() - 3.0f) * 0.5f);

        g.setColour (i == selected ? juce::Colours::white : ink (0.5f));
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

//==============================================================================
LinenStepper::LinenStepper (juce::RangedAudioParameter& param)
    : parameter (param),
      attachment (param, [this] (float v) { index = juce::roundToInt (v); repaint(); })
{
    attachment.sendInitialUpdate();
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

int LinenStepper::numSteps() const
{
    return juce::jmax (1, parameter.getNumSteps());
}

void LinenStepper::step (int delta)
{
    const int next = juce::jlimit (0, numSteps() - 1, index + delta);
    if (next != index)
        attachment.setValueAsCompleteGesture ((float) next);
}

void LinenStepper::paint (juce::Graphics& g)
{
    using namespace palette;

    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    const float radius = r.getHeight() * 0.5f;
    const bool off = index == 0 && parameter.getText (0.0f, 32).equalsIgnoreCase ("off");

    g.setColour (hovering ? porcelain : porcelain.withAlpha (0.75f));
    g.fillRoundedRectangle (r, radius);
    g.setColour (hovering ? cobalt.withAlpha (0.7f) : rim);
    g.drawRoundedRectangle (r, radius, 0.8f);

    // Tiny chevrons either side say "this steps".
    g.setColour (cobalt.withAlpha (hovering ? 0.9f : 0.5f));
    const float cy = r.getCentreY();
    for (const float x : { r.getX() + 8.0f, r.getRight() - 8.0f })
    {
        const float dir = x < r.getCentreX() ? -1.0f : 1.0f;
        juce::Path chevron;
        chevron.startNewSubPath (x - dir * 1.5f, cy - 3.0f);
        chevron.lineTo (x + dir * 1.5f, cy);
        chevron.lineTo (x - dir * 1.5f, cy + 3.0f);
        g.strokePath (chevron, juce::PathStrokeType (1.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    g.setColour (off ? ink (0.42f) : navy);
    g.setFont (fonts::sansMedium (10.0f, 0.06f));
    g.drawText (parameter.getCurrentValueAsText(), r.reduced (12.0f, 0.0f), juce::Justification::centred, false);
}

void LinenStepper::mouseEnter (const juce::MouseEvent&) { hovering = true; repaint(); }
void LinenStepper::mouseExit (const juce::MouseEvent&)  { hovering = false; repaint(); }

void LinenStepper::mouseDown (const juce::MouseEvent&)
{
    dragStartIndex = index;
    dragged = false;
}

void LinenStepper::mouseDrag (const juce::MouseEvent& e)
{
    const int delta = (e.getDistanceFromDragStartX() - e.getDistanceFromDragStartY()) / 14;
    if (delta != 0)
        dragged = true;

    const int target = juce::jlimit (0, numSteps() - 1, dragStartIndex + delta);
    if (target != index)
        attachment.setValueAsCompleteGesture ((float) target);
}

void LinenStepper::mouseUp (const juce::MouseEvent& e)
{
    if (dragged || ! e.mouseWasClicked())
        return;

    juce::PopupMenu menu;
    for (int i = 0; i < numSteps(); ++i)
        menu.addItem (i + 1, parameter.getText (parameter.convertTo0to1 ((float) i), 32), true, i == index);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (getWidth()),
                        [safe = juce::Component::SafePointer<LinenStepper> (this)] (int result)
                        {
                            if (safe != nullptr && result > 0)
                                safe->attachment.setValueAsCompleteGesture ((float) (result - 1));
                        });
}

void LinenStepper::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    if (std::abs (wheel.deltaY) > 0.0f)
        step (wheel.deltaY > 0.0f ? 1 : -1);
}

} // namespace linen
