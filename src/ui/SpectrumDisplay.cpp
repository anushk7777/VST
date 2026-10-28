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

    const float pitch = source.pitchHz.load (std::memory_order_relaxed);
    if (pitch > 0.0f)
        pitchHz = pitchHz > 0.0f ? pitchHz + (pitch - pitchHz) * rise : pitch;
    const float targetAlpha = pitch > 0.0f ? 1.0f : 0.0f;
    pitchAlpha += (targetAlpha - pitchAlpha) * fall;

    if (moving || pitchAlpha > 0.01f)
        repaint();
}

void SpectrumDisplay::paint (juce::Graphics& g)
{
    using namespace palette;

    const auto well = getLocalBounds().toFloat();
    juce::Path wellPath;
    wellPath.addRoundedRectangle (well, 14.0f);
    g.setColour (cashmere);
    g.fillPath (wellPath);
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

    // Spectrum: a soft ink line over a faint wash.
    std::vector<juce::Point<float>> spectrumPts, veilPts;
    spectrumPts.reserve (DeharshDisplay::numPoints);
    veilPts.reserve (DeharshDisplay::numPoints);
    for (int p = 0; p < DeharshDisplay::numPoints; ++p)
    {
        const float x = xForHz (DeharshDisplay::frequencyOf (p));
        spectrumPts.push_back ({ x, yForLevel (level[(size_t) p]) });
        veilPts.push_back ({ x, yForCut (cut[(size_t) p]) });
    }

    auto spectrum = splineThrough (spectrumPts);
    auto wash = spectrum;
    wash.lineTo (plot.getRight(), plot.getBottom() + 2.0f);
    wash.lineTo (plot.getX(), plot.getBottom() + 2.0f);
    wash.closeSubPath();
    g.setGradientFill (juce::ColourGradient (ink (0.10f), 0.0f, plot.getY(), ink (0.03f), 0.0f, plot.getBottom(), false));
    g.fillPath (wash);
    g.setColour (ink (0.34f));
    g.strokePath (spectrum, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // The silk veil: gain reduction hanging from the top edge.
    auto veilEdge = splineThrough (veilPts);
    juce::Path veil (veilEdge);
    veil.lineTo (plot.getRight(), plot.getY() - 2.0f);
    veil.lineTo (plot.getX(), plot.getY() - 2.0f);
    veil.closeSubPath();

    const float fullDepth = yForCut (maxVeilDb);
    juce::ColourGradient silk (champagneLight.withAlpha (0.35f), 0.0f, plot.getY(),
                               terracotta.withAlpha (0.72f), 0.0f, fullDepth, false);
    silk.addColour (0.3, champagne.withAlpha (0.55f));
    g.setGradientFill (silk);
    g.fillPath (veil);

    // The hem: a faint shadow just below it, then the edge itself, both
    // fading away when nothing is being cut.
    const float deepest = *std::max_element (cut.begin(), cut.end());
    const float hemAlpha = juce::jlimit (0.0f, 1.0f, deepest / 2.0f);
    g.setColour (espresso.withAlpha (0.10f * hemAlpha));
    g.strokePath (veilEdge, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved), juce::AffineTransform::translation (0.0f, 1.6f));
    g.setColour (terracotta.withAlpha (0.25f + 0.6f * hemAlpha));
    g.strokePath (veilEdge, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Areas outside the focus range are veiled in alabaster.
    const float xLow = xForHz (focusLowHz), xHigh = xForHz (focusHighHz);
    g.setColour (alabaster.withAlpha (0.45f));
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
        g.setFont (fonts::serifItalic (13.0f));
        g.drawText ("body", juce::Rectangle<float> (x + 7.0f, plot.getBottom() - 16.0f, 40.0f, 14.0f), juce::Justification::centredLeft);
    }

    // Focus handles.
    for (const auto& [handle, x] : { std::pair { Handle::low, xLow }, std::pair { Handle::high, xHigh } })
    {
        const bool active = hovered == handle || dragging == handle;
        g.setColour (champagneDeep.withAlpha (active ? 0.9f : 0.55f));
        g.fillRect (juce::Rectangle<float> (x - 0.5f, plot.getY(), 1.0f, plot.getHeight()));

        const auto knob = juce::Rectangle<float> (active ? 11.0f : 9.0f, active ? 11.0f : 9.0f).withCentre ({ x, plot.getY() + 8.0f });
        g.setGradientFill (juce::ColourGradient (champagneLight, knob.getX(), knob.getY(), champagne, knob.getRight(), knob.getBottom(), false));
        g.fillEllipse (knob);
        g.setColour (champagneDeep);
        g.drawEllipse (knob, 0.8f);
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
