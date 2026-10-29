#include "Meters.h"
#include "LinenLookAndFeel.h"
#include "Palette.h"

namespace linen
{

LinenMeter::LinenMeter (Style meterStyle, std::function<float()> valueSource, float scale)
    : style (meterStyle),
      source (std::move (valueSource)),
      fullScale (scale),
      vblank (this, [this] (double t) { tick (t); })
{
    setInterceptsMouseClicks (true, false);
}

void LinenMeter::tick (double timestampSeconds)
{
    const float dt = lastTick > 0.0 ? (float) juce::jlimit (0.0, 0.1, timestampSeconds - lastTick) : 1.0f / 60.0f;
    lastTick = timestampSeconds;

    const float target = juce::jlimit (0.0f, 1.0f, source() / fullScale);
    const float coeff = 1.0f - std::exp (-dt / (target > shown ? 0.02f : 0.25f));
    const float next = shown + (target - shown) * coeff;

    if (std::abs (next - shown) > 0.001f)
    {
        shown = next;
        repaint();
    }
}

void LinenMeter::paint (juce::Graphics& g)
{
    using namespace palette;
    const auto r = getLocalBounds().toFloat();

    if (style == Style::bar)
    {
        const float radius = r.getWidth() * 0.5f;
        g.setColour (well);
        g.fillRoundedRectangle (r, radius);
        g.setColour (ink (0.12f));
        g.drawRoundedRectangle (r.reduced (0.5f), radius, 0.7f);

        if (shown > 0.0f)
        {
            const auto fill = r.reduced (1.5f).withHeight (juce::jmax (r.getWidth() - 3.0f, (r.getHeight() - 3.0f) * shown));
            g.setGradientFill (juce::ColourGradient (champagne, fill.getX(), fill.getY(),
                                                     terracotta, fill.getX(), fill.getBottom(), false));
            g.fillRoundedRectangle (fill, (fill.getWidth()) * 0.5f);
        }
        return;
    }

    // Dot: hairline ring, filling with an electric-blue glow.
    const auto dot = r.withSizeKeepingCentre (8.0f, 8.0f);
    if (shown > 0.01f)
    {
        g.setColour (electric.withAlpha (0.25f * shown));
        g.fillEllipse (dot.expanded (4.0f * shown));
    }
    g.setColour (well.interpolatedWith (electric, shown));
    g.fillEllipse (dot);
    g.setColour (cobaltDeep.withAlpha (0.7f));
    g.drawEllipse (dot, 0.8f);
}

//==============================================================================
namespace
{
    constexpr float vuMinDb = -20.0f, vuMaxDb = 3.0f;
    constexpr float vuSweep = 0.84f;   // radians either side of straight up

    /** Where a VU reading sits along the scale (0..1): the scale is linear in voltage. */
    float vuPosition (float db)
    {
        const auto volts = [] (float d) { return std::pow (10.0f, d / 20.0f); };
        return juce::jlimit (0.0f, 1.0f, (volts (db) - volts (vuMinDb)) / (volts (vuMaxDb) - volts (vuMinDb)));
    }

