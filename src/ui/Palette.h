#pragma once

#include <juce_graphics/juce_graphics.h>

namespace linen::palette
{
    inline const juce::Colour alabaster      { 0xfff6f3ec };  // base surface
    inline const juce::Colour cashmere       { 0xffebe4d8 };  // recessed wells
    inline const juce::Colour espresso       { 0xff221e1c };  // ink
    inline const juce::Colour champagneDeep  { 0xffb08f55 };  // shaded metal, active arcs
    inline const juce::Colour champagne      { 0xffc8a96e };
    inline const juce::Colour champagneLight { 0xffe6d3a7 };
    inline const juce::Colour terracotta     { 0xffc47d68 };  // gain-reduction veil

    inline juce::Colour ink (float alpha) { return espresso.withAlpha (alpha); }
}
