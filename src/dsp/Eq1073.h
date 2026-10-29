#pragma once

#include <juce_dsp/juce_dsp.h>

#include "Biquad.h"

namespace linen
{

/** 1073-style EQ settings. Frequencies are the console's switch positions
    (index 0 = off); gains are continuous. */
struct EqParams
{
    static constexpr std::array<float, 5> lowShelfHz { 0.0f, 35.0f, 60.0f, 110.0f, 220.0f };
    static constexpr std::array<float, 7> midHz      { 0.0f, 360.0f, 700.0f, 1600.0f, 3200.0f, 4800.0f, 7200.0f };
    static constexpr float highShelfHz = 12000.0f;

    int lowFreq = 2;          // 60 Hz
    float lowGainDb = 0.0f;   // ±16
    int midFreq = 4;          // 3.2 kHz
    float midGainDb = 0.0f;   // ±18
    float highGainDb = 0.0f;  // ±16
};

/** Three-band, 1073-style EQ: low shelf with a gentle bump, inductor-style
    mid bell (it narrows as you push it harder) and a 12 kHz high shelf.
    Gains glide; coefficients are redesigned every 16 samples while they do.
    (Low-end clean-up happens before the preamp, in the Tone shelves.) */
class Eq1073
{
public:
    void prepare (double sampleRate, int numChannels);
    void reset();
    void setParams (const EqParams& p);
    void process (juce::AudioBuffer<float>& buffer);

    /** The combined magnitude response at hz for the current settings (for tests/drawing). */
    double magnitudeDb (double hz) const;

    /** The combined magnitude response of given settings (for the display). */
    static double magnitudeDb (const EqParams& p, double sampleRate, double hz);

    static double midQ (float gainDb) { return 0.7 + 0.045 * std::abs (gainDb); }

private:
    enum Stage { lowShelf, mid, highShelf, numStages };
    using Design = std::array<Biquad::Coeffs, numStages>;

    static Design designFor (const EqParams& p, double sampleRate, float lowDb, float midDb, float highDb);
    void design();

    double sampleRate = 48000.0;
    int numChannels = 0;
    EqParams params;
    bool needsDesign = true;

    juce::SmoothedValue<float> lowGain, midGain, highGain;

    Design coeffs;
    std::vector<std::array<Biquad, numStages>> filters;
};

} // namespace linen
