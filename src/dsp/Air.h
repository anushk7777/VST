#pragma once

#include <juce_dsp/juce_dsp.h>

#include "Biquad.h"

namespace linen
{

struct AirParams
{
    float amount = 0.0f;   // 0..1: up to +6 dB of air above ~10 kHz, plus sparkle
    float deEss = 0.0f;    // 0..1: up to -6 dB on S and T sounds
};

/** Air that never brings the esses back.

    A sibilance detector compares the 6–14 kHz band with the vowel band
    (200 Hz–2.5 kHz). On sung vowels and breaths the 10 kHz shelf opens (and
    lifts quiet breaths a little more); when an S or T makes the top outweigh
    the vowel, it closes and turns into a gentle cut. An even-harmonic
    exciter (the 5–10 kHz band squared, at 2x oversampling) adds sparkle
    above 10 kHz, and ducks with the shelf. */
class Air
{
public:
    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset();
    void setParams (const AirParams& p) { params = p; }
    void process (juce::AudioBuffer<float>& buffer);

    /** How strongly the current sound reads as sibilance, 0..1 (for the GUI). */
    float getSibilance() const noexcept { return sibilanceMeter.load (std::memory_order_relaxed); }

private:
    void updateShelf();

    double sampleRate = 48000.0;
    int numChannels = 0;
    AirParams params;

    // Detector (mono).
    Biquad vowelHp, vowelLp, sibHp, sibLp;
    float vowelPower = 0.0f, sibPower = 0.0f, powerCoeff = 0.0f;
    float sibilance = 0.0f, breath = 0.0f;
    float sibAttack = 0.0f, sibRelease = 0.0f;
    std::atomic<float> sibilanceMeter { 0.0f };

    // Main path: dynamic high shelf, redesigned every 16 samples.
    std::vector<Biquad> shelves;
    float shelfDb = 0.0f;

    // Exciter path.
    struct ExciterChannel
    {
        Biquad bandHp1, bandHp2, bandLp1, bandLp2, outHp1, outHp2;
        float power = 0.0f;
    };
    std::vector<ExciterChannel> exciter;
    std::unique_ptr<juce::dsp::Oversampling<float>> exciterOversampling;
    juce::AudioBuffer<float> exciterBand;
    float exciterGain = 0.0f, exciterPowerCoeff = 0.0f;
    std::vector<float> chunkExciterGain;  // per 16-sample chunk, so the layer glides
};

} // namespace linen
