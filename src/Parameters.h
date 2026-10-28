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
    inline constexpr auto silk       = "silk";
    inline constexpr auto pain       = "pain";

    inline constexpr auto preampDrive = "preamp_drive";
    inline constexpr auto eqHighPass  = "eq_high_pass";
    inline constexpr auto eqLowFreq   = "eq_low_freq";
    inline constexpr auto eqLowGain   = "eq_low_gain";
    inline constexpr auto eqMidFreq   = "eq_mid_freq";
    inline constexpr auto eqMidGain   = "eq_mid_gain";
    inline constexpr auto eqHighGain  = "eq_high_gain";

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

    inline constexpr auto compThreshold = "comp_threshold";
    inline constexpr auto compRatio     = "comp_ratio";
    inline constexpr auto compAttack    = "comp_attack";
    inline constexpr auto compRelease   = "comp_release";
    inline constexpr auto compMakeup    = "comp_makeup";
    inline constexpr auto compMix       = "comp_mix";
    inline constexpr auto compCotton    = "comp_cotton";

    inline constexpr auto airAmount = "air_amount";
    inline constexpr auto airDeEss  = "air_deess";
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

    layout.add (std::make_unique<Float> (juce::ParameterID { id::silk, version }, "Silk",
                                         juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f,
                                         Attributes().withStringFromValueFunction (formatPercent)
                                                     .withValueFromStringFunction ([] (const juce::String& text)
                                                                                   { return text.getFloatValue() / 100.0f; })));

    layout.add (std::make_unique<Float> (juce::ParameterID { id::pain, version }, "Pain",
                                         juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f,
                                         Attributes().withStringFromValueFunction (formatPercent)
                                                     .withValueFromStringFunction ([] (const juce::String& text)
                                                                                   { return text.getFloatValue() / 100.0f; })));

    layout.add (std::make_unique<Float> (juce::ParameterID { id::mix, version }, "Mix",
                                         juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f,
                                         Attributes().withStringFromValueFunction (formatPercent)
                                                     .withValueFromStringFunction ([] (const juce::String& text)
                                                                                   { return text.getFloatValue() / 100.0f; })));

    using Bool = juce::AudioParameterBool;
    using Choice = juce::AudioParameterChoice;
    const auto percentAttributes = Attributes().withStringFromValueFunction (formatPercent)
                                               .withValueFromStringFunction ([] (const juce::String& text)
                                                                             { return text.getFloatValue() / 100.0f; });

    // Preamp and 1073-style EQ
    const auto eqGain = [] (float range)
    {
        return std::pair { juce::NormalisableRange<float> (-range, range, 0.1f),
                           Attributes().withLabel ("dB").withStringFromValueFunction (formatDb) };
    };

    layout.add (std::make_unique<Float> (juce::ParameterID { id::preampDrive, version }, "Preamp Drive",
                                         juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.25f, percentAttributes));
    layout.add (std::make_unique<Choice> (juce::ParameterID { id::eqHighPass, version }, "EQ High-pass",
                                          juce::StringArray { "Off", "50 Hz", "80 Hz", "160 Hz", "300 Hz" }, 0));
    layout.add (std::make_unique<Choice> (juce::ParameterID { id::eqLowFreq, version }, "EQ Low Freq",
                                          juce::StringArray { "Off", "35 Hz", "60 Hz", "110 Hz", "220 Hz" }, 2));
    {
        auto [range, attributes] = eqGain (16.0f);
        layout.add (std::make_unique<Float> (juce::ParameterID { id::eqLowGain, version }, "EQ Low Gain", range, 0.0f, attributes));
    }
    layout.add (std::make_unique<Choice> (juce::ParameterID { id::eqMidFreq, version }, "EQ Mid Freq",
                                          juce::StringArray { "Off", "360 Hz", "700 Hz", "1.6 kHz", "3.2 kHz", "4.8 kHz", "7.2 kHz" }, 4));
    {
        auto [range, attributes] = eqGain (18.0f);
        layout.add (std::make_unique<Float> (juce::ParameterID { id::eqMidGain, version }, "EQ Mid Gain", range, 0.0f, attributes));
    }
    {
        auto [range, attributes] = eqGain (16.0f);
        layout.add (std::make_unique<Float> (juce::ParameterID { id::eqHighGain, version }, "EQ High Gain (12 kHz)", range, 0.0f, attributes));
    }

    // De-harsh

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

    // Silk compressor
    layout.add (std::make_unique<Float> (juce::ParameterID { id::compThreshold, version }, "Comp Threshold",
                                         juce::NormalisableRange<float> (-48.0f, 0.0f, 0.1f), -18.0f,
                                         Attributes().withLabel ("dB").withStringFromValueFunction (formatDb)));
    layout.add (std::make_unique<Float> (juce::ParameterID { id::compRatio, version }, "Comp Ratio",
                                         centredRange (1.0f, 6.0f, 2.5f, 0.01f), 2.0f,
                                         Attributes().withStringFromValueFunction ([] (float v, int)
                                                                                   { return juce::String (v, 1) + ":1"; })));
    layout.add (std::make_unique<Float> (juce::ParameterID { id::compAttack, version }, "Comp Attack",
                                         centredRange (1.0f, 80.0f, 12.0f, 0.01f), 12.0f,
                                         Attributes().withLabel ("ms").withStringFromValueFunction (formatMs)));
    layout.add (std::make_unique<Float> (juce::ParameterID { id::compRelease, version }, "Comp Release",
                                         centredRange (20.0f, 400.0f, 80.0f, 0.1f), 60.0f,
                                         Attributes().withLabel ("ms").withStringFromValueFunction (formatMs)));
    layout.add (std::make_unique<Bool> (juce::ParameterID { id::compMakeup, version }, "Comp Auto Makeup", true));
    layout.add (std::make_unique<Float> (juce::ParameterID { id::compMix, version }, "Comp Mix",
                                         juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f, percentAttributes));
    layout.add (std::make_unique<Float> (juce::ParameterID { id::compCotton, version }, "Comp Cotton",
                                         juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f, percentAttributes));

    // Air
    layout.add (std::make_unique<Float> (juce::ParameterID { id::airAmount, version }, "Air",
                                         juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.3f, percentAttributes));
    layout.add (std::make_unique<Float> (juce::ParameterID { id::airDeEss, version }, "Air De-ess",
                                         juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f, percentAttributes));

    return layout;
}

