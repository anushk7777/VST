#include "Air.h"

namespace linen
{

namespace
{
    constexpr int controlInterval = 16;
    constexpr float shelfHz = 10000.0f;
    constexpr float maxAirDb = 6.0f, maxBreathLiftDb = 3.0f, maxDeEssDb = 6.0f;
    constexpr float maxExciter = 0.18f;

    float toDb (float power) { return 10.0f * std::log10 (power + 1.0e-12f); }
}

void Air::prepare (double newSampleRate, int maxBlockSize, int newNumChannels)
{
    sampleRate = newSampleRate;
    numChannels = newNumChannels;

    vowelHp.c = Biquad::Coeffs::highPass (sampleRate, 200.0, 0.707);
    vowelLp.c = Biquad::Coeffs::lowPass (sampleRate, 2500.0, 0.707);
    sibHp.c = Biquad::Coeffs::highPass (sampleRate, 6000.0, 0.707);
    sibLp.c = Biquad::Coeffs::lowPass (sampleRate, juce::jmin (14000.0, sampleRate * 0.45), 0.707);

    powerCoeff = (float) std::exp (-1.0 / (0.005 * sampleRate));
    sibAttack = (float) std::exp (-(double) controlInterval / (0.002 * sampleRate));
    sibRelease = (float) std::exp (-(double) controlInterval / (0.060 * sampleRate));

    shelves.assign ((size_t) numChannels, {});

    // The exciter squares the 5–10 kHz band: its products reach ~20 kHz and
    // beyond, so run it at 2x to keep them from folding back down.
    exciterOversampling = std::make_unique<juce::dsp::Oversampling<float>> ((size_t) juce::jmax (1, numChannels), 1,
                                                                            juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true);
    exciterOversampling->initProcessing ((size_t) maxBlockSize);
    exciterBand.setSize (juce::jmax (1, numChannels), maxBlockSize, false, false, true);
    chunkExciterGain.assign ((size_t) (maxBlockSize / controlInterval + 1), 0.0f);

    const double os = sampleRate * 2.0;
    const double bandTop = juce::jmin (10000.0, sampleRate * 0.21);
    exciter.assign ((size_t) numChannels, {});
    for (auto& e : exciter)
    {
        // 4th-order both sides: any low vowel energy left in the band would
        // intermodulate with the top when squared.
        e.bandHp1.c = Biquad::Coeffs::highPass (sampleRate, 5000.0, 0.54);
        e.bandHp2.c = Biquad::Coeffs::highPass (sampleRate, 5000.0, 1.31);
        e.bandLp1.c = Biquad::Coeffs::lowPass (sampleRate, bandTop, 0.54);   // 4th-order Butterworth
        e.bandLp2.c = Biquad::Coeffs::lowPass (sampleRate, bandTop, 1.31);
        e.outHp1.c = Biquad::Coeffs::highPass (os, 9000.0, 0.54);           // keep only the octave-up sparkle
        e.outHp2.c = Biquad::Coeffs::highPass (os, 9000.0, 1.31);
    }
    exciterPowerCoeff = (float) std::exp (-1.0 / (0.010 * os));

    reset();
}

void Air::reset()
{
    for (auto* b : { &vowelHp, &vowelLp, &sibHp, &sibLp })
        b->reset();
    vowelPower = sibPower = 0.0f;
    sibilance = breath = 0.0f;
    sibilanceMeter.store (0.0f);

    shelfDb = 0.0f;
    exciterGain = 0.0f;
    for (auto& s : shelves)
    {
        s.reset();
        s.c = Biquad::Coeffs::identity();
    }

    for (auto& e : exciter)
    {
        for (auto* b : { &e.bandHp1, &e.bandHp2, &e.bandLp1, &e.bandLp2, &e.outHp1, &e.outHp2 })
            b->reset();
        e.power = 0.0f;
    }
    if (exciterOversampling != nullptr)
        exciterOversampling->reset();
}

void Air::updateShelf()
{
    const float vowelDb = toDb (vowelPower), sibDb = toDb (sibPower);

    // Sibilance: the top band louder than the vowel band, and loud in itself.
    const float sibTarget = juce::jlimit (0.0f, 1.0f, (sibDb - vowelDb + 2.0f) / 8.0f)
                          * juce::jlimit (0.0f, 1.0f, (sibDb + 50.0f) / 10.0f);
    sibilance = sibTarget + (sibilance - sibTarget) * (sibTarget > sibilance ? sibAttack : sibRelease);

    // Breath: little vowel, quiet but present air. Lift it a touch.
    const float breathTarget = juce::jlimit (0.0f, 1.0f, (-40.0f - vowelDb) / 15.0f)
                             * juce::jlimit (0.0f, 1.0f, (sibDb + 75.0f) / 10.0f)
                             * (1.0f - sibilance);
    breath += (breathTarget - breath) * 0.05f;

    const float open = 1.0f - sibilance;
    const float amount = juce::jlimit (0.0f, 1.0f, params.amount);
    shelfDb = amount * open * (maxAirDb + maxBreathLiftDb * breath)
            - juce::jlimit (0.0f, 1.0f, params.deEss) * maxDeEssDb * sibilance;
    exciterGain = amount * open * maxExciter;

    const auto c = std::abs (shelfDb) < 0.001f ? Biquad::Coeffs::identity()
                                               : Biquad::Coeffs::highShelf (sampleRate, shelfHz, 0.6, shelfDb);
    for (auto& s : shelves)
        s.c = c;

    sibilanceMeter.store (sibilance, std::memory_order_relaxed);
}

void Air::process (juce::AudioBuffer<float>& buffer)
{
    const int n = buffer.getNumSamples();
    const int chans = juce::jmin (numChannels, buffer.getNumChannels());
    if (chans == 0)
        return;

    const bool idle = params.amount <= 0.0f && params.deEss <= 0.0f && std::abs (shelfDb) < 0.001f && exciterGain <= 0.0f;
    const float norm = 1.0f / (float) chans;

    // Exciter band, taken from the input before the shelf.
    const bool exciting = ! idle;
    if (exciting)
    {
        for (int ch = 0; ch < chans; ++ch)
        {
            auto& e = exciter[(size_t) ch];
            const auto* in = buffer.getReadPointer (ch);
            auto* band = exciterBand.getWritePointer (ch);
            for (int i = 0; i < n; ++i)
                band[i] = e.bandLp2.process (e.bandLp1.process (e.bandHp2.process (e.bandHp1.process (in[i]))));
        }
    }

    for (int start = 0; start < n; start += controlInterval)
    {
        const int len = juce::jmin (controlInterval, n - start);

        for (int i = start; i < start + len; ++i)
        {
            float x = 0.0f;
            for (int ch = 0; ch < chans; ++ch)
                x += buffer.getSample (ch, i);
            x *= norm;

            const float v = vowelLp.process (vowelHp.process (x));
            const float s = sibLp.process (sibHp.process (x));
            vowelPower = v * v + (vowelPower - v * v) * powerCoeff;
            sibPower = s * s + (sibPower - s * s) * powerCoeff;
        }

        updateShelf();
        chunkExciterGain[(size_t) (start / controlInterval)] = exciterGain;

        if (idle)
            continue;

        for (int ch = 0; ch < chans; ++ch)
        {
            auto* d = buffer.getWritePointer (ch, start);
            auto& shelf = shelves[(size_t) ch];
            for (int i = 0; i < len; ++i)
                d[i] = shelf.process (d[i]);
        }
    }

    if (! exciting)
        return;

    // Square the band at 2x (level-normalised, so it tracks the voice), keep the octave above.
    juce::dsp::AudioBlock<float> bandBlock (exciterBand.getArrayOfWritePointers(), (size_t) chans, (size_t) n);
    auto up = exciterOversampling->processSamplesUp (bandBlock);
    for (int ch = 0; ch < chans; ++ch)
    {
        auto& e = exciter[(size_t) ch];
        auto* d = up.getChannelPointer ((size_t) ch);
        for (int i = 0; i < (int) up.getNumSamples(); ++i)
        {
            e.power = d[i] * d[i] + (e.power - d[i] * d[i]) * exciterPowerCoeff;
            const float squared = d[i] * d[i] / (std::sqrt (e.power) + 1.0e-4f);
            d[i] = e.outHp2.process (e.outHp1.process (squared));
        }
    }
    exciterOversampling->processSamplesDown (bandBlock);

    for (int ch = 0; ch < chans; ++ch)
        for (int start = 0; start < n; start += controlInterval)
            buffer.addFrom (ch, start, exciterBand, ch, start, juce::jmin (controlInterval, n - start),
                            chunkExciterGain[(size_t) (start / controlInterval)]);
}

} // namespace linen
