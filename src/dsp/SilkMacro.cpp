#include "SilkMacro.h"
#include "LinenEngine.h"

namespace linen
{

namespace
{
    // Level trim, measured on a sung phrase (tests/SilkTests.cpp): a vocal's
    // loud phrases reach the compressor's detector at about -14 dB, and auto
    // gain gives back most, not all, of what extra compression takes.
    constexpr float vocalDetectorDb = -14.0f;
    constexpr float trimPerHeldDb = 0.86f;

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

    /** How far below its input level a sung vocal leaves the compressor, after auto gain. */
    float heldDownDb (const CompParams& c)
    {
        return -(SilkCompressor::staticGainDb (vocalDetectorDb, c.thresholdDb, c.ratio) + SilkCompressor::makeupDb (c));
    }
}

EngineParams SilkMacro::apply (const EngineParams& user, float silk)
{
    silk = juce::jlimit (0.0f, 1.0f, silk);
    if (std::abs (silk - 0.5f) <= neutralZone)
        return user;

    EngineParams p = user;
    const auto& on = user.sections;

    // Drive comes in early, De-harsh across the whole travel, compression in
    // the middle, Air last: the order an engineer would reach for them.
    if (on.preamp)
        p.preampDrive = juce::jlimit (0.0f, 1.0f, user.preampDrive + 0.25f * amount (silk, 0.0f, 0.5f));

    if (on.deharsh)
        p.deharsh.depth = juce::jlimit (0.0f, 15.0f, user.deharsh.depth + 4.0f * amount (silk, 0.0f, 1.0f));

    if (on.comp)
    {
        // Threshold and ratio together, so Silk compresses even from a
        // compressor left at 1:1.
        const float squeeze = amount (silk, 0.25f, 0.85f);
        p.comp.thresholdDb = juce::jlimit (-48.0f, 0.0f, user.comp.thresholdDb - 10.0f * squeeze);
        p.comp.ratio = juce::jlimit (1.0f, 6.0f, user.comp.ratio + squeeze);
        p.comp.cotton = juce::jlimit (0.0f, 1.0f, user.comp.cotton + 0.4f * amount (silk, 0.2f, 0.9f));
    }

    if (on.air)
        p.air.amount = juce::jlimit (0.0f, 1.0f, user.air.amount + 0.3f * amount (silk, 0.4f, 1.0f));

    p.outputGainDb = user.outputGainDb + levelTrimDb (user, p);
    return p;
}

float SilkMacro::levelTrimDb (const EngineParams& user, const EngineParams& silked)
{
    if (! user.sections.comp)
        return 0.0f;

    return trimPerHeldDb * (heldDownDb (silked.comp) - heldDownDb (user.comp));
}

} // namespace linen