/** Raw parameter values, cached once so the audio thread never looks them up by name. */
struct Handles
{
    explicit Handles (juce::AudioProcessorValueTreeState& state)
        : inputGain        (*state.getRawParameterValue (id::inputGain)),
          outputGain       (*state.getRawParameterValue (id::outputGain)),
          mix              (*state.getRawParameterValue (id::mix)),
          silk             (*state.getRawParameterValue (id::silk)),
          pain             (*state.getRawParameterValue (id::pain)),
          preampDrive      (*state.getRawParameterValue (id::preampDrive)),
          eqHighPass       (*state.getRawParameterValue (id::eqHighPass)),
          eqLowFreq        (*state.getRawParameterValue (id::eqLowFreq)),
          eqLowGain        (*state.getRawParameterValue (id::eqLowGain)),
          eqMidFreq        (*state.getRawParameterValue (id::eqMidFreq)),
          eqMidGain        (*state.getRawParameterValue (id::eqMidGain)),
          eqHighGain       (*state.getRawParameterValue (id::eqHighGain)),
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
          deharshSidechain (*state.getRawParameterValue (id::deharshSidechain)),
          compThreshold    (*state.getRawParameterValue (id::compThreshold)),
          compRatio        (*state.getRawParameterValue (id::compRatio)),
          compAttack       (*state.getRawParameterValue (id::compAttack)),
          compRelease      (*state.getRawParameterValue (id::compRelease)),
          compMakeup       (*state.getRawParameterValue (id::compMakeup)),
          compMix          (*state.getRawParameterValue (id::compMix)),
          compCotton       (*state.getRawParameterValue (id::compCotton)),
          airAmount        (*state.getRawParameterValue (id::airAmount)),
          airDeEss         (*state.getRawParameterValue (id::airDeEss))
    {
    }

    EngineParams read() const
    {
        EngineParams p;
        p.inputGainDb  = inputGain.load();
        p.outputGainDb = outputGain.load();
        p.mix          = mix.load();
        p.silk         = silk.load();
        p.pain         = pain.load();

        p.preampDrive  = preampDrive.load();
        p.eq.highPass   = juce::roundToInt (eqHighPass.load());
        p.eq.lowFreq    = juce::roundToInt (eqLowFreq.load());
        p.eq.lowGainDb  = eqLowGain.load();
        p.eq.midFreq    = juce::roundToInt (eqMidFreq.load());
        p.eq.midGainDb  = eqMidGain.load();
        p.eq.highGainDb = eqHighGain.load();

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

        p.comp.thresholdDb = compThreshold.load();
        p.comp.ratio       = compRatio.load();
        p.comp.attackMs    = compAttack.load();
        p.comp.releaseMs   = compRelease.load();
        p.comp.autoMakeup  = compMakeup.load() > 0.5f;
        p.comp.mix         = compMix.load();
        p.comp.cotton      = compCotton.load();

        p.air.amount = airAmount.load();
        p.air.deEss  = airDeEss.load();
        return p;
    }

    std::atomic<float>& inputGain;
    std::atomic<float>& outputGain;
    std::atomic<float>& mix;
    std::atomic<float>& silk;
    std::atomic<float>& pain;
    std::atomic<float>& preampDrive;
    std::atomic<float>& eqHighPass;
    std::atomic<float>& eqLowFreq;
    std::atomic<float>& eqLowGain;
    std::atomic<float>& eqMidFreq;
    std::atomic<float>& eqMidGain;
    std::atomic<float>& eqHighGain;
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
    std::atomic<float>& compThreshold;
    std::atomic<float>& compRatio;
    std::atomic<float>& compAttack;
    std::atomic<float>& compRelease;
    std::atomic<float>& compMakeup;
    std::atomic<float>& compMix;
    std::atomic<float>& compCotton;
    std::atomic<float>& airAmount;
    std::atomic<float>& airDeEss;
};

} // namespace linen::params
