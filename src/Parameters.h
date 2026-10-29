#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/LinenEngine.h"

/** The plugin's whole parameter layout lives here, in one place.
    IDs are permanent: renaming one breaks saved sessions and automation.

    Every default is neutral: a fresh Linen (the "Clean" preset) passes the
    audio through bit-exactly, delayed by its latency. Each knob then works
    on its own from there. */
namespace linen::params
{

namespace id
{
    inline constexpr auto inputGain  = "input_gain";
    inline constexpr auto outputGain = "output_gain";
    inline constexpr auto mix        = "mix";
    inline constexpr auto silk       = "silk";
    inline constexpr auto pain       = "pain";

    inline constexpr auto nullTest   = "null_test";

    inline constexpr auto toneOn       = "tone_on";
    inline constexpr auto toneLowFreq  = "tone_low_freq";
    inline constexpr auto toneLowGain  = "tone_low_gain";
    inline constexpr auto toneHighFreq = "tone_high_freq";
    inline constexpr auto toneHighGain = "tone_high_gain";

    inline constexpr auto preampOn    = "preamp_on";
    inline constexpr auto preampDrive = "preamp_drive";
    inline constexpr auto eqLowFreq   = "eq_low_freq";
    inline constexpr auto eqLowGain   = "eq_low_gain";
    inline constexpr auto eqMidFreq   = "eq_mid_freq";
    inline constexpr auto eqMidGain   = "eq_mid_gain";
    inline constexpr auto eqHighGain  = "eq_high_gain";

    inline constexpr auto deharshOn        = "deharsh_on";
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
    inline constexpr auto deharshKey       = "deharsh_key";
    inline constexpr auto deharshScale     = "deharsh_scale";
    inline constexpr auto deharshTune      = "deharsh_tune";
    inline constexpr auto deharshDelta     = "deharsh_delta";
    inline constexpr auto deharshSidechain = "deharsh_sidechain";

    // The Dua Wox compressor. Its TUBE slot keeps the comp_* IDs from 0.1/0.2.
    inline constexpr auto compOn        = "comp_on";
    inline constexpr auto adcTubeOn     = "adc_tube_on";
    inline constexpr auto compThreshold = "comp_threshold";
    inline constexpr auto compRatio     = "comp_ratio";
    inline constexpr auto compAttack    = "comp_attack";
    inline constexpr auto compRelease   = "comp_release";
    inline constexpr auto compMakeup    = "comp_makeup";
    inline constexpr auto compMix       = "comp_mix";
    inline constexpr auto compCotton    = "comp_cotton";     // Warmth
    inline constexpr auto adcOptoOn     = "adc_opto_on";
    inline constexpr auto adcOptoReduction = "adc_opto_reduction";

    inline constexpr auto airOn     = "air_on";
    inline constexpr auto airAmount = "air_amount";
    inline constexpr auto airDeEss  = "air_deess";
    inline constexpr auto airMid    = "air_mid";

    inline constexpr auto linen     = "linen";   // the hero knob
}

inline constexpr int version = 1;      // parameters since 0.1
inline constexpr int version2 = 2;     // added in 0.2: Tone, power switches, Null
inline constexpr int version3 = 3;     // added in 0.3: the Dua Wox compressor's slots
inline constexpr int version4 = 4;     // added in 0.5: LINEN, Mid Air, Key mode

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

    layout.add (std::make_unique<Float> (juce::ParameterID { id::linen, version4 }, "LINEN",
                                         juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f,
                                         Attributes().withStringFromValueFunction (formatPercent)
                                                     .withValueFromStringFunction ([] (const juce::String& text)
                                                                                   { return text.getFloatValue() / 100.0f; })));

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

    layout.add (std::make_unique<Bool> (juce::ParameterID { id::nullTest, version2 }, "Null Test", false));

    // Gains in dB, centred on 0.
    const auto gainDb = [] (float range)
    {
        return std::pair { juce::NormalisableRange<float> (-range, range, 0.1f),
                           Attributes().withLabel ("dB").withStringFromValueFunction (formatDb) };
    };
    const auto hzAttributes = Attributes().withLabel ("Hz").withStringFromValueFunction (formatHz);

