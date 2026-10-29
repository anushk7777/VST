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
    : display       (load (LinenBinary::ArchivoExpandedSemiBold_ttf, LinenBinary::ArchivoExpandedSemiBold_ttfSize)),
      displayMedium (load (LinenBinary::ArchivoExpandedMedium_ttf,   LinenBinary::ArchivoExpandedMedium_ttfSize)),
      sans          (load (LinenBinary::ArchivoNarrowRegular_ttf,    LinenBinary::ArchivoNarrowRegular_ttfSize)),
      sansMedium    (load (LinenBinary::ArchivoNarrowMedium_ttf,     LinenBinary::ArchivoNarrowMedium_ttfSize))
{
}

namespace fonts
{
    juce::Font display (float h, float k)       { return make (juce::SharedResourcePointer<Typefaces>()->display, h, k); }
    juce::Font displayMedium (float h, float k) { return make (juce::SharedResourcePointer<Typefaces>()->displayMedium, h, k); }
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
namespace
{
    /** Where a knob's parts sit, from its body's diameter outward. */
    struct KnobGeometry
    {
        explicit KnobGeometry (float diameter)
            : body (diameter * 0.5f),
              ledInner (body + 3.0f),
              ledOuter (ledInner + juce::jmax (3.5f, diameter * 0.1f)),
              tickInner (ledOuter + 2.5f),
              tickOuter (tickInner + 5.0f),
              numbers (diameter >= 60.0f),
              numberRadius (tickOuter + 6.5f),
              reach (numbers ? numberRadius + 5.0f : tickOuter + 1.0f)
        {
        }

        float body, ledInner, ledOuter, tickInner, tickOuter;
        bool numbers;
        float numberRadius, reach;
    };

    constexpr float lightAngle = -juce::MathConstants<float>::pi * 0.25f;   // the room's light, from the top left

    float angleAt (float proportion) { return startAngle + proportion * (endAngle - startAngle); }

    /** An engraved line: a dark groove with a lit lower edge. */
    void engrave (juce::Graphics& g, juce::Line<float> line, float width, float depth)
    {
        g.setColour (juce::Colours::white.withAlpha (0.75f));
        g.drawLine (juce::Line<float> (line.getStart().translated (0.45f, 0.65f), line.getEnd().translated (0.45f, 0.65f)), width * 0.85f);
        g.setColour (palette::ink (depth));
        g.drawLine (line, width);
    }

