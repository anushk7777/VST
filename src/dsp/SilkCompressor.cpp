#include "SilkCompressor.h"

namespace linen
{

namespace
{
    constexpr float slowReleaseMs = 1500.0f;
    constexpr float makeupReferenceDb = -14.0f;  // a typical sung vocal's RMS
}

float SilkCompressor::staticGainDb (float inputDb, float thresholdDb, float ratio)
{
    const float over = inputDb - thresholdDb;
    const float slope = 1.0f / juce::jmax (1.0f, ratio) - 1.0f;

    if (over <= -kneeDb * 0.5f)
        return 0.0f;
    if (over >= kneeDb * 0.5f)
        return slope * over;

    const float x = over + kneeDb * 0.5f;
    return slope * x * x / (2.0f * kneeDb);
}

void SilkCompressor::prepare (double newSampleRate, int, int newNumChannels)
{
    sampleRate = newSampleRate;
    numChannels = newNumChannels;

    detectorHighPass.c = Biquad::Coeffs::highPass (sampleRate, 100.0, 0.707);
    rmsCoeff = (float) std::exp (-1.0 / (0.010 * sampleRate));        // 10 ms RMS window
    historyCoeff = (float) std::exp (-1.0 / (1.0 * sampleRate));      // ~1 s memory

    makeup.reset (sampleRate, 0.05);
    mix.reset (sampleRate, 0.025);
    reset();
}

void SilkCompressor::reset()
{
    detectorHighPass.reset();
    rmsState = 0.0f;
    reductionDb = 0.0f;
    history = 0.0f;
    meterDb.store (0.0f);
    makeup.setCurrentAndTargetValue (1.0f);
    mix.setCurrentAndTargetValue (params.mix);
}

void SilkCompressor::process (juce::AudioBuffer<float>& buffer)
{
    const int n = buffer.getNumSamples();
    const int chans = juce::jmin (numChannels, buffer.getNumChannels());
    if (chans == 0)
        return;

    const float attack = std::exp (-1.0f / (juce::jmax (0.1f, params.attackMs) * 0.001f * (float) sampleRate));
    const float fastRelease = juce::jmax (5.0f, params.releaseMs);
    const float norm = 1.0f / (float) chans;

    const float makeupDb = params.autoMakeup ? -0.6f * staticGainDb (makeupReferenceDb, params.thresholdDb, params.ratio) : 0.0f;
    makeup.setTargetValue (juce::Decibels::decibelsToGain (makeupDb));
    mix.setTargetValue (juce::jlimit (0.0f, 1.0f, params.mix));

    for (int i = 0; i < n; ++i)
    {
        // Linked detector: mean of the channels, high-passed, RMS.
        float x = 0.0f;
        for (int ch = 0; ch < chans; ++ch)
            x += buffer.getSample (ch, i);
        x = detectorHighPass.process (x * norm);
        rmsState = x * x + (rmsState - x * x) * rmsCoeff;
        const float levelDb = 10.0f * std::log10 (rmsState + 1.0e-12f) + 3.01f;  // +3 dB: sine RMS → peak level

        const float target = -staticGainDb (levelDb, params.thresholdDb, params.ratio);

        // Opto-style release: the longer it has been holding the voice down,
        // the slower it lets go (fast → ~1.5 s).
        history = juce::jmin (1.0f, reductionDb / 6.0f) + (history - juce::jmin (1.0f, reductionDb / 6.0f)) * historyCoeff;
        const float releaseMs = fastRelease + (slowReleaseMs - fastRelease) * history * history;
        const float release = std::exp (-1.0f / (releaseMs * 0.001f * (float) sampleRate));

        reductionDb = target + (reductionDb - target) * (target > reductionDb ? attack : release);

        const float gain = juce::Decibels::decibelsToGain (-reductionDb) * makeup.getNextValue();
        const float m = mix.getNextValue();

        for (int ch = 0; ch < chans; ++ch)
        {
            auto* d = buffer.getWritePointer (ch);
            d[i] = d[i] * (1.0f - m + m * gain);
        }
    }

    meterDb.store (reductionDb, std::memory_order_relaxed);
}

} // namespace linen
