#pragma once

#include <juce_dsp/juce_dsp.h>

#include "Biquad.h"

namespace linen
{

struct CompParams
{
    float thresholdDb = 0.0f;   // -48..0
    float ratio = 1.0f;         // 1..6 (1 = off)
    float attackMs = 12.0f;
    float releaseMs = 120.0f;   // fast part of the release
    bool autoMakeup = true;
    float mix = 1.0f;           // parallel blend
};

/** The "invisible" compressor: feed-forward, stereo-linked RMS detection
    through a 100 Hz side-chain high-pass (so plosives and proximity boom
    don't pump it), a wide 10 dB soft knee, and an opto-style two-stage
    release: quick after a short peak, slowing toward ~1.5 s the longer it
    has been holding the voice down. No latency. */
class SilkCompressor
{
public:
    static constexpr float kneeDb = 10.0f;

    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset();
    void setParams (const CompParams& p) { params = p; }
    void process (juce::AudioBuffer<float>& buffer);

    /** Static curve: gain change in dB for a steady input level (negative = reduction). */
    static float staticGainDb (float inputDb, float thresholdDb, float ratio);

    /** Gain reduction right now, in positive dB (for the meter). */
    float getGainReductionDb() const noexcept { return meterDb.load (std::memory_order_relaxed); }

private:
    double sampleRate = 48000.0;
    int numChannels = 0;
    CompParams params;

    Biquad detectorHighPass;
    float rmsState = 0.0f, rmsCoeff = 0.0f;
    float reductionDb = 0.0f;     // smoothed, positive
    float history = 0.0f;         // 0..1: how long it has been compressing
    float historyCoeff = 0.0f;

    juce::SmoothedValue<float> makeup, mix;
    std::atomic<float> meterDb { 0.0f };
};

} // namespace linen