    // Tone: Pro-Q-style shelves before the preamp (frequency knobs are log-like).
    using linen::ToneParams;
    layout.add (std::make_unique<Bool> (juce::ParameterID { id::toneOn, version2 }, "Tone On", true));
    layout.add (std::make_unique<Float> (juce::ParameterID { id::toneLowFreq, version2 }, "Tone Low Shelf Freq",
                                         centredRange (ToneParams::minLowHz, ToneParams::maxLowHz,
                                                       std::sqrt (ToneParams::minLowHz * ToneParams::maxLowHz), 0.1f),
                                         ToneParams().lowHz, hzAttributes));
    {
        auto [range, attributes] = gainDb (ToneParams::maxGainDb);
        layout.add (std::make_unique<Float> (juce::ParameterID { id::toneLowGain, version2 }, "Tone Low Shelf Gain", range, 0.0f, attributes));
    }
    layout.add (std::make_unique<Float> (juce::ParameterID { id::toneHighFreq, version2 }, "Tone High Shelf Freq",
                                         centredRange (ToneParams::minHighHz, ToneParams::maxHighHz,
                                                       std::sqrt (ToneParams::minHighHz * ToneParams::maxHighHz), 1.0f),
                                         ToneParams().highHz, hzAttributes));
    {
        auto [range, attributes] = gainDb (ToneParams::maxGainDb);
        layout.add (std::make_unique<Float> (juce::ParameterID { id::toneHighGain, version2 }, "Tone High Shelf Gain", range, 0.0f, attributes));
    }

    // Preamp and 1073-style EQ
    const auto eqGain = gainDb;

