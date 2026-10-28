#pragma once

#include <juce_graphics/juce_graphics.h>

namespace linen
{

/** The background: glossy cool white, lit from the top left, with a soft
    diagonal sheen and a whisper of grain (so the gradient never bands).
    Rendered once per size at physical pixel resolution, never per frame. */
class Backdrop
{
public:
    /** Draws the cached backdrop into area, re-rendering only if the physical size changed. */
    void draw (juce::Graphics& g, juce::Rectangle<int> area);

    static juce::Image render (int physicalWidth, int physicalHeight);

private:
    juce::Image cache;
};

} // namespace linen
