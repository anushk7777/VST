#include "ToneShelves.h"

namespace linen
{

namespace
{
    constexpr int redesignInterval = 16;

    // A flat shelf's leftover filter memory below this (about -240 dBFS) is
    // cleared, and the shelf is skipped from then on.
    constexpr double settledBelow = 1.0e-12;

    ToneParams limited (const ToneParams& p)
    {
        ToneParams r;
        r.lowHz = juce::jlimit (ToneParams::minLowHz, ToneParams::maxLowHz, p.lowHz);
        r.highHz = juce::jlimit (ToneParams::minHighHz, ToneParams::maxHighHz, p.highHz);
        r.lowGainDb = juce::jlimit (-ToneParams::maxGainDb, ToneParams::maxGainDb, p.lowGainDb);
        r.highGainDb = juce::jlimit (-ToneParams::maxGainDb, ToneParams::maxGainDb, p.highGainDb);
        return r;
    }
}

void ToneShelves::prepare (double newSampleRate, int newNumChannels)
{
    sampleRate = newSampleRate;
    numChannels = newNumChannels;
    filters.assign ((size_t) numChannels, {});

    lowGain.reset (sampleRate, 0.03);
    highGain.reset (sampleRate, 0.03);
    lowHz.reset (sampleRate, 0.03);
    highHz.reset (sampleRate, 0.03);

    reset();
}

void ToneShelves::reset()
{
    lowGain.setCurrentAndTargetValue (params.lowGainDb);
    highGain.setCurrentAndTargetValue (params.highGainDb);
    lowHz.setCurrentAndTargetValue (params.lowHz);
    highHz.setCurrentAndTargetValue (params.highHz);

    for (auto& stages : filters)
        for (auto& f : stages)
            f.reset();

    design();
    passing = { juce::exactlyEqual (params.lowGainDb, 0.0f), juce::exactlyEqual (params.highGainDb, 0.0f) };
}

void ToneShelves::setParams (const ToneParams& p)
{
    params = limited (p);
    lowGain.setTargetValue (params.lowGainDb);
    highGain.setTargetValue (params.highGainDb);
    lowHz.setTargetValue (params.lowHz);
    highHz.setTargetValue (params.highHz);
}

Biquad::Coeffs ToneShelves::designStage (Stage stage, double rate, float hz, float gainDb)
{
    // At exactly 0 dB the RBJ shelf is an identity (b == a), so a gain that
    // glides back to 0 lets the filter's memory die away smoothly rather
    // than cutting it off with a click.
    return stage == low ? Biquad::Coeffs::lowShelf (rate, hz, shelfQ, gainDb)
                        : Biquad::Coeffs::highShelf (rate, hz, shelfQ, gainDb);
}

void ToneShelves::design()
{
    coeffs[low] = designStage (low, sampleRate, lowHz.getCurrentValue(), lowGain.getCurrentValue());
    coeffs[high] = designStage (high, sampleRate, highHz.getCurrentValue(), highGain.getCurrentValue());

    for (auto& stages : filters)
        for (int s = 0; s < numStages; ++s)
            stages[(size_t) s].c = coeffs[(size_t) s];

    needsDesign = false;
}

void ToneShelves::process (juce::AudioBuffer<float>& buffer)
{
    const int n = buffer.getNumSamples();
    const int chans = juce::jmin (numChannels, buffer.getNumChannels());

    for (int start = 0; start < n; start += redesignInterval)
    {
        const int len = juce::jmin (redesignInterval, n - start);
        const bool gliding = lowGain.isSmoothing() || highGain.isSmoothing() || lowHz.isSmoothing() || highHz.isSmoothing();

        if (gliding)
        {
            lowGain.skip (len);
            highGain.skip (len);
            lowHz.skip (len);
            highHz.skip (len);
        }

        if (gliding || needsDesign)
            design();

        // A shelf with any gain (or on its way to or from one) runs.
        if (lowGain.isSmoothing() || ! juce::exactlyEqual (lowGain.getCurrentValue(), 0.0f))
            passing[low] = false;
        if (highGain.isSmoothing() || ! juce::exactlyEqual (highGain.getCurrentValue(), 0.0f))
            passing[high] = false;

        if (passing[low] && passing[high])
            continue;

        for (int ch = 0; ch < chans; ++ch)
        {
            auto* data = buffer.getWritePointer (ch, start);
            auto& stages = filters[(size_t) ch];

            for (int s = 0; s < numStages; ++s)
                if (! passing[(size_t) s])
                    for (int i = 0; i < len; ++i)
                        data[i] = stages[(size_t) s].process (data[i]);
        }
    }

    settle();
}

void ToneShelves::settle()
{
    for (int s = 0; s < numStages; ++s)
    {
        const auto& gain = s == low ? lowGain : highGain;
        if (passing[(size_t) s] || gain.isSmoothing() || ! juce::exactlyEqual (gain.getCurrentValue(), 0.0f))
            continue;

        bool quiet = true;
        for (const auto& stages : filters)
            quiet = quiet && std::abs (stages[(size_t) s].s1) < settledBelow && std::abs (stages[(size_t) s].s2) < settledBelow;

        if (quiet)
        {
            for (auto& stages : filters)
                stages[(size_t) s].reset();
            passing[(size_t) s] = true;
        }
    }
}

double ToneShelves::magnitudeDb (double hz) const
{
    return coeffs[low].magnitudeDb (sampleRate, hz) + coeffs[high].magnitudeDb (sampleRate, hz);
}

double ToneShelves::magnitudeDb (const ToneParams& p, double rate, double hz)
{
    const auto q = limited (p);
    return designStage (low, rate, q.lowHz, q.lowGainDb).magnitudeDb (rate, hz)
         + designStage (high, rate, q.highHz, q.highGainDb).magnitudeDb (rate, hz);
}

} // namespace linen
