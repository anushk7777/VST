#pragma once

#include <juce_dsp/juce_dsp.h>

#include "Biquad.h"

namespace linen
{

/** 1073-style EQ settings. Frequencies are the console's switch positions
    (index 0 = off); gains are continuous. */
struct EqParams
{
    static constexpr std::array<float, 5> highPassHz { 0.0f, 50.0f, 80.0f, 160.0f, 300.0f };
    static constexpr std::array<float, 5> lowShelfHz { 0.0f, 35.0f, 60.0f, 110.0f, 220.0f };
    static constexpr std::array<float, 7> midHz      { 0.0f, 360.0f, 700.0f, 1600.0f, 3200.0f, 4800.0f, 7200.0f };
    static constexpr float highShelfHz = 12000.0f;

    int highPass = 0;
    int lowFreq = 2;          // 60 Hz
    float lowGainDb = 0.0f;   // ±16
    int midFreq = 4;          // 3.2 kHz
    float midGainDb = 0.0f;   // ±18
    float highGainDb = 0.0f;  // ±16
};

/** Four-band, 1073-style EQ: 18 dB/oct high-pass, low shelf with a gentle
    bump, inductor-style mid bell (it narrows as you push it harder) and a
    12 kHz high shelf. Gains glide; coefficients are redesigned every 16
    samples while they do. */
class Eq1073
{
public:
    void prepare (double sampleRate, int numChannels);
    void reset();
    void setParams (const EqParams& p);
    void process (juce::AudioBuffer<float>& buffer);

    /** The combined magnitude response at hz for the current settings (for tests/drawing). */
    double magnitudeDb (double hz) const;

    static double midQ (float gainDb) { return 0.7 + 0.045 * std::abs (gainDb); }

private:
    void design();

    double sampleRate = 48000.0;
    int numChannels = 0;
    EqParams params;
    bool needsDesign = true;

    juce::SmoothedValue<float> lowGain, midGain, highGain;

    enum Stage { hp1, hp2, lowShelf, mid, highShelf, numStages };
    std::array<Biquad::Coeffs, numStages> coeffs;
    std::vector<std::array<Biquad, numStages>> filters;
};

} // namespace linen
