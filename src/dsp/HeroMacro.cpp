#include "HeroMacro.h"
#include "LinenEngine.h"

namespace linen
{

namespace
{
    /** Smooth 0..1 ramp of the knob between from and to. */
    float curve (float linen, float from, float to)
    {
        const float t = juce::jlimit (0.0f, 1.0f, (linen - from) / (to - from));
        return t * t * (3.0f - 2.0f * t);
    }

    // The recipe at 100%, tuned on a real tracked vocal (README: "The LINEN knob").
    constexpr float fullDrive = 0.4f;
    constexpr float fullDepth = 4.5f;
    constexpr float fullThresholdDb = -26.0f, startThresholdDb = -16.0f;
    constexpr float fullRatio = 3.5f;          // the TUBE at about 2.2:1
    constexpr float fullOpto = 0.45f;
    constexpr float fullWarmth = 0.45f;
    constexpr float fullHighAir = 0.6f, fullMidAir = 0.35f;
    constexpr float fullTune = 0.6f;
}

EngineParams HeroMacro::apply (const EngineParams& user, float linen)
{
    linen = juce::jlimit (0.0f, 1.0f, linen);
    if (linen <= 0.0f)
        return user;

    EngineParams p = user;
    const auto& on = user.sections;

    if (on.preamp)
        p.preampDrive = juce::jmax (user.preampDrive, fullDrive * curve (linen, 0.0f, 0.8f));

    if (on.deharsh)
    {
        p.deharsh.depth = juce::jmax (user.deharsh.depth, fullDepth * curve (linen, 0.0f, 0.7f));
        if (user.deharsh.key >= 0)
            p.deharsh.tune = juce::jmax (user.deharsh.tune, fullTune * curve (linen, 0.2f, 1.0f));
    }

    if (on.comp)
    {
        const float squeeze = curve (linen, 0.1f, 0.9f);
        p.comp.thresholdDb = juce::jmin (user.comp.thresholdDb, startThresholdDb + (fullThresholdDb - startThresholdDb) * squeeze);
        p.comp.ratio = juce::jmax (user.comp.ratio, 1.0f + (fullRatio - 1.0f) * squeeze);
        p.comp.optoReduction = juce::jmax (user.comp.optoReduction, fullOpto * squeeze);
        p.comp.warmth = juce::jmax (user.comp.warmth, fullWarmth * curve (linen, 0.2f, 1.0f));
        p.comp.weave = juce::jmax (user.comp.weave, 0.5f + 0.5f * curve (linen, 0.4f, 1.0f));
    }

    if (on.air)
    {
        p.air.amount = juce::jmax (user.air.amount, fullHighAir * curve (linen, 0.15f, 1.0f));
        p.air.mid = juce::jmax (user.air.mid, fullMidAir * curve (linen, 0.25f, 1.0f));
    }

    return p;
}

} // namespace linen
