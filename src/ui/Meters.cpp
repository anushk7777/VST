#include "Meters.h"
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
            g.setGradientFill (juce::ColourGradient (electric, fill.getX(), fill.getY(),
                                                     cobaltDeep, fill.getX(), fill.getBottom(), false));
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

} // namespace linen
