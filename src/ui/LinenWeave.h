#pragma once

#include <juce_graphics/juce_graphics.h>

namespace linen
{

/** Renders the background: alabaster with a fine linen warp/weft, a little
    fibre grain and a soft top-lit vignette. Rendered once per size at physical
    pixel resolution (so it stays crisp on high-DPI screens), never per frame. */
class LinenWeave
{
public:
    /** Draws the cached weave into area, re-rendering only if the physical size changed. */
    void draw (juce::Graphics& g, juce::Rectangle<int> area);

    static juce::Image render (int physicalWidth, int physicalHeight, float pixelScale);

private:
    juce::Image cache;
};

} // namespace linen