    layout.add (std::make_unique<Bool> (juce::ParameterID { id::preampOn, version2 }, "Preamp On", true));
    layout.add (std::make_unique<Float> (juce::ParameterID { id::preampDrive, version }, "Preamp Drive",
                                         juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f, percentAttributes));
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
    layout.add (std::make_unique<Bool> (juce::ParameterID { id::deharshOn, version2 }, "De-harsh On", true));
    layout.add (std::make_unique<Float> (juce::ParameterID { id::deharshDepth, version }, "De-harsh Depth",
                                         juce::NormalisableRange<float> (0.0f, 15.0f, 0.01f), 0.0f,
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
    layout.add (std::make_unique<Choice> (juce::ParameterID { id::deharshKey, version4 }, "De-harsh Key",
                                          juce::StringArray { "Off", "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 0));
    layout.add (std::make_unique<Choice> (juce::ParameterID { id::deharshScale, version4 }, "De-harsh Scale",
                                          juce::StringArray { "Major", "Minor" }, 0));
    layout.add (std::make_unique<Float> (juce::ParameterID { id::deharshTune, version4 }, "De-harsh Tune",
                                         juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f, percentAttributes));
    layout.add (std::make_unique<Bool> (juce::ParameterID { id::deharshDelta, version }, "De-harsh Delta", false));
    layout.add (std::make_unique<Bool> (juce::ParameterID { id::deharshSidechain, version }, "De-harsh Sidechain", false));

    // Dua Wox compressor. TUBE: ratio 1:1 = off. OPTO: reduction 0 = off.
    layout.add (std::make_unique<Bool> (juce::ParameterID { id::compOn, version2 }, "Dua Wox On", true));
    layout.add (std::make_unique<Bool> (juce::ParameterID { id::adcTubeOn, version3 }, "Dua Wox Tube On", true));
    layout.add (std::make_unique<Float> (juce::ParameterID { id::compThreshold, version }, "Dua Wox Tube Threshold",
                                         juce::NormalisableRange<float> (-48.0f, 0.0f, 0.1f), -18.0f,
                                         Attributes().withLabel ("dB").withStringFromValueFunction (formatDb)));
    // The stored value is the knob's travel (1..6, kept from 0.1); it reads
    // the ratio the tube really leans at as its knee begins (1:1 to about 2.3:1).
    layout.add (std::make_unique<Float> (juce::ParameterID { id::compRatio, version }, "Dua Wox Tube Ratio",
                                         centredRange (1.0f, 6.0f, 2.5f, 0.01f), 1.0f,
                                         Attributes().withStringFromValueFunction ([] (float v, int)
                                                                                   { return juce::String (AdcCompressor::tubeRatioFor (v), 2) + ":1"; })
                                                     .withValueFromStringFunction ([] (const juce::String& text)
                                                                                   { return AdcCompressor::ratioKnobFor (text.getFloatValue()); })));
    layout.add (std::make_unique<Float> (juce::ParameterID { id::compAttack, version }, "Dua Wox Tube Attack",
                                         centredRange (1.0f, 80.0f, 12.0f, 0.01f), 12.0f,
                                         Attributes().withLabel ("ms").withStringFromValueFunction (formatMs)));
    layout.add (std::make_unique<Float> (juce::ParameterID { id::compRelease, version }, "Dua Wox Tube Release",
                                         centredRange (20.0f, 400.0f, 80.0f, 0.1f), 60.0f,
                                         Attributes().withLabel ("ms").withStringFromValueFunction (formatMs)));
    layout.add (std::make_unique<Bool> (juce::ParameterID { id::adcOptoOn, version3 }, "Dua Wox Opto On", true));
    layout.add (std::make_unique<Float> (juce::ParameterID { id::adcOptoReduction, version3 }, "Dua Wox Opto Peak Reduction",
                                         juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f, percentAttributes));
    layout.add (std::make_unique<Bool> (juce::ParameterID { id::compMakeup, version }, "Dua Wox Auto Gain", true));
    layout.add (std::make_unique<Float> (juce::ParameterID { id::compMix, version }, "Dua Wox Blend",
                                         juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f, percentAttributes));
    layout.add (std::make_unique<Float> (juce::ParameterID { id::compCotton, version }, "Dua Wox Warmth",
                                         juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f, percentAttributes));

    // Air
    layout.add (std::make_unique<Bool> (juce::ParameterID { id::airOn, version2 }, "Air On", true));
    layout.add (std::make_unique<Float> (juce::ParameterID { id::airAmount, version }, "High Air",
                                         juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f, percentAttributes));
    layout.add (std::make_unique<Float> (juce::ParameterID { id::airMid, version4 }, "Mid Air",
                                         juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f, percentAttributes));
    layout.add (std::make_unique<Float> (juce::ParameterID { id::airDeEss, version }, "Air De-ess",
                                         juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f, percentAttributes));

    return layout;
}

/** Raw parameter values, cached once so the audio thread never looks them up by name. */
struct Handles
{
    explicit Handles (juce::AudioProcessorValueTreeState& state)
        : inputGain        (*state.getRawParameterValue (id::inputGain)),
          outputGain       (*state.getRawParameterValue (id::outputGain)),
          mix              (*state.getRawParameterValue (id::mix)),
          linen            (*state.getRawParameterValue (id::linen)),
          silk             (*state.getRawParameterValue (id::silk)),
          pain             (*state.getRawParameterValue (id::pain)),
          nullTest         (*state.getRawParameterValue (id::nullTest)),
          toneOn           (*state.getRawParameterValue (id::toneOn)),
          toneLowFreq      (*state.getRawParameterValue (id::toneLowFreq)),
          toneLowGain      (*state.getRawParameterValue (id::toneLowGain)),
          toneHighFreq     (*state.getRawParameterValue (id::toneHighFreq)),
          toneHighGain     (*state.getRawParameterValue (id::toneHighGain)),
          preampOn         (*state.getRawParameterValue (id::preampOn)),
          preampDrive      (*state.getRawParameterValue (id::preampDrive)),
          eqLowFreq        (*state.getRawParameterValue (id::eqLowFreq)),
          eqLowGain        (*state.getRawParameterValue (id::eqLowGain)),
          eqMidFreq        (*state.getRawParameterValue (id::eqMidFreq)),
          eqMidGain        (*state.getRawParameterValue (id::eqMidGain)),
          eqHighGain       (*state.getRawParameterValue (id::eqHighGain)),
          deharshOn        (*state.getRawParameterValue (id::deharshOn)),
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
          deharshKey       (*state.getRawParameterValue (id::deharshKey)),
          deharshScale     (*state.getRawParameterValue (id::deharshScale)),
          deharshTune      (*state.getRawParameterValue (id::deharshTune)),
          deharshDelta     (*state.getRawParameterValue (id::deharshDelta)),
          deharshSidechain (*state.getRawParameterValue (id::deharshSidechain)),
          compOn           (*state.getRawParameterValue (id::compOn)),
          adcTubeOn        (*state.getRawParameterValue (id::adcTubeOn)),
          compThreshold    (*state.getRawParameterValue (id::compThreshold)),
          compRatio        (*state.getRawParameterValue (id::compRatio)),
          compAttack       (*state.getRawParameterValue (id::compAttack)),
          compRelease      (*state.getRawParameterValue (id::compRelease)),
          compMakeup       (*state.getRawParameterValue (id::compMakeup)),
          compMix          (*state.getRawParameterValue (id::compMix)),
          compCotton       (*state.getRawParameterValue (id::compCotton)),
          adcOptoOn        (*state.getRawParameterValue (id::adcOptoOn)),
          adcOptoReduction (*state.getRawParameterValue (id::adcOptoReduction)),
          airOn            (*state.getRawParameterValue (id::airOn)),
          airAmount        (*state.getRawParameterValue (id::airAmount)),
          airDeEss         (*state.getRawParameterValue (id::airDeEss)),
          airMid           (*state.getRawParameterValue (id::airMid))
    {
    }

    EngineParams read() const
    {
        EngineParams p;
        p.inputGainDb  = inputGain.load();
        p.outputGainDb = outputGain.load();
        p.mix          = mix.load();
        p.linen        = linen.load();
        p.silk         = silk.load();
        p.pain         = pain.load();
        p.nullTest     = nullTest.load() > 0.5f;

        p.sections.tone    = toneOn.load() > 0.5f;
        p.sections.preamp  = preampOn.load() > 0.5f;
        p.sections.deharsh = deharshOn.load() > 0.5f;
        p.sections.comp    = compOn.load() > 0.5f;
        p.sections.air     = airOn.load() > 0.5f;

        p.tone.lowHz      = toneLowFreq.load();
        p.tone.lowGainDb  = toneLowGain.load();
        p.tone.highHz     = toneHighFreq.load();
        p.tone.highGainDb = toneHighGain.load();

        p.preampDrive  = preampDrive.load();
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
        d.key         = juce::roundToInt (deharshKey.load()) - 1;
        d.minor       = deharshScale.load() > 0.5f;
        d.tune        = deharshTune.load();
        d.delta       = deharshDelta.load() > 0.5f;
        p.deharshSidechain = deharshSidechain.load() > 0.5f;

        p.comp.tubeOn      = adcTubeOn.load() > 0.5f;
        p.comp.thresholdDb = compThreshold.load();
        p.comp.ratio       = compRatio.load();
        p.comp.attackMs    = compAttack.load();
        p.comp.releaseMs   = compRelease.load();
        p.comp.autoMakeup  = compMakeup.load() > 0.5f;
        p.comp.mix         = compMix.load();
        p.comp.warmth      = compCotton.load();
        p.comp.optoOn      = adcOptoOn.load() > 0.5f;
        p.comp.optoReduction = adcOptoReduction.load();

        p.air.amount = airAmount.load();
        p.air.deEss  = airDeEss.load();
        p.air.mid    = airMid.load();
        return p;
    }

    std::atomic<float>& inputGain;
    std::atomic<float>& outputGain;
    std::atomic<float>& mix;
    std::atomic<float>& linen;
    std::atomic<float>& silk;
    std::atomic<float>& pain;
    std::atomic<float>& nullTest;
    std::atomic<float>& toneOn;
    std::atomic<float>& toneLowFreq;
    std::atomic<float>& toneLowGain;
    std::atomic<float>& toneHighFreq;
    std::atomic<float>& toneHighGain;
    std::atomic<float>& preampOn;
    std::atomic<float>& preampDrive;
    std::atomic<float>& eqLowFreq;
    std::atomic<float>& eqLowGain;
    std::atomic<float>& eqMidFreq;
    std::atomic<float>& eqMidGain;
    std::atomic<float>& eqHighGain;
    std::atomic<float>& deharshOn;
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
    std::atomic<float>& deharshKey;
    std::atomic<float>& deharshScale;
    std::atomic<float>& deharshTune;
    std::atomic<float>& deharshDelta;
    std::atomic<float>& deharshSidechain;
    std::atomic<float>& compOn;
    std::atomic<float>& adcTubeOn;
    std::atomic<float>& compThreshold;
    std::atomic<float>& compRatio;
    std::atomic<float>& compAttack;
    std::atomic<float>& compRelease;
    std::atomic<float>& compMakeup;
    std::atomic<float>& compMix;
    std::atomic<float>& compCotton;
    std::atomic<float>& adcOptoOn;
    std::atomic<float>& adcOptoReduction;
    std::atomic<float>& airOn;
    std::atomic<float>& airAmount;
    std::atomic<float>& airDeEss;
    std::atomic<float>& airMid;
};

} // namespace linen::params
