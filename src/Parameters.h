#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/LinenEngine.h"

/** The plugin's whole parameter layout lives here, in one place.
    IDs are permanent: renaming one breaks saved sessions and automation. */
namespace linen::params
{

namespace id
{
    inline constexpr auto inputGain  = "input_gain";
    inline constexpr auto outputGain = "output_gain";
    inline constexpr auto mix        = "mix";
}

inline constexpr int version = 1;

inline juce::String formatDb (float value, int)
{
    return (value > 0.0f ? "+" : "") + juce::String (value, 1) + " dB";
}

inline juce::String formatPercent (float value, int)
{
    return juce::String (juce::roundToInt (value * 100.0f)) + " %";
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using Float = juce::AudioParameterFloat;
    using Attributes = juce::AudioParameterFloatAttributes;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<Float> (juce::ParameterID { id::inputGain, version }, "Input",
                                         juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
                                         Attributes().withLabel ("dB").withStringFromValueFunction (formatDb)));

    layout.add (std::make_unique<Float> (juce::ParameterID { id::outputGain, version }, "Output",
                                         juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
                                         Attributes().withLabel ("dB").withStringFromValueFunction (formatDb)));

    layout.add (std::make_unique<Float> (juce::ParameterID { id::mix, version }, "Mix",
                                         juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f,
                                         Attributes().withStringFromValueFunction (formatPercent)
                                                     .withValueFromStringFunction ([] (const juce::String& text)
                                                                                   { return text.getFloatValue() / 100.0f; })));

    return layout;
}

/** Raw parameter values, cached once so the audio thread never looks them up by name. */
struct Handles
{
    explicit Handles (juce::AudioProcessorValueTreeState& state)
        : inputGain  (*state.getRawParameterValue (id::inputGain)),
          outputGain (*state.getRawParameterValue (id::outputGain)),
          mix        (*state.getRawParameterValue (id::mix))
    {
    }

    EngineParams read() const
    {
        EngineParams p;
        p.inputGainDb  = inputGain.load();
        p.outputGainDb = outputGain.load();
        p.mix          = mix.load();
        return p;
    }

    std::atomic<float>& inputGain;
    std::atomic<float>& outputGain;
    std::atomic<float>& mix;
};

} // namespace linen::params
