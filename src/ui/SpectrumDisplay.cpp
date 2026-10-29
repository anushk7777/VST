#include "SpectrumDisplay.h"
#include "LinenLookAndFeel.h"
#include "Palette.h"

namespace linen
{

namespace
{
    constexpr float minHz = 20.0f, maxHz = 20000.0f;
    constexpr float floorDb = -96.0f, ceilingDb = -6.0f;
    constexpr float maxVeilDb = 18.0f;       // cut that reaches the veil's full depth
    constexpr float veilDepth = 0.55f;       // ...as a fraction of the plot height
    constexpr float eqRangeDb = 15.0f;       // EQ curve: ±15 dB...
    constexpr float eqSpan = 0.42f;          // ...spans this much of the plot either side of its middle

    float eqHzAt (int point, int numPoints)
    {
        return minHz * std::pow (maxHz / minHz, (float) point / (float) (numPoints - 1));
    }

    /** A smooth curve through every point (Catmull-Rom, as cubic Béziers). */
    juce::Path splineThrough (const std::vector<juce::Point<float>>& pts)
    {
        juce::Path path;
        if (pts.empty())
            return path;

        path.startNewSubPath (pts.front());
        for (size_t i = 0; i + 1 < pts.size(); ++i)
        {
            const auto p0 = pts[i > 0 ? i - 1 : i];
            const auto p1 = pts[i];
            const auto p2 = pts[i + 1];
            const auto p3 = pts[i + 2 < pts.size() ? i + 2 : i + 1];
            path.cubicTo (p1 + (p2 - p0) / 6.0f, p2 - (p3 - p1) / 6.0f, p2);
        }
        return path;
    }
}

SpectrumDisplay::SpectrumDisplay (DeharshDisplay& displaySource, juce::RangedAudioParameter& focusLow, juce::RangedAudioParameter& focusHigh)
    : source (displaySource),
      focusLowParam (focusLow),
      focusHighParam (focusHigh),
      focusLowAttachment (focusLow, [this] (float v) { focusLowHz = v; repaint(); }),
      focusHighAttachment (focusHigh, [this] (float v) { focusHighHz = v; repaint(); }),
      vblank (this, [this] (double t) { tick (t); })
{
    level.fill (floorDb);
    cut.fill (0.0f);
    focusLowAttachment.sendInitialUpdate();
    focusHighAttachment.sendInitialUpdate();
    setOpaque (false);
}

void SpectrumDisplay::resized()
{
    plot = getLocalBounds().toFloat().reduced (22.0f, 18.0f).withTrimmedBottom (14.0f);
}

float SpectrumDisplay::xForHz (float hz) const
{
    return plot.getX() + plot.getWidth() * std::log (juce::jlimit (minHz, maxHz, hz) / minHz) / std::log (maxHz / minHz);
}

float SpectrumDisplay::hzForX (float x) const
{
    return minHz * std::pow (maxHz / minHz, juce::jlimit (0.0f, 1.0f, (x - plot.getX()) / plot.getWidth()));
}

float SpectrumDisplay::yForLevel (float db) const
{
    const float t = (juce::jlimit (floorDb, ceilingDb, db) - floorDb) / (ceilingDb - floorDb);
    return plot.getBottom() - t * plot.getHeight();
}

float SpectrumDisplay::yForCut (float db) const
{
    return plot.getY() + juce::jlimit (0.0f, 1.0f, db / maxVeilDb) * plot.getHeight() * veilDepth;
}

float SpectrumDisplay::yForEq (float db) const
{
    return plot.getCentreY() - juce::jlimit (-1.0f, 1.0f, db / eqRangeDb) * plot.getHeight() * eqSpan;
}

void SpectrumDisplay::updateEqCurve()
{
    eqVisible = false;
    for (int i = 0; i < numEqPoints; ++i)
    {
        eqDb[(size_t) i] = eqResponse != nullptr ? eqResponse (eqHzAt (i, numEqPoints)) : 0.0f;
        eqVisible = eqVisible || std::abs (eqDb[(size_t) i]) > 0.05f;
    }
    repaint();
}

void SpectrumDisplay::tick (double timestampSeconds)
{
    const float dt = lastTick > 0.0 ? (float) juce::jlimit (0.0, 0.1, timestampSeconds - lastTick) : 1.0f / 60.0f;
    lastTick = timestampSeconds;

    // Quick to rise (15 ms), slow and viscous to fall (350 ms).
    const float rise = 1.0f - std::exp (-dt / 0.015f);
    const float fall = 1.0f - std::exp (-dt / 0.35f);

    bool moving = false;
    for (int p = 0; p < DeharshDisplay::numPoints; ++p)
    {
        const float targetLevel = source.levelDb[(size_t) p].load (std::memory_order_relaxed);
        const float targetCut = source.cutDb[(size_t) p].load (std::memory_order_relaxed);

        auto& l = level[(size_t) p];
        auto& c = cut[(size_t) p];
        const float before = l + c;
        l += (targetLevel - l) * (targetLevel > l ? rise : fall);
        c += (targetCut - c) * (targetCut > c ? rise : fall);
        moving = moving || std::abs (l + c - before) > 0.01f;
    }

    if (eqDirty.exchange (false))
        updateEqCurve();

    const float pitch = source.pitchHz.load (std::memory_order_relaxed);
    if (pitch > 0.0f)
        pitchHz = pitchHz > 0.0f ? pitchHz + (pitch - pitchHz) * rise : pitch;
    const float targetAlpha = pitch > 0.0f ? 1.0f : 0.0f;
    pitchAlpha += (targetAlpha - pitchAlpha) * fall;

    // Context: Pain, stress, Listen. The backlight follows the signal.
    nowSeconds = timestampSeconds;
    const auto previous = context;
    if (contextSource != nullptr)
        context = contextSource();

    const float loudest = *std::max_element (level.begin(), level.end());
    const float targetBacklight = juce::jlimit (0.0f, 1.0f, (loudest + 70.0f) / 50.0f);
    backlight += (targetBacklight - backlight) * fall;
    listenGlow += ((context.listening >= 0.0f ? 1.0f : 0.0f) - listenGlow) * (1.0f - std::exp (-dt / 0.2f));

    const bool contextMoving = std::abs (context.stress - previous.stress) > 0.002f || std::abs (context.pain - previous.pain) > 0.002f
                            || context.stress > 0.02f || context.listening >= 0.0f || listenGlow > 0.01f;
    const bool showingMessage = nowSeconds - messageSince < 12.0;

    if (moving || pitchAlpha > 0.01f || contextMoving || showingMessage)
        repaint();
}

void SpectrumDisplay::showMessage (const juce::String& text, bool good)
{
    message = text;
    messageGood = good;
    messageSince = nowSeconds;
    repaint();
}

void SpectrumDisplay::paint (juce::Graphics& g)
{
    using namespace palette;

    const auto bounds = getLocalBounds().toFloat();
    juce::Path wellPath;
    wellPath.addRoundedRectangle (bounds, 14.0f);
    g.setColour (well);
    g.fillPath (wellPath);

    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (wellPath);

        // Backlight: a warm glow from below, like a lit VU window, brightening
        // with the signal; it turns cobalt while Listen is listening.
        const auto warm = ivory.interpolatedWith (champagneLight, 0.35f);
        const auto light = warm.interpolatedWith (cobaltLight, 0.55f * listenGlow);
        juce::ColourGradient glow (light.withAlpha (0.55f + 0.35f * backlight), bounds.getCentreX(), bounds.getBottom() + bounds.getHeight() * 0.15f,
                                   light.withAlpha (0.0f), bounds.getCentreX(), bounds.getY() - bounds.getHeight() * 0.2f, true);
        g.setGradientFill (glow);
        g.fillRect (bounds);

        // Glass: a soft reflection across the top of the well.
        const auto glass = bounds.withHeight (bounds.getHeight() * 0.3f);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.35f), 0.0f, glass.getY(),
                                                 juce::Colours::white.withAlpha (0.0f), 0.0f, glass.getBottom(), false));
        g.fillRect (glass);
    }
    wellShadow.render (g, wellPath);

    // Frequency grid.
    g.setFont (fonts::sans (9.0f, 0.12f));
    for (const float hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = xForHz (hz);
        g.setColour (ink (0.06f));
        g.fillRect (juce::Rectangle<float> (x, plot.getY(), 1.0f, plot.getHeight()));
        g.setColour (ink (0.36f));
        const auto label = hz >= 1000.0f ? juce::String ((int) (hz / 1000.0f)) + "k" : juce::String ((int) hz);
        g.drawText (label, juce::Rectangle<float> (x - 20.0f, plot.getBottom() + 3.0f, 40.0f, 12.0f), juce::Justification::centred);
    }

    juce::Graphics::ScopedSaveState clip (g);
    g.reduceClipRegion (plot.toNearestInt().expanded (0, 2));

    // Under Pain stress, the 1.5-4.5 kHz points tremble.
    const auto jitter = [this] (float hz)
    {
        if (context.stress < 0.02f || hz < 1500.0f || hz > 4500.0f)
            return 0.0f;
        return (tremor.nextFloat() * 2.0f - 1.0f) * 3.5f * context.stress;
    };

    // Spectrum: a soft ink line over a faint wash.
    std::vector<juce::Point<float>> spectrumPts, veilPts;
    spectrumPts.reserve (DeharshDisplay::numPoints);
    veilPts.reserve (DeharshDisplay::numPoints);
    for (int p = 0; p < DeharshDisplay::numPoints; ++p)
    {
        const float hz = DeharshDisplay::frequencyOf (p);
        const float x = xForHz (hz);
        spectrumPts.push_back ({ x, yForLevel (level[(size_t) p]) + jitter (hz) });
        veilPts.push_back ({ x, yForCut (cut[(size_t) p]) + 0.6f * jitter (hz) });
    }

    auto spectrum = splineThrough (spectrumPts);
    auto wash = spectrum;
    wash.lineTo (plot.getRight(), plot.getBottom() + 2.0f);
    wash.lineTo (plot.getX(), plot.getBottom() + 2.0f);
    wash.closeSubPath();
    g.setGradientFill (juce::ColourGradient (ink (0.10f), 0.0f, plot.getY(), ink (0.03f), 0.0f, plot.getBottom(), false));
    g.fillPath (wash);
    g.setColour (ink (0.36f));
    g.strokePath (spectrum, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // The EQ shape: a cobalt line about a faint 0 dB rule, with a whisper of
    // light between them, only while some band is boosting or cutting.
    if (eqVisible)
    {
        std::vector<juce::Point<float>> eqPts;
        eqPts.reserve (numEqPoints);
        for (int i = 0; i < numEqPoints; ++i)
            eqPts.push_back ({ xForHz (eqHzAt (i, numEqPoints)), yForEq (eqDb[(size_t) i]) });

        const float zero = yForEq (0.0f);
        auto curve = splineThrough (eqPts);
        juce::Path area (curve);
        area.lineTo (plot.getRight(), zero);
        area.lineTo (plot.getX(), zero);
        area.closeSubPath();

        g.setColour (cobalt.withAlpha (0.07f));
        g.fillPath (area);
        g.setColour (cobalt.withAlpha (0.18f));
        g.fillRect (juce::Rectangle<float> (plot.getX(), zero - 0.5f, plot.getWidth(), 1.0f));
        g.setColour (cobalt.withAlpha (0.75f));
        g.strokePath (curve, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // The silk veil: gain reduction hanging from the top edge, champagne to
    // terracotta, deepening toward crimson as Pain rises.
    auto veilEdge = splineThrough (veilPts);
    juce::Path veil (veilEdge);
    veil.lineTo (plot.getRight(), plot.getY() - 2.0f);
    veil.lineTo (plot.getX(), plot.getY() - 2.0f);
    veil.closeSubPath();

    const float painTint = juce::jlimit (0.0f, 1.0f, context.pain);
    const auto deep = terracotta.interpolatedWith (crimsonDeep, painTint);
    const float fullDepth = yForCut (maxVeilDb);
    juce::ColourGradient silk (champagneLight.withAlpha (0.35f), 0.0f, plot.getY(), deep.withAlpha (0.66f), 0.0f, fullDepth, false);
    silk.addColour (0.3, champagne.interpolatedWith (crimson, painTint * 0.5f).withAlpha (0.5f));
    g.setGradientFill (silk);
    g.fillPath (veil);

    // The hem: a faint shadow just below it, then the edge itself, both
    // fading away when nothing is being cut.
    const float deepest = *std::max_element (cut.begin(), cut.end());
    const float hemAlpha = juce::jlimit (0.0f, 1.0f, deepest / 2.0f);
    g.setColour (espresso.withAlpha (0.10f * hemAlpha));
    g.strokePath (veilEdge, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved), juce::AffineTransform::translation (0.0f, 1.6f));
    g.setColour (deep.withAlpha (0.25f + 0.6f * hemAlpha));
    g.strokePath (veilEdge, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Areas outside the focus range are veiled in linen.
    const float xLow = xForHz (focusLowHz), xHigh = xForHz (focusHighHz);
    g.setColour (surface.withAlpha (0.45f));
    g.fillRect (juce::Rectangle<float>::leftTopRightBottom (plot.getX() - 2.0f, plot.getY() - 2.0f, xLow, plot.getBottom() + 2.0f));
    g.fillRect (juce::Rectangle<float>::leftTopRightBottom (xHigh, plot.getY() - 2.0f, plot.getRight() + 2.0f, plot.getBottom() + 2.0f));

    // The protected fundamental: a small champagne mark at the floor.
    if (pitchAlpha > 0.01f && pitchHz > 0.0f)
    {
        const float x = xForHz (pitchHz);
        juce::Path diamond;
        diamond.addTriangle (x - 4.0f, plot.getBottom() - 6.0f, x + 4.0f, plot.getBottom() - 6.0f, x, plot.getBottom() - 12.0f);
        diamond.addTriangle (x - 4.0f, plot.getBottom() - 6.0f, x + 4.0f, plot.getBottom() - 6.0f, x, plot.getBottom());
        g.setColour (champagneDeep.withAlpha (0.85f * pitchAlpha));
        g.fillPath (diamond);
        g.setColour (ink (0.55f * pitchAlpha));
        g.setFont (fonts::sans (11.0f, 0.06f));
        g.drawText ("body", juce::Rectangle<float> (x + 7.0f, plot.getBottom() - 16.0f, 40.0f, 14.0f), juce::Justification::centredLeft);
    }

    // Focus handles: champagne, lit cobalt while touched.
    for (const auto& [handle, x] : { std::pair { Handle::low, xLow }, std::pair { Handle::high, xHigh } })
    {
        const bool active = hovered == handle || dragging == handle;
        g.setColour (active ? cobalt.withAlpha (0.85f) : champagneDeep.withAlpha (0.55f));
        g.fillRect (juce::Rectangle<float> (x - 0.5f, plot.getY(), 1.0f, plot.getHeight()));

        const auto knob = juce::Rectangle<float> (active ? 11.0f : 9.0f, active ? 11.0f : 9.0f).withCentre ({ x, plot.getY() + 8.0f });
        g.setGradientFill (juce::ColourGradient (champagneLight, knob.getX(), knob.getY(), champagne, knob.getRight(), knob.getBottom(), false));
        g.fillEllipse (knob);
        g.setColour (active ? cobalt : champagneDeep);
        g.drawEllipse (knob, active ? 1.4f : 0.8f);
    }

    // Listening: a calm caption with the progress.
    if (listenGlow > 0.01f)
    {
        const auto box = plot.withSizeKeepingCentre (300.0f, 30.0f);
        g.setColour (ivory.withAlpha (0.85f * listenGlow));
        g.fillRoundedRectangle (box, 15.0f);
        g.setColour (cobalt.withAlpha (0.6f * listenGlow));
        g.drawRoundedRectangle (box, 15.0f, 1.0f);
        g.setColour (cobaltDeep.withAlpha (listenGlow));
        g.setFont (fonts::sans (14.0f, 0.04f));
        const int percent = juce::roundToInt (juce::jmax (0.0f, context.listening) * 100.0f);
        g.drawText ("listening to your voice  " + juce::String (percent) + "%", box, juce::Justification::centred);
    }

    // What Listen did, fading out after a while.
    const double age = nowSeconds - messageSince;
    if (message.isNotEmpty() && age < 12.0)
    {
        const float alpha = (float) juce::jlimit (0.0, 1.0, juce::jmin (age / 0.3, (12.0 - age) / 1.5));
        const auto box = plot.withTrimmedTop (plot.getHeight() - 46.0f).reduced (40.0f, 4.0f);
        g.setColour (ivory.withAlpha (0.9f * alpha));
        g.fillRoundedRectangle (box, 12.0f);
        g.setColour ((messageGood ? cobalt : champagneDeep).withAlpha (0.5f * alpha));
        g.drawRoundedRectangle (box, 12.0f, 1.0f);
        g.setColour (espresso.withAlpha (alpha));
        g.setFont (fonts::sans (12.5f, 0.04f));
        g.drawFittedText (message, box.reduced (14.0f, 2.0f).toNearestInt(), juce::Justification::centred, 2);
    }
}

SpectrumDisplay::Handle SpectrumDisplay::handleAt (juce::Point<float> p) const
{
    const float dLow = std::abs (p.x - xForHz (focusLowHz));
    const float dHigh = std::abs (p.x - xForHz (focusHighHz));
    if (juce::jmin (dLow, dHigh) > 8.0f || ! plot.expanded (0.0f, 10.0f).contains (p))
        return Handle::none;
    return dLow <= dHigh ? Handle::low : Handle::high;
}

void SpectrumDisplay::mouseMove (const juce::MouseEvent& e)
{
    const auto h = handleAt (e.position);
    if (h != hovered)
    {
        hovered = h;
        setMouseCursor (h == Handle::none ? juce::MouseCursor::NormalCursor : juce::MouseCursor::LeftRightResizeCursor);
        setTooltip (h == Handle::low  ? "Focus low: De-harsh ignores everything below this"
                  : h == Handle::high ? "Focus high: De-harsh ignores everything above this" : juce::String());
        repaint();
    }
}

void SpectrumDisplay::mouseExit (const juce::MouseEvent&)
{
    hovered = Handle::none;
    repaint();
}

void SpectrumDisplay::mouseDown (const juce::MouseEvent& e)
{
    dragging = handleAt (e.position);
    if (dragging == Handle::low)  focusLowAttachment.beginGesture();
    if (dragging == Handle::high) focusHighAttachment.beginGesture();
}

void SpectrumDisplay::mouseDrag (const juce::MouseEvent& e)
{
    const float hz = hzForX (e.position.x);
    if (dragging == Handle::low)
        focusLowAttachment.setValueAsPartOfGesture (juce::jmin (hz, focusHighHz * 0.7f));
    else if (dragging == Handle::high)
        focusHighAttachment.setValueAsPartOfGesture (juce::jmax (hz, focusLowHz * 1.4f));
}

void SpectrumDisplay::mouseUp (const juce::MouseEvent&)
{
    if (dragging == Handle::low)  focusLowAttachment.endGesture();
    if (dragging == Handle::high) focusHighAttachment.endGesture();
    dragging = Handle::none;
    repaint();
}

void SpectrumDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto h = handleAt (e.position);
    auto& param = h == Handle::low ? focusLowParam : focusHighParam;
    auto& attachment = h == Handle::low ? focusLowAttachment : focusHighAttachment;
    if (h != Handle::none)
        attachment.setValueAsCompleteGesture (param.convertFrom0to1 (param.getDefaultValue()));
}

} // namespace linen
