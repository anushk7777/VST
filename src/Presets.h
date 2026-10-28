#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Parameters.h"

/** Factory presets. Each lists only what differs from the defaults; every
    other parameter returns to its default when a preset loads. */
namespace linen::presets
{

struct Preset
{
    const char* name;
    std::initializer_list<std::pair<const char*, float>> values;
};

namespace id = params::id;

inline const std::array<Preset, 6> factory {{
    { "House Silk", {} },

    { "Velvet Pop Lead", {
        { id::silk, 0.6f }, { id::eqHighPass, 2 }, { id::preampDrive, 0.3f },
        { id::eqLowFreq, 3 }, { id::eqLowGain, -1.0f }, { id::eqMidFreq, 4 }, { id::eqMidGain, -1.5f }, { id::eqHighGain, 2.5f },
        { id::deharshDepth, 6.0f }, { id::deharshDetail, 0.55f },
        { id::compThreshold, -20.0f }, { id::compRatio, 2.5f }, { id::compAttack, 15.0f }, { id::compCotton, 0.65f },
        { id::airAmount, 0.45f }, { id::airDeEss, 0.6f } } },

    { "Breathy Intimate", {
        { id::eqHighPass, 2 }, { id::preampDrive, 0.2f }, { id::eqHighGain, 1.0f },
        { id::deharshDepth, 4.0f },
        { id::compThreshold, -24.0f }, { id::compAttack, 25.0f }, { id::compCotton, 0.4f },
        { id::airAmount, 0.7f } } },

    { "Warm & Close", {
        { id::eqHighPass, 1 }, { id::preampDrive, 0.45f }, { id::eqLowFreq, 4 }, { id::eqLowGain, 1.5f },
        { id::compThreshold, -22.0f }, { id::compRatio, 3.0f }, { id::compCotton, 0.8f },
        { id::airAmount, 0.2f } } },

    { "Transparent Polish", {
        { id::preampDrive, 0.0f }, { id::deharshDepth, 3.0f }, { id::deharshDetail, 0.7f },
        { id::compThreshold, -16.0f }, { id::compRatio, 1.5f }, { id::compCotton, 0.0f },
        { id::airAmount, 0.15f }, { id::airDeEss, 0.4f } } },

    { "De-harsh Only", {
        { id::preampDrive, 0.0f }, { id::deharshDepth, 6.0f }, { id::compRatio, 1.0f }, { id::compCotton, 0.0f },
        { id::airAmount, 0.0f }, { id::airDeEss, 0.0f } } },
}};

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
