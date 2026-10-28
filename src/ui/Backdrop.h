#pragma once

#include <juce_graphics/juce_graphics.h>

namespace linen
{

/** The background: alabaster with a fine linen warp/weft, a little fibre
    grain and a soft top-lit vignette. Rendered once per size at physical
    pixel resolution (so it stays crisp on high-DPI screens), never per frame. */
class Backdrop
{
public:
    /** Draws the cached linen into area, re-rendering only if the physical size changed. */
    void draw (juce::Graphics& g, juce::Rectangle<int> area);

    /** Pain's scorch: warms and darkens the outer edge, like hot iron (0..1). */
    static void drawScorch (juce::Graphics& g, juce::Rectangle<float> area, float amount);

    static juce::Image render (int physicalWidth, int physicalHeight, float pixelScale);

private:
    juce::Image cache;
};

} // namespace linen
