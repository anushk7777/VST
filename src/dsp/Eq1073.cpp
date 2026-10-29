#include "Eq1073.h"

namespace linen
{

namespace
{
    constexpr int redesignInterval = 16;

    template <size_t N>
    float pick (const std::array<float, N>& table, int index)
    {
        return table[(size_t) juce::jlimit (0, (int) N - 1, index)];
    }
}

void Eq1073::prepare (double newSampleRate, int newNumChannels)
{
    sampleRate = newSampleRate;
    numChannels = newNumChannels;
    filters.assign ((size_t) numChannels, {});

    for (auto* s : { &lowGain, &midGain, &highGain })
        s->reset (sampleRate, 0.03);

    reset();
}

void Eq1073::reset()
{
    lowGain.setCurrentAndTargetValue (params.lowGainDb);
    midGain.setCurrentAndTargetValue (params.midGainDb);
    highGain.setCurrentAndTargetValue (params.highGainDb);

    for (auto& stages : filters)
        for (auto& f : stages)
            f.reset();

    design();
}

void Eq1073::setParams (const EqParams& p)
{
    needsDesign = needsDesign || p.lowFreq != params.lowFreq || p.midFreq != params.midFreq;
    params = p;
    lowGain.setTargetValue (p.lowGainDb);
    midGain.setTargetValue (p.midGainDb);
    highGain.setTargetValue (p.highGainDb);
}

Eq1073::Design Eq1073::designFor (const EqParams& p, double rate, float lowDb, float midDb, float highDb)
{
    // Every stage always runs (an "off" stage is an identity filter), so
    // switching bands in and out never restarts a filter from cold.
    Design d;

    // Low shelf with a slight resonant bump (Q 0.9), as the inductor shelf has.
    const float lowHz = pick (EqParams::lowShelfHz, p.lowFreq);
    d[lowShelf] = lowHz > 0.0f ? Biquad::Coeffs::lowShelf (rate, lowHz, 0.9, lowDb) : Biquad::Coeffs::identity();

    const float midHz = pick (EqParams::midHz, p.midFreq);
    d[mid] = midHz > 0.0f ? Biquad::Coeffs::peak (rate, midHz, midQ (midDb), midDb) : Biquad::Coeffs::identity();

    d[highShelf] = Biquad::Coeffs::highShelf (rate, EqParams::highShelfHz, 0.6, highDb);
    return d;
}

void Eq1073::design()
{
    coeffs = designFor (params, sampleRate, lowGain.getCurrentValue(), midGain.getCurrentValue(), highGain.getCurrentValue());

    for (auto& stages : filters)
        for (int s = 0; s < numStages; ++s)
            stages[(size_t) s].c = coeffs[(size_t) s];

    needsDesign = false;
}

void Eq1073::process (juce::AudioBuffer<float>& buffer)
{
    const int n = buffer.getNumSamples();
    const int chans = juce::jmin (numChannels, buffer.getNumChannels());

    for (int start = 0; start < n; start += redesignInterval)
    {
        const int len = juce::jmin (redesignInterval, n - start);
        const bool gliding = lowGain.isSmoothing() || midGain.isSmoothing() || highGain.isSmoothing();

        if (gliding)
        {
            lowGain.skip (len);
            midGain.skip (len);
            highGain.skip (len);
        }

        if (gliding || needsDesign)
            design();

        for (int ch = 0; ch < chans; ++ch)
        {
            auto* data = buffer.getWritePointer (ch, start);
            auto& stages = filters[(size_t) ch];

            for (auto& f : stages)
                for (int i = 0; i < len; ++i)
                    data[i] = f.process (data[i]);
        }
    }
}

double Eq1073::magnitudeDb (double hz) const
{
    double total = 0.0;
    for (const auto& c : coeffs)
        total += c.magnitudeDb (sampleRate, hz);
    return total;
}

double Eq1073::magnitudeDb (const EqParams& p, double rate, double hz)
{
    double total = 0.0;
    for (const auto& c : designFor (p, rate, p.lowGainDb, p.midGainDb, p.highGainDb))
        total += c.magnitudeDb (rate, hz);
    return total;
}

} // namespace linen