    float vuAngle (float db) { return -vuSweep + 2.0f * vuSweep * vuPosition (db); }
}

LinenVuMeter::LinenVuMeter (std::function<float()> totalDb, std::function<float()> tubeDb, std::function<float()> optoDb)
    : total (std::move (totalDb)),
      tube (std::move (tubeDb)),
      opto (std::move (optoDb)),
      vblank (this, [this] (double t) { tick (t); })
{
}

void LinenVuMeter::tick (double timestampSeconds)
{
    const float dt = lastTick > 0.0 ? (float) juce::jlimit (0.0, 1.0 / 30.0, timestampSeconds - lastTick) : 1.0f / 60.0f;
    lastTick = timestampSeconds;

    // A damped needle: ~300 ms to settle, the faintest overshoot, like a real VU.
    constexpr float omega = juce::MathConstants<float>::twoPi * 2.1f, damping = 0.78f;
    const float target = juce::jlimit (0.0f, 24.0f, total());
    velocity += (omega * omega * (target - needle) - 2.0f * damping * omega * velocity) * dt;
    needle += velocity * dt;

    const auto follow = [dt] (float shown, float value)
    {
        return shown + (value - shown) * (1.0f - std::exp (-dt / (value > shown ? 0.02f : 0.25f)));
    };
    const float nextTube = follow (tubeShown, juce::jmax (0.0f, tube()));
    const float nextOpto = follow (optoShown, juce::jmax (0.0f, opto()));

    if (std::abs (velocity) > 0.002f || std::abs (target - needle) > 0.002f
        || std::abs (nextTube - tubeShown) > 0.01f || std::abs (nextOpto - optoShown) > 0.01f)
    {
        tubeShown = nextTube;
        optoShown = nextOpto;
        repaint();
    }
}

void LinenVuMeter::paint (juce::Graphics& g)
{
    using namespace palette;

    auto area = getLocalBounds().toFloat();
    const auto bars = area.removeFromBottom (22.0f);
    const auto bezel = area.reduced (1.0f);

    // Champagne bezel, a black lip, then the backlit face.
    g.setColour (espresso.withAlpha (0.25f));
    g.fillRoundedRectangle (bezel.translated (1.5f, 3.0f), 9.0f);
    g.setGradientFill (juce::ColourGradient (champagneLight.brighter (0.1f), bezel.getX(), bezel.getY(),
                                             champagneDeep.darker (0.2f), bezel.getRight(), bezel.getBottom(), false));
    g.fillRoundedRectangle (bezel, 9.0f);
    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.drawRoundedRectangle (bezel.reduced (0.6f), 9.0f, 0.7f);

    const auto lip = bezel.reduced (4.0f);
    g.setColour (espresso.withAlpha (0.9f));
    g.fillRoundedRectangle (lip, 6.0f);
    const auto face = lip.reduced (2.0f);

    juce::Path facePath;
    facePath.addRoundedRectangle (face, 4.5f);
    const float working = juce::jlimit (0.0f, 1.0f, needle / 8.0f);
    {
        juce::ColourGradient backlight (vuFace.brighter (0.06f), face.getCentreX(), face.getBottom() - face.getHeight() * 0.2f,
                                        vuFace.darker (0.16f), face.getX(), face.getY(), true);
        g.setGradientFill (backlight);
        g.fillPath (facePath);

        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (facePath);
        if (working > 0.01f)
        {
            juce::ColourGradient lamp (electric.withAlpha (0.22f * working), face.getCentreX(), face.getBottom(),
                                       electric.withAlpha (0.0f), face.getCentreX(), face.getY(), true);
            g.setGradientFill (lamp);
            g.fillPath (facePath);
        }

        // The scale: an arc around a pivot below the face.
        const juce::Point<float> pivot (face.getCentreX(), face.getBottom() + face.getHeight() * 0.42f);
        const float arcR = face.getHeight() * 1.05f;

        juce::Path blackArc, redArc;
        blackArc.addCentredArc (pivot.x, pivot.y, arcR, arcR, 0.0f, vuAngle (vuMinDb), vuAngle (0.0f), true);
        redArc.addCentredArc (pivot.x, pivot.y, arcR + 1.5f, arcR + 1.5f, 0.0f, vuAngle (0.0f), vuAngle (vuMaxDb), true);
        g.setColour (vuInk);
        g.strokePath (blackArc, juce::PathStrokeType (1.0f));
        g.setColour (crimson);
        g.strokePath (redArc, juce::PathStrokeType (3.5f));

        const std::pair<float, const char*> marks[] { { -20.0f, "20" }, { -10.0f, "10" }, { -7.0f, "7" }, { -5.0f, "5" },
                                                       { -3.0f, "3" }, { -2.0f, "2" }, { -1.0f, "1" }, { 0.0f, "0" },
                                                       { 1.0f, "1" }, { 2.0f, "2" }, { 3.0f, "3" } };
        g.setFont (fonts::sansMedium (8.0f, 0.0f));
        for (const auto& [db, label] : marks)
        {
            const float a = vuAngle (db);
            g.setColour (db > 0.0f ? crimson : vuInk);
            g.drawLine ({ pivot.getPointOnCircumference (arcR, a), pivot.getPointOnCircumference (arcR + 6.0f, a) }, 1.1f);
            const auto at = pivot.getPointOnCircumference (arcR + 12.5f, a);
            g.drawText (label, juce::Rectangle<float> (16.0f, 9.0f).withCentre (at), juce::Justification::centred, false);
        }
        for (const float db : { -15.0f, -9.0f, -8.0f, -6.0f, -4.0f })
        {
            const float a = vuAngle (db);
            g.setColour (vuInk.withAlpha (0.7f));
            g.drawLine ({ pivot.getPointOnCircumference (arcR, a), pivot.getPointOnCircumference (arcR + 3.5f, a) }, 0.8f);
        }
        for (const float db : { -2.5f, -1.5f, -0.5f, 0.5f, 1.5f, 2.5f })
        {
            const float a = vuAngle (db);
            g.setColour (db > 0.0f ? crimson : vuInk.withAlpha (0.7f));
            g.drawLine ({ pivot.getPointOnCircumference (arcR, a), pivot.getPointOnCircumference (arcR + 3.5f, a) }, 0.8f);
        }

        // Legend.
        g.setColour (vuInk);
        g.setFont (fonts::serifMedium (15.0f, 0.12f));
        g.drawText ("ADC", juce::Rectangle<float> (face.getWidth(), 16.0f).withCentre ({ face.getCentreX(), face.getY() + face.getHeight() * 0.6f }),
                    juce::Justification::centred, false);
        g.setColour (vuInk.withAlpha (0.6f));
        g.setFont (fonts::sans (6.5f, 0.22f));
        g.drawText ("GAIN REDUCTION  dB", juce::Rectangle<float> (face.getWidth(), 9.0f).withCentre ({ face.getCentreX(), face.getY() + face.getHeight() * 0.75f }),
                    juce::Justification::centred, false);

        // The needle and its shadow on the face.
        const float a = vuAngle (-needle);
        const juce::Line<float> needleLine (pivot.getPointOnCircumference (face.getHeight() * 0.5f, a), pivot.getPointOnCircumference (arcR + 8.0f, a));
        g.setColour (espresso.withAlpha (0.16f));
        g.drawLine ({ needleLine.getStart().translated (2.0f, 3.0f), needleLine.getEnd().translated (2.0f, 3.0f) }, 1.6f);
        g.setColour (vuInk);
        g.drawLine (needleLine, 1.3f);

        // The pivot's cover plate.
        const float plateR = face.getHeight() * 0.62f;
        g.setGradientFill (juce::ColourGradient (espresso.brighter (0.25f), pivot.x, pivot.y - plateR, espresso, pivot.x, pivot.y, false));
        g.fillEllipse (juce::Rectangle<float> (2.0f * plateR, 2.0f * plateR).withCentre (pivot));
        g.setColour (champagne.withAlpha (0.8f));
        g.setFont (fonts::sans (5.5f, 0.3f));
        g.drawText ("ANUSHK DUA", juce::Rectangle<float> (face.getWidth(), 8.0f).withCentre ({ pivot.x, face.getBottom() - 6.0f }),
                    juce::Justification::centred, false);

        // Glass: a soft sheen across the top left, and the face's inner shadow.
        juce::Path sheen;
        sheen.startNewSubPath (face.getX(), face.getY());
        sheen.lineTo (face.getX() + face.getWidth() * 0.62f, face.getY());
        sheen.lineTo (face.getX(), face.getY() + face.getHeight() * 0.75f);
        sheen.closeSubPath();
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.32f), face.getX(), face.getY(),
                                                 juce::Colours::white.withAlpha (0.0f), face.getX() + face.getWidth() * 0.3f,
                                                 face.getY() + face.getHeight() * 0.38f, false));
        g.fillPath (sheen);
        g.setGradientFill (juce::ColourGradient (espresso.withAlpha (0.28f), face.getX(), face.getY(),
                                                 espresso.withAlpha (0.0f), face.getX(), face.getY() + 7.0f, false));
        g.fillRect (face.withHeight (7.0f));
    }

    // TUBE and OPTO: who is doing the work.
    const auto bar = [&] (juce::Rectangle<float> r, const juce::String& name, float db)
    {
        g.setColour (ink (0.55f));
        g.setFont (fonts::sansMedium (7.0f, 0.2f));
        g.drawText (name, r.removeFromLeft (30.0f), juce::Justification::centredLeft, false);
        constexpr int segments = 12;
        const float w = r.getWidth() / (float) segments;
        for (int i = 0; i < segments; ++i)
        {
            const auto seg = juce::Rectangle<float> (r.getX() + w * (float) i + 0.8f, r.getCentreY() - 1.6f, w - 1.6f, 3.2f);
            const float level = juce::jlimit (0.0f, 1.0f, db - (float) i);
            g.setColour (ink (0.16f));
            g.fillRoundedRectangle (seg, 1.2f);
            if (level > 0.0f)
            {
                g.setColour (electric.withAlpha (0.2f * level));
                g.fillRoundedRectangle (seg.expanded (1.2f, 1.6f), 2.0f);
                g.setColour (cobalt.interpolatedWith (ink (0.16f), 1.0f - level));
                g.fillRoundedRectangle (seg, 1.2f);
                g.setColour (juce::Colours::white.withAlpha (0.45f * level));
                g.fillRect (seg.reduced (seg.getWidth() * 0.2f, 1.2f));
            }
        }
    };
    auto rows = bars.reduced (6.0f, 2.0f);
    bar (rows.removeFromTop (rows.getHeight() * 0.5f), "TUBE", tubeShown);
    bar (rows, "OPTO", optoShown);
}

} // namespace linen
