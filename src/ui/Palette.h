#pragma once

#include <juce_graphics/juce_graphics.h>

/** Linen's colours: warm linen, champagne metal and espresso ink, with
    electric cobalt reserved for light (lamps, active states, Listen) and
    crimson for Pain. Everything on screen takes its colour from here. */
namespace linen::palette
{
    // Linen.
    inline const juce::Colour surface      { 0xfff6f3ec };  // alabaster base
    inline const juce::Colour surfaceShade { 0xffe9e2d4 };  // where it falls into shade
    inline const juce::Colour card         { 0xfffaf7f1 };  // raised section cards
    inline const juce::Colour well         { 0xffebe4d8 };  // recessed wells: display, meters
    inline const juce::Colour espresso     { 0xff221e1c };  // ink

    // Champagne metal.
    inline const juce::Colour champagneDeep  { 0xffb08f55 };
    inline const juce::Colour champagne      { 0xffc8a96e };
    inline const juce::Colour champagneLight { 0xffe6d3a7 };
    inline const juce::Colour ivory          { 0xfffbf8f1 };  // pill and selector bodies
    inline const juce::Colour rim            { 0xffd6c7aa };  // hairline edges

    // The silk veil.
    inline const juce::Colour terracotta { 0xffc47d68 };

    // Light: lamps, active controls, Listen.
    inline const juce::Colour cobaltDeep  { 0xff0a2db0 };
    inline const juce::Colour cobalt      { 0xff1f4fff };
    inline const juce::Colour cobaltLight { 0xff7c9bff };
    inline const juce::Colour electric    { 0xff3a86ff };

    // Pain.
    inline const juce::Colour crimsonDeep { 0xff9e2a2b };
    inline const juce::Colour crimson     { 0xffd1493f };

    inline juce::Colour ink (float alpha) { return espresso.withAlpha (alpha); }
}
