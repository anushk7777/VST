#pragma once

#include <juce_graphics/juce_graphics.h>

/** Linen's colours: glossy white with electric cobalt. Everything on screen
    takes its colour from here. */
namespace linen::palette
{
    inline const juce::Colour surface      { 0xfffbfcfe };  // cool white base
    inline const juce::Colour surfaceShade { 0xffeef2f8 };  // where the base falls into shade
    inline const juce::Colour well         { 0xffedf1f7 };  // recessed wells: display, meters
    inline const juce::Colour navy         { 0xff0c1530 };  // ink

    inline const juce::Colour cobaltDeep   { 0xff0a2db0 };  // shaded enamel, pressed states
    inline const juce::Colour cobalt       { 0xff1f4fff };  // electric cobalt: the accent
    inline const juce::Colour cobaltLight  { 0xff7c9bff };  // lit enamel, soft accents
    inline const juce::Colour electric     { 0xff3a86ff };  // glow: veil depth, meters, lights

    inline const juce::Colour porcelain      { 0xffffffff };  // glossy white knob bodies
    inline const juce::Colour porcelainShade { 0xffdbe2ee };
    inline const juce::Colour rim            { 0xffc3cde0 };

    inline juce::Colour ink (float alpha) { return navy.withAlpha (alpha); }
}