    void drawScale (juce::Graphics& g, juce::Point<float> c, const KnobGeometry& k, bool bipolar)
    {
        // Big knobs get a tick every 5%, the rest every 10%.
        for (int i = 0; i <= 20; i += k.numbers ? 1 : 2)
        {
            const bool major = i % 2 == 0;
            const float a = angleAt ((float) i / 20.0f);
            const float outer = major ? k.tickOuter : k.tickInner + 2.6f;
            engrave (g, { c.getPointOnCircumference (k.tickInner, a), c.getPointOnCircumference (outer, a) },
                     major ? 1.1f : 0.8f, major ? 0.42f : 0.3f);
        }

        if (! k.numbers)
            return;

        g.setFont (fonts::sansMedium (7.5f, 0.0f));
        const auto label = [&] (const juce::String& text, float proportion)
        {
            const auto at = c.getPointOnCircumference (k.numberRadius, angleAt (proportion));
            const auto box = juce::Rectangle<float> (16.0f, 9.0f).withCentre (at);
            g.setColour (juce::Colours::white.withAlpha (0.8f));
            g.drawText (text, box.translated (0.4f, 0.6f), juce::Justification::centred, false);
            g.setColour (palette::ink (0.5f));
            g.drawText (text, box, juce::Justification::centred, false);
        };

        if (bipolar)
        {
            label (juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")), 0.0f);
            label ("0", 0.5f);
            label ("+", 1.0f);
        }
        else
        {
            for (int n = 0; n <= 10; n += 2)
                label (juce::String (n), (float) n / 10.0f);
        }
    }

    /** The segmented LED ring: lit segments glow cobalt (or the knob's glow
        colour), the rest are dark glass. */
    void drawLedRing (juce::Graphics& g, juce::Point<float> c, const KnobGeometry& k, float proportion, bool bipolar,
                      juce::Colour light, float intensity)
    {
        using namespace palette;
        int count = juce::jlimit (15, 31, juce::roundToInt (k.body * 0.9f));
        count |= 1;   // odd, so a bipolar ring has a centre segment

        const float spacing = (endAngle - startAngle) / (float) (count - 1);
        const float mid = 0.5f * (k.ledInner + k.ledOuter), length = k.ledOuter - k.ledInner;
        const float width = juce::jlimit (1.3f, 2.6f, mid * spacing * 0.36f);
        const float position = proportion * (float) (count - 1);
        const float centreIndex = 0.5f * (float) (count - 1);

        juce::Path segment, bloom;
        segment.addRoundedRectangle (-width * 0.5f, -mid - length * 0.5f, width, length, width * 0.5f);
        bloom.addRoundedRectangle (-width * 1.4f, -mid - length * 0.75f, width * 2.8f, length * 1.5f, width * 1.4f);

        const auto hot = light.interpolatedWith (electric, 0.5f);
        for (int i = 0; i < count; ++i)
        {
            float level;
            if (bipolar)
            {
                const float lo = juce::jmin (position, centreIndex), hi = juce::jmax (position, centreIndex);
                level = (float) i >= lo - 0.5f && (float) i <= hi + 0.5f ? 1.0f : 0.0f;
                if (std::abs (position - centreIndex) < 0.25f)
                    level = i == (int) centreIndex ? 0.6f : 0.0f;
            }
            else
            {
                level = proportion <= 0.0f ? 0.0f : juce::jlimit (0.0f, 1.0f, position - (float) i + 1.0f);
            }

            const auto place = juce::AffineTransform::rotation (startAngle + spacing * (float) i).translated (c);
            auto seg = segment;
            seg.applyTransform (place);

            if (level <= 0.0f)
            {
                g.setColour (ink (0.17f));
                g.fillPath (seg);
                continue;
            }

            const float l = level * intensity;
            auto glowPath = bloom;
            glowPath.applyTransform (place);
            g.setColour (hot.withAlpha (0.26f * l));
            g.fillPath (glowPath);
            g.setColour (light.interpolatedWith (ink (0.17f), 1.0f - l));
            g.fillPath (seg);
            g.setColour (juce::Colours::white.withAlpha (0.55f * l));
            const auto from = c.getPointOnCircumference (k.ledInner + length * 0.3f, startAngle + spacing * (float) i);
            const auto to = c.getPointOnCircumference (k.ledOuter - length * 0.3f, startAngle + spacing * (float) i);
            g.drawLine ({ from, to }, width * 0.35f);
        }
    }
}

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

int LinenKnob::getFootprint() const
{
    return (int) std::ceil (2.0f * KnobGeometry (diameter).reach);
}

int LinenKnob::getPreferredHeight() const
{
    const KnobGeometry k (diameter);
    return (int) std::ceil (k.reach + k.body + 24.0f);
}

juce::Point<float> LinenKnob::knobCentre() const
{
    return { (float) getWidth() * 0.5f, KnobGeometry (diameter).reach };
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

    const KnobGeometry k (diameter);
    const auto c = knobCentre();
    const float R = k.body;
    const float proportion = (float) valueToProportionOfLength (getValue());
    const float angle = angleAt (proportion);
    const bool active = hovering || isMouseButtonDown();
    const auto tinted = [this] (juce::Colour col, float amount) { return glow.tint > 0.0f ? col.interpolatedWith (glow.colour, glow.tint * amount) : col; };

    // Halo: a soft light around the knob (Silk breathing, Pain burning).
    if (glow.halo > 0.01f)
    {
        juce::ColourGradient halo (glow.colour.withAlpha (0.55f * glow.halo), c.x, c.y,
                                   glow.colour.withAlpha (0.0f), c.x + k.tickOuter + 6.0f, c.y, true);
        halo.addColour (0.55, glow.colour.withAlpha (0.4f * glow.halo));
        g.setGradientFill (halo);
        g.fillEllipse (juce::Rectangle<float> (2.0f * (k.tickOuter + 6.0f), 2.0f * (k.tickOuter + 6.0f)).withCentre (c));
    }

    drawScale (g, c, k, bipolar);
    drawLedRing (g, c, k, proportion, bipolar, tinted (cobalt, 1.0f), active ? 1.0f : 0.82f);

    // The body's shadow on the panel.
    const auto body = juce::Rectangle<float> (2.0f * R, 2.0f * R).withCentre (c);
    juce::Path bodyPath;
    bodyPath.addEllipse (body);
    bodyShadow.render (g, bodyPath);

    // Knurled skirt: machined champagne gold, its ridges turning with the value.
    g.setGradientFill (juce::ColourGradient (tinted (champagneLight, 0.5f).brighter (0.12f), body.getX(), body.getY(),
                                             tinted (champagneDeep, 0.5f).darker (0.25f), body.getRight(), body.getBottom(), false));
    g.fillPath (bodyPath);
    {
        const float inner = R * 0.79f, outer = R - 0.7f;
        const int ridges = juce::jmax (36, juce::roundToInt (R * 2.4f));
        const float step = juce::MathConstants<float>::twoPi / (float) ridges;
        for (int i = 0; i < ridges; ++i)
        {
            const float a = angle + step * (float) i;
            const float lit = std::cos (a - lightAngle);
            g.setColour (espresso.withAlpha (0.12f + 0.2f * juce::jmax (0.0f, -lit)));
            g.drawLine ({ c.getPointOnCircumference (inner, a), c.getPointOnCircumference (outer, a) }, 0.9f);
            const float b = a + step * 0.5f;
            g.setColour (juce::Colours::white.withAlpha (0.06f + 0.4f * juce::jmax (0.0f, std::cos (b - lightAngle))));
            g.drawLine ({ c.getPointOnCircumference (inner, b), c.getPointOnCircumference (outer, b) }, 0.7f);
        }
    }
    g.setColour (espresso.withAlpha (0.45f));
    g.drawEllipse (body.reduced (0.4f), 0.8f);

    // The lit LEDs' light caught on the metal's edge.
    if (proportion > 0.0f || bipolar)
    {
        const float from = bipolar ? angleAt (0.5f) : startAngle;
        juce::Path spill;
        spill.addCentredArc (c.x, c.y, R - 1.2f, R - 1.2f, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
        g.setColour (tinted (electric, 1.0f).withAlpha (active ? 0.4f : 0.28f));
        g.strokePath (spill, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Polished bevel: lit along its lower right, where it faces the light back.
    const float bevelOuter = R * 0.79f, faceR = R * 0.69f;
    {
        juce::Path bevel;
        bevel.addEllipse (juce::Rectangle<float> (2.0f * bevelOuter, 2.0f * bevelOuter).withCentre (c));
        bevel.addEllipse (juce::Rectangle<float> (2.0f * faceR, 2.0f * faceR).withCentre (c));
        bevel.setUsingNonZeroWinding (false);
        g.setGradientFill (juce::ColourGradient (tinted (champagneDeep, 0.5f).darker (0.1f), c.x - bevelOuter, c.y - bevelOuter,
                                                 tinted (ivory, 0.3f), c.x + bevelOuter * 0.7f, c.y + bevelOuter * 0.7f, false));
        g.fillPath (bevel);
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        juce::Path rimLight;
        rimLight.addCentredArc (c.x, c.y, bevelOuter - 0.6f, bevelOuter - 0.6f, 0.0f, 0.55f, 2.6f, true);
        g.strokePath (rimLight, juce::PathStrokeType (0.8f));
    }

    // Brushed top, turned on a lathe: fine rings and a bright streak across the light.
    const auto face = juce::Rectangle<float> (2.0f * faceR, 2.0f * faceR).withCentre (c);
    juce::Path facePath;
    facePath.addEllipse (face);
    g.setGradientFill (juce::ColourGradient (tinted (champagneLight, 0.4f).brighter (0.08f), c.x, face.getY(),
                                             tinted (champagne, 0.4f), c.x, face.getBottom(), false));
    g.fillPath (facePath);
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (facePath);
        for (float rr = faceR; rr > 1.5f; rr -= 1.2f)
        {
            g.setColour ((int) (rr * 4.3f) % 2 == 0 ? juce::Colours::white.withAlpha (0.07f) : espresso.withAlpha (0.035f));
            g.drawEllipse (juce::Rectangle<float> (2.0f * rr, 2.0f * rr).withCentre (c), 0.55f);
        }

        // Lathe-turned metal throws its highlight as a band through the centre,
        // across the direction of the light.
        for (const float side : { 1.0f, -1.0f })
        {
            juce::Path wedge;
            const float spread = 0.42f;
            const float a0 = lightAngle + (side > 0.0f ? 0.0f : juce::MathConstants<float>::pi);
            wedge.startNewSubPath (c);
            wedge.lineTo (c.getPointOnCircumference (faceR * 1.1f, a0 - spread));
            wedge.lineTo (c.getPointOnCircumference (faceR * 1.1f, a0 + spread));
            wedge.closeSubPath();
            juce::ColourGradient streak (juce::Colours::white.withAlpha (side > 0.0f ? 0.42f : 0.18f),
                                         c.getPointOnCircumference (faceR, a0),
                                         juce::Colours::white.withAlpha (0.0f), c, true);
            g.setGradientFill (streak);
            g.fillPath (wedge);
        }
    }
    g.setColour (espresso.withAlpha (0.25f));
    g.drawEllipse (face.reduced (0.3f), 0.6f);

    // The index: an engraved line inlaid with lit cobalt, out to the face's edge.
    const auto indexColour = tinted (active ? cobaltLight : cobalt, 1.0f);
    {
        const juce::Line<float> index (c.getPointOnCircumference (R * 0.42f, angle), c.getPointOnCircumference (faceR - 1.2f, angle));
        g.setColour (electric.withAlpha (active ? 0.4f : 0.25f));
        g.drawLine (index, 4.2f);
        g.setColour (espresso.withAlpha (0.55f));
        g.drawLine (index, 2.4f);
        g.setColour (indexColour.interpolatedWith (juce::Colours::white, active ? 0.45f : 0.2f));
        g.drawLine (index, 1.3f);
    }

    // Sapphire glass cap, the index glowing through it.
    const float capR = R * 0.3f;
    const auto cap = juce::Rectangle<float> (2.0f * capR, 2.0f * capR).withCentre (c);
    g.setGradientFill (juce::ColourGradient (tinted (champagneLight, 0.5f), cap.getX(), cap.getY(),
                                             tinted (champagneDeep, 0.5f).darker (0.3f), cap.getRight(), cap.getBottom(), false));
    g.fillEllipse (cap.expanded (1.3f));
    juce::ColourGradient glass (tinted (cobaltDeep, 0.8f), c.x - capR * 0.3f, c.y - capR * 0.35f,
                                tinted (sapphire, 0.8f).darker (0.35f), c.x + capR, c.y + capR, true);
    glass.addColour (0.6, tinted (sapphire, 0.8f));
    g.setGradientFill (glass);
    g.fillEllipse (cap);
    {
        const juce::Line<float> filament (c.getPointOnCircumference (capR * 0.15f, angle), c.getPointOnCircumference (capR * 0.92f, angle));
        g.setColour (electric.withAlpha (0.45f));
        g.drawLine (filament, 2.6f);
        g.setColour (indexColour.interpolatedWith (juce::Colours::white, 0.55f));
        g.drawLine (filament, 1.0f);
    }
    g.setColour (juce::Colours::white.withAlpha (0.7f));
    g.fillEllipse (juce::Rectangle<float> (capR * 0.62f, capR * 0.36f).withCentre ({ c.x - capR * 0.3f, c.y - capR * 0.5f }));
    juce::Path crescent;
    crescent.addCentredArc (c.x, c.y, capR * 0.78f, capR * 0.78f, 0.0f, 1.9f, 3.1f, true);
    g.setColour (cobaltLight.withAlpha (0.35f));
    g.strokePath (crescent, juce::PathStrokeType (0.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Caption in the gap at the foot of the ring, or the value while touched.
    const auto text = active ? getTextFromValue (getValue()) : caption.toUpperCase();
    const auto textBox = juce::Rectangle<float> ((float) getWidth(), 14.0f).withPosition (0.0f, c.y + R + 6.0f);
    g.setColour (active ? espresso : ink (0.62f));
    g.setFont (fonts::sansMedium (9.5f, active ? 0.04f : 0.16f));
    g.drawText (text, textBox, juce::Justification::centred, false);
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

//==============================================================================
LinenToggle::LinenToggle()
    : ticker (this, [this] (double now)
      {
          if (! flipping)
              return;
          if (flipStart <= 0.0)
              flipStart = now;
          const float t = juce::jlimit (0.0f, 1.0f, (float) ((now - flipStart) / 0.08));
          shown = from + (to - from) * (t * t * (3.0f - 2.0f * t));
          flipping = t < 1.0f;
          repaint();
      })
{
    setClickingTogglesState (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void LinenToggle::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    using namespace palette;

    // Follow the state: flip over when it changes (by click or automation).
    const float target = getToggleState() ? 1.0f : 0.0f;
    if (shown < 0.0f)
        shown = to = target;
    else if (! juce::exactlyEqual (target, to))
    {
        from = shown;
        to = target;
        flipStart = 0.0;
        flipping = true;
    }

    const auto b = getLocalBounds().toFloat();
    const float unit = juce::jmin (b.getWidth(), b.getHeight() * 0.68f);
    const auto pivot = b.getCentre();

    // Washer and hex nut.
    const float washerR = unit * 0.46f, nutR = unit * 0.36f;
    g.setColour (espresso.withAlpha (0.22f));
    g.fillEllipse (juce::Rectangle<float> (2.0f * washerR, 2.0f * washerR).withCentre (pivot).translated (0.6f, 1.2f));
    g.setGradientFill (juce::ColourGradient (chromeLight, pivot.x - washerR, pivot.y - washerR, chromeDark, pivot.x + washerR, pivot.y + washerR, false));
    g.fillEllipse (juce::Rectangle<float> (2.0f * washerR, 2.0f * washerR).withCentre (pivot));

    juce::Path nut;
    for (int i = 0; i < 6; ++i)
    {
        const auto v = pivot.getPointOnCircumference (nutR, juce::MathConstants<float>::pi / 6.0f + (float) i * juce::MathConstants<float>::pi / 3.0f);
        i == 0 ? nut.startNewSubPath (v) : nut.lineTo (v);
    }
    nut.closeSubPath();
    g.setGradientFill (juce::ColourGradient (chromeLight, pivot.x - nutR, pivot.y - nutR, chrome.darker (0.25f), pivot.x + nutR, pivot.y + nutR, false));
    g.fillPath (nut);
    g.setColour (chromeDark.withAlpha (0.8f));
    g.strokePath (nut, juce::PathStrokeType (0.6f));
    g.setColour (espresso.withAlpha (0.85f));
    g.fillEllipse (juce::Rectangle<float> (unit * 0.3f, unit * 0.3f).withCentre (pivot));

    // The lever leans about 35 degrees off the panel, up for IN, down for OUT;
    // as it flips it swings through pointing straight out at you.
    const float tilt = (shown * 2.0f - 1.0f) * juce::degreesToRadians (35.0f);
    const float length = unit * 1.5f;
    const auto tip = pivot.translated (0.0f, -length * std::sin (tilt));
    const float closeness = 1.0f + 0.25f * std::cos (tilt);   // closer to you, a little larger
    const float tipW = unit * 0.56f * closeness, tipH = unit * 0.46f * closeness;

    // Its shadow, cast down and to the right.
    g.setColour (espresso.withAlpha (0.2f));
    {
        juce::Path shade;
        shade.addLineSegment ({ pivot.translated (1.0f, 2.0f), tip.translated (unit * 0.28f, unit * 0.5f) }, unit * 0.24f);
        g.fillPath (shade);
        g.fillEllipse (juce::Rectangle<float> (tipW, tipH).withCentre (tip.translated (unit * 0.3f, unit * 0.55f)));
    }

    // Shaft: chrome, a bright streak down its middle.
    {
        const float w0 = unit * 0.2f, w1 = unit * 0.26f * closeness;
        juce::Path shaft;
        shaft.startNewSubPath (pivot.x - w0 * 0.5f, pivot.y);
        shaft.lineTo (tip.x - w1 * 0.5f, tip.y);
        shaft.lineTo (tip.x + w1 * 0.5f, tip.y);
        shaft.lineTo (pivot.x + w0 * 0.5f, pivot.y);
        shaft.closeSubPath();
        juce::ColourGradient metal (chromeDark, pivot.x - w1 * 0.5f, pivot.y, chromeDark.darker (0.2f), pivot.x + w1 * 0.5f, pivot.y, false);
        metal.addColour (0.3, chromeLight);
        metal.addColour (0.55, chrome);
        g.setGradientFill (metal);
        g.fillPath (shaft);
    }

    // The bat: a rounded end, lit from the top left.
    const auto bat = juce::Rectangle<float> (tipW, tipH).withCentre (tip);
    juce::ColourGradient batMetal (juce::Colours::white, bat.getX() + tipW * 0.3f, bat.getY() + tipH * 0.25f,
                                   chromeDark.darker (0.3f), bat.getRight(), bat.getBottom(), true);
    batMetal.addColour (0.35, chromeLight);
    batMetal.addColour (0.7, chrome);
    g.setGradientFill (batMetal);
    g.fillEllipse (bat);
    g.setColour (chromeDark.withAlpha (0.9f));
    g.drawEllipse (bat, 0.6f);

    if (highlighted || down)
    {
        g.setColour (cobalt.withAlpha (down ? 0.35f : 0.18f));
        g.drawEllipse (juce::Rectangle<float> (2.0f * washerR + 3.0f, 2.0f * washerR + 3.0f).withCentre (pivot), 1.0f);
    }
}

//==============================================================================
LinenValuePill::LinenValuePill()
    : juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox)
{
    setMouseDragSensitivity (180);
    setVelocityModeParameters (0.6, 1, 0.0, true, juce::ModifierKeys::shiftModifier);
    setPopupDisplayEnabled (false, false, nullptr);
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
}

void LinenValuePill::mouseEnter (const juce::MouseEvent& e)
{
    hovering = true;
    juce::Slider::mouseEnter (e);
    repaint();
}

void LinenValuePill::mouseExit (const juce::MouseEvent& e)
{
    hovering = false;
    juce::Slider::mouseExit (e);
    repaint();
}

void LinenValuePill::paint (juce::Graphics& g)
{
    using namespace palette;

    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    const float radius = r.getHeight() * 0.5f;
    const bool active = hovering || isMouseButtonDown();

    g.setColour (active ? ivory : ivory.withAlpha (0.65f));
    g.fillRoundedRectangle (r, radius);
    g.setColour (active ? champagneDeep.withAlpha (0.8f) : rim);
    g.drawRoundedRectangle (r, radius, 0.8f);

    // A fine line along the bottom shows where the value sits in its range.
    const float proportion = (float) valueToProportionOfLength (getValue());
    const float inset = radius * 0.8f;
    g.setColour (active ? cobalt.withAlpha (0.8f) : champagneDeep.withAlpha (0.55f));
    g.fillRoundedRectangle (r.getX() + inset, r.getBottom() - 3.5f, (r.getWidth() - inset * 2.0f) * proportion, 1.4f, 0.7f);

    g.setColour (espresso);
    g.setFont (fonts::sansMedium (10.0f, 0.06f));
    g.drawText (getTextFromValue (getValue()), r.reduced (8.0f, 0.0f).withTrimmedBottom (1.0f), juce::Justification::centred, false);
}

//==============================================================================
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
    g.drawText (parameter.getCurrentValueAsText(), r.reduced (10.0f, 0.0f), juce::Justification::centred, false);
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
