#pragma once

#include <juce_core/juce_core.h>

namespace linen
{

struct EngineParams;

/** The Silk macro: one knob that moves the whole chain together.

    50% leaves every setting as it is. Turning it up adds drive, De-harsh
    depth, compression (the tube's threshold and ratio, and the opto's
    reduction), Warmth and Air in step; turning it down pulls them all back
    toward transparent. Each target follows its
    own curve, and an output trim makes up for the level that the extra (or
    lighter) compression takes, so the knob changes texture and density
    rather than loudness. Sections that are switched off are left alone. */
struct SilkMacro
{
    /** Within this distance of 50% the macro is exactly neutral (a detent). */
    static constexpr float neutralZone = 0.0025f;

    /** Returns the settings the engine should run with, given the user's
        settings and the Silk position (0..1). */
    static EngineParams apply (const EngineParams& user, float silk);

    /** Output trim (dB) that keeps a typical vocal at constant level, given
        the user's settings and what Silk turned them into. 0 when Silk
        changed nothing that moves the level. */
    static float levelTrimDb (const EngineParams& user, const EngineParams& silked);
};

} // namespace linen
