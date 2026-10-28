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

    inline constexpr auto deharshDepth     = "deharsh_depth";
    inline constexpr auto deharshDetail    = "deharsh_detail";
    inline constexpr auto deharshAttack    = "deharsh_attack";
    inline constexpr auto deharshRelease   = "deharsh_release";
    inline constexpr auto deharshMaxCut    = "deharsh_max_cut";
    inline constexpr auto deharshFocusLow  = "deharsh_focus_low";
    inline constexpr auto deharshFocusHigh = "deharsh_focus_high";
    inline constexpr auto deharshMode      = "deharsh_mode";
    inline constexpr auto deharshGlare     = "deharsh_glare";
    inline constexpr auto deharshBody      = "deharsh_body";
    inline constexpr auto deharshDelta     = "deharsh_delta";
    inline constexpr auto deharshSidechain = "deharsh_sidechain";
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

inline juce::String formatMs (float value, int)
{
    return juce::String (value, value < 10.0f ? 1 : 0) + " ms";
}

inline juce::String formatHz (float value, int)
{
    return value >= 1000.0f ? juce::String (value / 1000.0f, value < 10000.0f ? 2 : 1) + " kHz"
                            : juce::String (juce::roundToInt (value)) + " Hz";
}

/** A range whose middle sits at centre (for times and frequencies). */
inline juce::NormalisableRange<float> centredRange (float min, float max, float centre, float step = 0.0f)
{
    juce::NormalisableRange<float> range (min, max, step);
    range.setSkewForCentre (centre);
    return range;
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

    // De-harsh
    using Bool = juce::AudioParameterBool;
    using Choice = juce::AudioParameterChoice;
    const auto percentAttributes = Attributes().withStringFromValueFunction (formatPercent)
                                               .withValueFromStringFunction ([] (const juce::String& text)
                                                                             { return text.getFloatValue() / 100.0f; });

    layout.add (std::make_unique<Float> (juce::ParameterID { id::deharshDepth, version }, "De-harsh Depth",
                                         juce::NormalisableRange<float> (0.0f, 15.0f, 0.01f), 5.0f,
                                         Attributes().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1); })));
    layout.add (std::make_unique<Float> (juce::ParameterID { id::deharshDetail, version }, "De-harsh Detail",
                                         juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f, percentAttributes));
    layout.add (std::make_unique<Float> (juce::ParameterID { id::deharshAttack, version }, "De-harsh Attack",
                                         centredRange (0.5f, 50.0f, 6.0f, 0.01f), 5.0f,
                                         Attributes().withLabel ("ms").withStringFromValueFunction (formatMs)));
    layout.add (std::make_unique<Float> (juce::ParameterID { id::deharshRelease, version }, "De-harsh Release",
                                         centredRange (10.0f, 500.0f, 100.0f, 0.1f), 120.0f,
                                         Attributes().withLabel ("ms").withStringFromValueFunction (formatMs)));
    layout.add (std::make_unique<Float> (juce::ParameterID { id::deharshMaxCut, version }, "De-harsh Max Cut",
                                         juce::NormalisableRange<float> (0.0f, 24.0f, 0.1f), 12.0f,
                                         Attributes().withLabel ("dB").withStringFromValueFunction ([] (float v, int)
                                                                                                    { return juce::String (v, 1) + " dB"; })));
    layout.add (std::make_unique<Float> (juce::ParameterID { id::deharshFocusLow, version }, "De-harsh Focus Low",
                                         centredRange (20.0f, 2000.0f, 200.0f, 0.1f), 120.0f,
                                         Attributes().withLabel ("Hz").withStringFromValueFunction (formatHz)));
    layout.add (std::make_unique<Float> (juce::ParameterID { id::deharshFocusHigh, version }, "De-harsh Focus High",
                                         centredRange (1000.0f, 20000.0f, 4500.0f, 1.0f), 18000.0f,
                                         Attributes().withLabel ("Hz").withStringFromValueFunction (formatHz)));
    layout.add (std::make_unique<Choice> (juce::ParameterID { id::deharshMode, version }, "De-harsh Mode",
                                          juce::StringArray { "Soft", "Hard" }, 0));
    layout.add (std::make_unique<Bool> (juce::ParameterID { id::deharshGlare, version }, "De-harsh Glare Focus", true));
    layout.add (std::make_unique<Bool> (juce::ParameterID { id::deharshBody, version }, "De-harsh Body Protect", true));
    layout.add (std::make_unique<Bool> (juce::ParameterID { id::deharshDelta, version }, "De-harsh Delta", false));
    layout.add (std::make_unique<Bool> (juce::ParameterID { id::deharshSidechain, version }, "De-harsh Sidechain", false));

    return layout;
}

/** Raw parameter values, cached once so the audio thread never looks them up by name. */
struct Handles
{
    explicit Handles (juce::AudioProcessorValueTreeState& state)
        : inputGain        (*state.getRawParameterValue (id::inputGain)),
          outputGain       (*state.getRawParameterValue (id::outputGain)),
          mix              (*state.getRawParameterValue (id::mix)),
          deharshDepth     (*state.getRawParameterValue (id::deharshDepth)),
          deharshDetail    (*state.getRawParameterValue (id::deharshDetail)),
          deharshAttack    (*state.getRawParameterValue (id::deharshAttack)),
          deharshRelease   (*state.getRawParameterValue (id::deharshRelease)),
          deharshMaxCut    (*state.getRawParameterValue (id::deharshMaxCut)),
          deharshFocusLow  (*state.getRawParameterValue (id::deharshFocusLow)),
          deharshFocusHigh (*state.getRawParameterValue (id::deharshFocusHigh)),
          deharshMode      (*state.getRawParameterValue (id::deharshMode)),
          deharshGlare     (*state.getRawParameterValue (id::deharshGlare)),
          deharshBody      (*state.getRawParameterValue (id::deharshBody)),
          deharshDelta     (*state.getRawParameterValue (id::deharshDelta)),
          deharshSidechain (*state.getRawParameterValue (id::deharshSidechain))
    {
    }

    EngineParams read() const
    {
        EngineParams p;
        p.inputGainDb  = inputGain.load();
        p.outputGainDb = outputGain.load();
        p.mix          = mix.load();

        auto& d = p.deharsh;
        d.depth       = deharshDepth.load();
        d.detail      = deharshDetail.load();
        d.attackMs    = deharshAttack.load();
        d.releaseMs   = deharshRelease.load();
        d.maxCutDb    = deharshMaxCut.load();
        d.focusLowHz  = deharshFocusLow.load();
        d.focusHighHz = deharshFocusHigh.load();
        d.hard        = deharshMode.load() > 0.5f;
        d.glareFocus  = deharshGlare.load() > 0.5f;
        d.bodyProtect = deharshBody.load() > 0.5f;
        d.delta       = deharshDelta.load() > 0.5f;
        p.deharshSidechain = deharshSidechain.load() > 0.5f;
        return p;
    }

    std::atomic<float>& inputGain;
    std::atomic<float>& outputGain;
    std::atomic<float>& mix;
    std::atomic<float>& deharshDepth;
    std::atomic<float>& deharshDetail;
    std::atomic<float>& deharshAttack;
    std::atomic<float>& deharshRelease;
    std::atomic<float>& deharshMaxCut;
    std::atomic<float>& deharshFocusLow;
    std::atomic<float>& deharshFocusHigh;
    std::atomic<float>& deharshMode;
    std::atomic<float>& deharshGlare;
    std::atomic<float>& deharshBody;
    std::atomic<float>& deharshDelta;
    std::atomic<float>& deharshSidechain;
};

} // namespace linen::params
