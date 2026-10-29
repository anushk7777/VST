#pragma once

#include <juce_dsp/juce_dsp.h>

#include "Biquad.h"

namespace linen
{

/** Tone settings: a low and a high shelf, each with a free frequency and a
    gain. 0 dB is off. */
struct ToneParams
{
    static constexpr float minLowHz = 30.0f, maxLowHz = 1000.0f;
    static constexpr float minHighHz = 1000.0f, maxHighHz = 16000.0f;
    static constexpr float maxGainDb = 12.0f;

    float lowHz = 250.0f;
    float lowGainDb = 0.0f;
    float highHz = 3000.0f;
    float highGainDb = 0.0f;
};

/** Pro-Q-style shelves that shape the voice before it reaches the preamp:
    take weight out of the lows (or add some), calm or open the top, so the
    preamp is fed a balanced voice.

    Gentle, overshoot-free shelves (RBJ, Q 0.707): half the gain at the
    corner frequency, the full gain a few octaves beyond it. Gains and
    frequencies glide; coefficients are redesigned every 16 samples while
    they move. A shelf at 0 dB is an exact pass-through once its filter has
    settled, so flat settings are bit-exact. No latency. */
class ToneShelves
{
public:
    static constexpr double shelfQ = 0.707;

    void prepare (double sampleRate, int numChannels);
    void reset();
    void setParams (const ToneParams& p);
    void process (juce::AudioBuffer<float>& buffer);

    /** Combined magnitude response at hz for the current settings. */
    double magnitudeDb (double hz) const;

    /** Combined magnitude response of given settings (for the display). */
    static double magnitudeDb (const ToneParams& p, double sampleRate, double hz);

private:
    enum Stage { low, high, numStages };

    static Biquad::Coeffs designStage (Stage stage, double sampleRate, float hz, float gainDb);
    void design();
    void settle();

    double sampleRate = 48000.0;
    int numChannels = 0;
    ToneParams params;
    bool needsDesign = true;

    juce::SmoothedValue<float> lowGain, highGain;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> lowHz { 250.0f }, highHz { 3000.0f };

    std::array<Biquad::Coeffs, numStages> coeffs;
    std::array<bool, numStages> passing { true, true };   // flat and settled: skipped, exact
    std::vector<std::array<Biquad, numStages>> filters;
};

} // namespace linen
