#include "SilkMacro.h"
#include "LinenEngine.h"

namespace linen
{

namespace
{
    /** Smooth 0..1 ramp between edge0 and edge1. */
    float smoothstep (float edge0, float edge1, float x)
    {
        const float t = juce::jlimit (0.0f, 1.0f, (x - edge0) / (edge1 - edge0));
        return t * t * (3.0f - 2.0f * t);
    }

    /** Maps silk 0..1 to -1..+1 around the neutral middle, shaped by a curve
        that is active over [from, to] of the upper half (mirrored below). */
    float amount (float silk, float from, float to)
    {
        const float m = (silk - 0.5f) * 2.0f;
        const float shaped = smoothstep (from, to, std::abs (m));
        return m < 0.0f ? -shaped : shaped;
    }
}

EngineParams SilkMacro::apply (const EngineParams& user, float silk)
{
    EngineParams p = user;
    silk = juce::jlimit (0.0f, 1.0f, silk);

    // Drive comes in early, De-harsh across the whole travel, compression in
    // the middle, Air last: the order an engineer would reach for them.
    p.preampDrive = juce::jlimit (0.0f, 1.0f, user.preampDrive + 0.25f * amount (silk, 0.0f, 0.5f));
    p.deharsh.depth = juce::jlimit (0.0f, 15.0f, user.deharsh.depth + 4.0f * amount (silk, 0.0f, 1.0f));
    p.comp.thresholdDb = juce::jlimit (-48.0f, 0.0f, user.comp.thresholdDb - 10.0f * amount (silk, 0.25f, 0.85f));
    p.comp.cotton = juce::jlimit (0.0f, 1.0f, user.comp.cotton + 0.4f * amount (silk, 0.2f, 0.9f));
    p.air.amount = juce::jlimit (0.0f, 1.0f, user.air.amount + 0.3f * amount (silk, 0.4f, 1.0f));

    p.outputGainDb = user.outputGainDb + levelTrimDb (silk);
    return p;
}

float SilkMacro::levelTrimDb (float silk)
{
    // Measured on a sung phrase at the house settings (tests/SilkTests.cpp):
    // more compression loses a little level that auto gain doesn't fully
    // restore, and pulling everything back loses a touch of preamp density.
    const float m = (juce::jlimit (0.0f, 1.0f, silk) - 0.5f) * 2.0f;
    return m > 0.0f ? 1.0f * m * m : 0.2f * m * m;
}

} // namespace linen
