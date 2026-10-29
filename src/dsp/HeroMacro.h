#pragma once

#include <juce_core/juce_core.h>

namespace linen
{

struct EngineParams;

/** The LINEN knob: one sweep from untouched to the full Linen sound.

    It lays a recipe over the user's settings, each target the stronger of
    the user's and the recipe's: preamp drive, De-harsh depth, Dua Wox
    (threshold, ratio, Peak red., Warmth, Weave), Mid and High Air, and Key
    Tune when a key is set. Each comes in on its own curve (drive and
    De-harsh first, compression in the middle, air and colour last). The
    engine's level match then holds the result at the untouched voice's
    long-term level, so turning it up changes the sound, not the loudness.
    Sections that are switched off are left alone. At 0 it changes nothing. */
struct HeroMacro
{
    static EngineParams apply (const EngineParams& user, float linen);
};

} // namespace linen
