#pragma once

#include <juce_core/juce_core.h>

namespace linen
{

struct EngineParams;

/** The Silk macro: one knob that moves the whole chain together.

    50% leaves every setting as it is. Turning it up adds drive, De-harsh
    depth, compression, Cotton and Air in step; turning it down pulls them
    all back toward transparent. Each target follows its own curve, and an
    output trim keeps the level steady, so the knob changes texture and
    density rather than loudness. */
struct SilkMacro
{
    /** Returns the settings the engine should run with, given the user's
        settings and the Silk position (0..1). */
    static EngineParams apply (const EngineParams& user, float silk);

    /** Output trim (dB) that keeps a typical vocal at constant level as the
        macro moves away from 50%. */
    static float levelTrimDb (float silk);
};

} // namespace linen
