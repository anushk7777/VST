#pragma once

#include <juce_dsp/juce_dsp.h>

#include "Biquad.h"

namespace linen
{

/** PAIN: the counterpart to Silk. Raw ache that only bites when the singer
    pushes, voiced so it still sounds like a record, not like clipping.

    A "nervous system" watches the strain band (1.8-4.2 kHz) with two
    memories: a fast reflex (1.5 ms / 40 ms) for belts and hard consonants,
    and a slow thermal fatigue (250 ms / 1.2 s) that builds on long loud
    lines. Their combination, scaled by Pain, is the stress (0..1) that drives:

      1. Starved-plate breakdown (4x oversampled): the positive rail sags,
         the bias shifts and a little hysteresis creeps in, so pushed notes
         crack with odd + even harmonics while quiet ones stay fragile.
      2. Throat ache: upward compression of the 1.1-3 kHz band between
         -42 and -18 dBFS, pulling up fry, scrape and tension between words.
      3. Trauma flutter: a tiny modulated delay (up to 0.45 ms, 4-9 Hz noise)
         that only trembles under stress.
      4. Level compensation, so turning Pain up adds ache, not volume.

    Pain 0 is an exact, latency-aligned bypass. */
class PainEngine
{
public:
    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset();
    void setPain (float pain01) { pain.setTargetValue (juce::jlimit (0.0f, 1.0f, pain01)); }
    void process (juce::AudioBuffer<float>& buffer);

    int getLatencySamples() const noexcept { return latency; }

    /** Peak stress of the last block, 0..1 (already scaled by Pain), for the GUI. */
    float getStressLevel() const noexcept { return stressMeter.load (std::memory_order_relaxed); }

    /** Output trim (dB) that keeps a sung vocal level as Pain rises. */
    static float compensationDb (float pain01);

private:
    void processWet (juce::AudioBuffer<float>& buffer, int numSamples, int channels);

    double sampleRate = 48000.0;
    int numChannels = 0;
    int latency = 0, oversamplingLatency = 0, flutterBase = 0;
    bool wasActive = false;

    juce::SmoothedValue<float> pain { 0.0f }, makeup { 1.0f };
    std::vector<float> painRamp, stressRamp, makeupRamp;
    std::atomic<float> stressMeter { 0.0f };

    // Nervous system.
    Biquad strainHp, strainLp;
    float reflex = 0.0f, fatigue = 0.0f;
    float sagMakeup = 0.0f, sagMakeupCoeff = 0.0f;
    float reflexAttack = 0.0f, reflexRelease = 0.0f, fatigueAttack = 0.0f, fatigueRelease = 0.0f;

    // Stage 1: starved plate.
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    std::vector<float> hysteresis;
    std::vector<Biquad> dcBlock;

    // Stage 2: throat ache.
    juce::dsp::StateVariableTPTFilter<float> throatBand;
    juce::AudioBuffer<float> band;
    float throatPower = 0.0f, throatCoeff = 0.0f;

    // Stage 3: flutter.
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd> flutter;
    juce::Random noise;
    Biquad tremorHp, tremorLp1, tremorLp2;
    float tremorNorm = 1.0f;

    // Bypass blend.
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> dryDelay;
    juce::AudioBuffer<float> dry;
};

} // namespace linen
