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
    setColour (juce::PopupMenu::backgroundColourId, card);
    setColour (juce::PopupMenu::textColourId, espresso);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, champagneLight);
    setColour (juce::PopupMenu::highlightedTextColourId, espresso);
    setColour (juce::TooltipWindow::backgroundColourId, card);
    setColour (juce::TooltipWindow::textColourId, espresso);
    setColour (juce::TooltipWindow::outlineColourId, champagne);
    setColour (juce::Label::textColourId, espresso);
    setColour (juce::TextEditor::backgroundColourId, card);
    setColour (juce::TextEditor::textColourId, espresso);
    setColour (juce::TextEditor::highlightColourId, champagneLight);
    setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::focusedOutlineColourId, cobalt);
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

void LinenKnob::setGlow (std::function<Glow()> source)
{
    glowSource = std::move (source);
    glowTicker = glowSource == nullptr ? nullptr : std::make_unique<juce::VBlankAttachment> (this, [this]
    {
        const auto next = glowSource();
        if (std::abs (next.halo - glow.halo) > 0.004f || std::abs (next.tint - glow.tint) > 0.004f || next.colour != glow.colour)
        {
            glow = next;
            repaint();
        }
    });
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

    // Halo: a soft light around the knob (Silk breathing, Pain burning).
    if (glow.halo > 0.01f)
    {
        juce::ColourGradient halo (glow.colour.withAlpha (0.7f * glow.halo), centre.x, centre.y,
                                   glow.colour.withAlpha (0.0f), centre.x + r + 9.0f, centre.y, true);
        halo.addColour (0.62, glow.colour.withAlpha (0.45f * glow.halo));
        g.setGradientFill (halo);
        g.fillEllipse (area.expanded (9.0f));
    }

    // Value track: a hairline ring; the set amount in deep champagne, lit cobalt while touched.
    {
        const float arcR = r - 1.5f;
        juce::Path track;
        track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour (ink (0.10f));
        g.strokePath (track, juce::PathStrokeType (1.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        juce::Path value;
        const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
        value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
        if (active)
        {
            g.setColour (cobalt.withAlpha (0.22f));
            g.strokePath (value, juce::PathStrokeType (5.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        const auto arcColour = glow.tint > 0.0f ? champagneDeep.interpolatedWith (glow.colour, glow.tint) : champagneDeep;
        g.setColour (active ? cobalt : arcColour.withAlpha (0.9f));
        g.strokePath (value, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Recessed seat.
    const float seatR = r - 7.0f;
    juce::Path seat;
    seat.addEllipse (centre.x - seatR, centre.y - seatR, seatR * 2.0f, seatR * 2.0f);
    g.setColour (well);
    g.fillPath (seat);
    wellShadow.render (g, seat);

    // Champagne body (tinted toward the glow colour when asked).
    const float bodyR = seatR - 5.0f;
    const auto body = juce::Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (centre);
    juce::Path bodyPath;
    bodyPath.addEllipse (body);
    bodyShadow.render (g, bodyPath);

    const auto tinted = [this] (juce::Colour c) { return glow.tint > 0.0f ? c.interpolatedWith (glow.colour, glow.tint * 0.85f) : c; };
    g.setGradientFill (juce::ColourGradient (tinted (champagneLight).brighter (0.06f), body.getX() + bodyR * 0.4f, body.getY(),
                                             tinted (champagne).darker (0.08f), body.getRight() - bodyR * 0.3f, body.getBottom(), false));
    g.fillPath (bodyPath);

    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (bodyPath);

        // Brushed metal: fine concentric rings.
        for (float rr = bodyR; rr > 2.0f; rr -= 1.4f)
        {
            const bool light = (int) (rr * 3.7f) % 2 == 0;
            g.setColour (light ? juce::Colours::white.withAlpha (0.05f) : espresso.withAlpha (0.025f));
            g.drawEllipse (juce::Rectangle<float> (rr * 2.0f, rr * 2.0f).withCentre (centre), 0.6f);
        }

        // Specular sheen from the top-left light.
        juce::ColourGradient sheen (juce::Colours::white.withAlpha (0.5f), centre.x - bodyR * 0.35f, body.getY(),
                                    juce::Colours::white.withAlpha (0.0f), centre.x, centre.y + bodyR * 0.1f, true);
        g.setGradientFill (sheen);
        g.fillEllipse (body);
    }

    g.setColour (tinted (champagneDeep).withAlpha (0.6f));
    g.drawEllipse (body.reduced (0.4f), 0.8f);

    // Slightly concave cap.
    const float capR = bodyR * 0.62f;
    const auto cap = juce::Rectangle<float> (capR * 2.0f, capR * 2.0f).withCentre (centre);
    g.setGradientFill (juce::ColourGradient (tinted (champagne).withAlpha (0.55f), cap.getCentreX(), cap.getY(),
                                             tinted (champagneLight).brighter (0.1f).withAlpha (0.65f), cap.getCentreX(), cap.getBottom(), false));
    g.fillEllipse (cap);
    g.setColour (juce::Colours::white.withAlpha (0.35f));
    g.drawEllipse (cap.translated (0.0f, 0.6f), 0.7f);

    // Pointer.
    const auto tip = centre.getPointOnCircumference (bodyR * 0.78f, angle);
    g.setColour (active ? cobaltDeep : espresso.withAlpha (0.85f));
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

    /** Champagne capsule: lit top, deeper bottom, a fine highlight. */
    void fillChampagne (juce::Graphics& g, juce::Rectangle<float> r, float radius)
    {
        using namespace palette;
        g.setGradientFill (juce::ColourGradient (champagneLight, r.getX(), r.getY(), champagne, r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, radius);
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.drawLine (r.getX() + radius, r.getY() + 1.2f, r.getRight() - radius, r.getY() + 1.2f, 0.8f);
    }

    /** A tiny indicator LED: dark glass when off, a cobalt light with bloom when on. */
    void drawLed (juce::Graphics& g, juce::Point<float> centre, float level)
    {
        using namespace palette;
        const auto led = juce::Rectangle<float> (5.5f, 5.5f).withCentre (centre);
        if (level > 0.01f)
        {
            juce::ColourGradient bloom (electric.withAlpha (0.55f * level), centre.x, centre.y,
                                        electric.withAlpha (0.0f), centre.x + 7.0f, centre.y, true);
            g.setGradientFill (bloom);
            g.fillEllipse (led.expanded (5.0f));
        }
        g.setGradientFill (juce::ColourGradient (level > 0.01f ? cobaltLight.interpolatedWith (juce::Colours::white, 0.3f) : espresso.withAlpha (0.35f),
                                                 led.getX(), led.getY(),
                                                 level > 0.01f ? cobalt : espresso.withAlpha (0.6f), led.getRight(), led.getBottom(), false));
        g.fillEllipse (led);
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        g.fillEllipse (led.withSizeKeepingCentre (2.0f, 1.4f).translated (-0.8f, -1.0f));
    }
}

LinenPill::LinenPill (const juce::String& caption)
    : juce::ToggleButton (caption)
{
    setClickingTogglesState (true);
}

int LinenPill::getIdealWidth() const
{
    return (int) std::ceil (textWidth (getButtonText().toUpperCase())) + 36;
}

void LinenPill::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    using namespace palette;

    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    const float radius = r.getHeight() * 0.5f;
    const bool on = getToggleState();

    if (on)
    {
        fillChampagne (g, r, radius);
        g.setColour (champagneDeep.withAlpha (0.8f));
        g.drawRoundedRectangle (r, radius, 0.8f);
    }
    else
    {
        g.setColour (highlighted ? ivory : ivory.withAlpha (0.6f));
        g.fillRoundedRectangle (r, radius);
        g.setColour (highlighted ? champagneDeep.withAlpha (0.8f) : rim);
        g.drawRoundedRectangle (r, radius, 0.8f);
    }

    drawLed (g, { r.getX() + 11.0f, r.getCentreY() }, on ? 1.0f : 0.0f);

    g.setColour (on ? espresso : ink (highlighted ? 0.75f : 0.55f));
    g.setFont (pillFont());
    g.drawText (getButtonText().toUpperCase(), getLocalBounds().withTrimmedLeft (12), juce::Justification::centred, false);
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

    g.setColour (ivory.withAlpha (0.6f));
    g.fillRoundedRectangle (r, radius);
    g.setColour (rim);
    g.drawRoundedRectangle (r, radius, 0.8f);

    for (int i = 0; i < labels.size(); ++i)
    {
        auto seg = r.withX (r.getX() + segment * (float) i).withWidth (segment);

        if (i == selected)
            fillChampagne (g, seg.reduced (1.5f), (seg.getHeight() - 3.0f) * 0.5f);

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

    g.setColour (hovering ? ivory : ivory.withAlpha (0.65f));
    g.fillRoundedRectangle (r, radius);
    g.setColour (hovering ? champagneDeep.withAlpha (0.8f) : rim);
    g.drawRoundedRectangle (r, radius, 0.8f);

    // Tiny chevrons either side say "this steps"; they light cobalt under the pointer.
    g.setColour (hovering ? cobalt.withAlpha (0.85f) : champagneDeep.withAlpha (0.6f));
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

    g.setColour (off ? ink (0.42f) : espresso);
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
