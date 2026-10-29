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
    float releaseMs = 60.0f;    // the fast half of the release
    bool autoMakeup = true;
    float mix = 1.0f;           // parallel blend
    float cotton = 0.0f;        // 0..1: texture while compressing
};

/** The "invisible" compressor, with the Cotton texture.

    Detection: feed-forward, stereo-linked RMS through a 100 Hz high-pass (so
    plosives and boom don't pump it) and a gentle presence lift (an optical
    cell reacts more to bright energy), into a wide 10 dB soft knee.

    Release, after the optical cell: half of the gain reduction lets go at
    the Release time; the other half lets go slowly, from 0.4 s up to 5 s the
    longer and harder it has been holding (the cell's "memory"). Heavy,
    sustained compression therefore never pumps.

    Cotton, only while it compresses, and in proportion to how hard:
      - diode-bridge style harmonics (mostly 2nd, some 3rd), at 2x oversampling;
      - a touch of low-mid warmth and a softer top, so loud lines get rounder
        and warmer while quiet, breathy ones stay open.
    With no gain reduction, or Cotton at 0, the signal path is a pure gain. */
class SilkCompressor
{
public:
    static constexpr float kneeDb = 10.0f;

    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset();
    void setParams (const CompParams& p) { params = p; }
    void process (juce::AudioBuffer<float>& buffer);

    /** Constant after prepare(): the oversampled colour path's latency. */
    int getLatencySamples() const noexcept { return latency; }

    /** Static curve: gain change in dB for a steady input level (negative = reduction). */
    static float staticGainDb (float inputDb, float thresholdDb, float ratio);

    /** The automatic make-up gain for these settings, in dB (0 with Auto gain off). */
    static float makeupDb (const CompParams& p);

    /** Gain reduction right now, in positive dB (for the meter). */
    float getGainReductionDb() const noexcept { return meterDb.load (std::memory_order_relaxed); }

private:
    void updateTexture (float reduction);

    double sampleRate = 48000.0;
    int numChannels = 0;
    int latency = 0;
    CompParams params;

    // Detector.
    Biquad detectorHighPass, detectorPresence;
    float rmsState = 0.0f, rmsCoeff = 0.0f;

    // Two-stage release.
    float fastStage = 0.0f, slowStage = 0.0f;   // dB of reduction, each half of the total
    float memory = 0.0f, memoryRise = 0.0f, memoryFall = 0.0f;

    juce::SmoothedValue<float> makeup, mix;
    std::vector<float> gainRamp, colourRamp;
    std::atomic<float> meterDb { 0.0f };

    // Cotton: harmonics at 2x, added as a difference so "no colour" is exact.
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    juce::AudioBuffer<float> compressed, colour, dry;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> alignDelay, dryDelay;
    std::vector<Biquad> colourDcBlock;

    // Cotton: GR-coupled warmth and soft top, redesigned every 16 samples.
    std::vector<std::array<Biquad, 2>> texture;
    float textureAmount = 0.0f;
};

} // namespace linen
