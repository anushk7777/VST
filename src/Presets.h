#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Parameters.h"

/** Factory presets. Each lists only what differs from the defaults, and the
    defaults are neutral ("Clean"), so every preset spells out its whole
    sound. Every other parameter returns to its default when a preset loads. */
namespace linen::presets
{

struct Preset
{
    const char* name;
    std::initializer_list<std::pair<const char*, float>> values;
};

namespace id = params::id;

inline const std::array<Preset, 9> factory {{
    // Untouched: the output is the input, bit for bit (delayed by the latency).
    { "Clean", {} },

    // Dua Wox in every preset: the TUBE catches the peaks, the
    // OPTO levels the line underneath, Warmth (comp_cotton) colours both.
    { "House Silk", {
        { id::preampDrive, 0.3f },
        { id::deharshDepth, 5.0f },
        { id::compRatio, 2.0f }, { id::adcOptoReduction, 0.35f }, { id::compCotton, 0.5f },
        { id::airAmount, 0.3f }, { id::airDeEss, 0.5f } } },

    // Tone: a little weight out below ~450 Hz, a little presence from 3 kHz
    // (the Pro-Q moves this preset was first paired with, now built in).
    { "Velvet Pop Lead", {
        { id::silk, 0.6f },
        { id::toneLowFreq, 450.0f }, { id::toneLowGain, -2.0f }, { id::toneHighFreq, 3000.0f }, { id::toneHighGain, 1.2f },
        { id::preampDrive, 0.35f },
        { id::eqLowFreq, 3 }, { id::eqLowGain, -1.0f }, { id::eqMidFreq, 4 }, { id::eqMidGain, -1.0f }, { id::eqHighGain, 2.5f },
        { id::deharshDepth, 6.0f }, { id::deharshDetail, 0.55f },
        { id::compThreshold, -20.0f }, { id::compRatio, 2.5f }, { id::compAttack, 15.0f },
        { id::adcOptoReduction, 0.4f }, { id::compCotton, 0.65f },
        { id::airAmount, 0.45f }, { id::airDeEss, 0.6f } } },

    { "Breathy Intimate", {
        { id::toneLowFreq, 120.0f }, { id::toneLowGain, -3.0f },
        { id::preampDrive, 0.2f }, { id::eqHighGain, 1.0f },
        { id::deharshDepth, 4.0f },
        { id::compThreshold, -24.0f }, { id::compRatio, 2.0f }, { id::compAttack, 25.0f },
        { id::adcOptoReduction, 0.3f }, { id::compCotton, 0.4f },
        { id::airAmount, 0.7f }, { id::airDeEss, 0.5f } } },

    { "Warm & Close", {
        { id::toneLowFreq, 70.0f }, { id::toneLowGain, -2.0f },
        { id::preampDrive, 0.4f }, { id::eqLowFreq, 4 }, { id::eqLowGain, 1.5f },
        { id::deharshDepth, 5.0f },
        { id::compThreshold, -22.0f }, { id::compRatio, 2.5f }, { id::adcOptoReduction, 0.4f }, { id::compCotton, 0.6f },
        { id::airAmount, 0.2f }, { id::airDeEss, 0.5f } } },

    { "Transparent Polish", {
        { id::deharshDepth, 3.0f }, { id::deharshDetail, 0.7f },
        { id::compThreshold, -16.0f }, { id::compRatio, 1.5f }, { id::adcOptoReduction, 0.2f },
        { id::airAmount, 0.15f }, { id::airDeEss, 0.4f } } },

    { "De-harsh Only", {
        { id::deharshDepth, 6.0f } } },

    { "Velvet Ache", {
        { id::silk, 0.65f }, { id::pain, 0.35f },
        { id::toneLowFreq, 120.0f }, { id::toneLowGain, -3.0f },
        { id::preampDrive, 0.3f },
        { id::deharshDepth, 5.0f },
        { id::compRatio, 2.0f }, { id::adcOptoReduction, 0.35f }, { id::compCotton, 0.6f },
        { id::airAmount, 0.3f }, { id::airDeEss, 0.5f } } },

    { "Torn Linen", {
        { id::silk, 0.4f }, { id::pain, 0.8f },
        { id::toneLowFreq, 120.0f }, { id::toneLowGain, -3.0f },
        { id::preampDrive, 0.3f },
        { id::deharshDepth, 5.0f },
        { id::compThreshold, -20.0f }, { id::compRatio, 2.5f }, { id::adcOptoReduction, 0.3f }, { id::compCotton, 0.5f },
        { id::airAmount, 0.3f }, { id::airDeEss, 0.5f } } },
}};

/** Presets whose job is to crack and strain (Pain): left out of the clean-sound budgets. */
inline bool isMeantToDistort (const Preset& preset)
{
    for (const auto& [paramId, value] : preset.values)
        if (juce::String (paramId) == id::pain && value > 0.0f)
            return true;
    return false;
}

/** Index of the preset with this name, or -1. */
inline int indexOf (const juce::String& name)
{
    for (size_t i = 0; i < factory.size(); ++i)
        if (name == factory[i].name)
            return (int) i;
    return -1;
}

/** Sets every parameter to its default, then applies the preset's values. */
inline void apply (juce::AudioProcessorValueTreeState& state, const Preset& preset)
{
    for (auto* p : state.processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            float value = ranged->getDefaultValue();
            for (const auto& [paramId, v] : preset.values)
                if (ranged->getParameterID() == paramId)
                    value = ranged->convertTo0to1 (v);

            ranged->beginChangeGesture();
            ranged->setValueNotifyingHost (value);
            ranged->endChangeGesture();
        }
}

} // namespace linen::presets
