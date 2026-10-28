#include "PainEngine.h"

namespace linen
{

namespace
{
    constexpr int oversamplingOrder = 2;            // 4x
    constexpr float bypassBelow = 0.02f;            // Pain under this fades to the untouched signal
    constexpr double flutterBaseMs = 1.5, flutterMaxMs = 0.45;
    constexpr float sagMakeupDb = 6.6f;             // what the sagging rail takes from a full-stress belt

    float soft (float x) noexcept { return x / std::sqrt (1.0f + x * x); }

    float softSlope (float x) noexcept
    {
        const float d = 1.0f + x * x;
        return 1.0f / (d * std::sqrt (d));
    }

    /** The starved plate: the positive side runs out of headroom first. */
    float plate (float u, float positiveRail) noexcept
    {
        return u >= 0.0f ? positiveRail * soft (u / positiveRail) : soft (u);
    }

    float coefficient (double seconds, double rate) { return (float) std::exp (-1.0 / (seconds * rate)); }
}

float PainEngine::compensationDb (float pain01)
{
    // Measured on a sung phrase at the house settings (tests/PainTests.cpp):
    // throat-ache lift and extra preamp density add up to about 1 dB.
    return -1.07f * std::pow (juce::jlimit (0.0f, 1.0f, pain01), 0.8f);
}

void PainEngine::prepare (double newSampleRate, int maxBlockSize, int newNumChannels)
{
    sampleRate = newSampleRate;
    numChannels = newNumChannels;
    const int chans = juce::jmax (1, numChannels);
    const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlockSize, (juce::uint32) chans };

    oversampling = std::make_unique<juce::dsp::Oversampling<float>> ((size_t) chans, oversamplingOrder,
                                                                     juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple,
                                                                     true, true);
    oversampling->initProcessing ((size_t) maxBlockSize);
    oversamplingLatency = juce::roundToInt (oversampling->getLatencyInSamples());
    flutterBase = juce::roundToInt (flutterBaseMs * 0.001 * sampleRate);
    latency = oversamplingLatency + flutterBase;

    flutter.prepare (spec);
    flutter.setMaximumDelayInSamples (flutterBase * 2 + 8);

    dryDelay.prepare (spec);
    dryDelay.setMaximumDelayInSamples (latency + 1);
    dryDelay.setDelay ((float) latency);
    dry.setSize (chans, maxBlockSize, false, false, true);
    band.setSize (chans, maxBlockSize, false, false, true);

    strainHp.c = Biquad::Coeffs::highPass (sampleRate, 1800.0, 0.707);
    strainLp.c = Biquad::Coeffs::lowPass (sampleRate, 4200.0, 0.707);
    reflexAttack = coefficient (0.0015, sampleRate);
    reflexRelease = coefficient (0.040, sampleRate);
    fatigueAttack = coefficient (0.250, sampleRate);
    fatigueRelease = coefficient (1.200, sampleRate);

    throatBand.prepare (spec);
    throatBand.setType (juce::dsp::StateVariableTPTFilterType::bandpass);
    throatBand.setCutoffFrequency (1820.0f);   // geometric centre of 1.1-3 kHz
    throatBand.setResonance (0.95f);
    throatCoeff = coefficient (0.010, sampleRate);
    sagMakeupCoeff = coefficient (0.025, sampleRate);

    hysteresis.assign ((size_t) chans, 0.0f);
    dcBlock.assign ((size_t) chans, {});
    for (auto& d : dcBlock)
        d.c = Biquad::Coeffs::highPassFirstOrder (sampleRate, 8.0);

    // Tremor: noise band-passed to 4-9 Hz, normalised so its peaks sit near ±1.
    tremorHp.c = Biquad::Coeffs::highPass (sampleRate, 4.0, 0.707);
    tremorLp1.c = Biquad::Coeffs::lowPass (sampleRate, 9.0, 0.707);
    tremorLp2.c = Biquad::Coeffs::lowPass (sampleRate, 9.0, 0.707);
    {
        juce::Random calibration (7);
        double sum = 0.0;
        const int count = (int) (sampleRate * 4.0);
        for (int i = 0; i < count; ++i)
        {
            const float v = tremorLp2.process (tremorLp1.process (tremorHp.process (calibration.nextFloat() * 2.0f - 1.0f)));
            if (i > count / 4)
                sum += (double) v * v;
        }
        tremorNorm = (float) (1.0 / (2.5 * std::sqrt (sum / (count - count / 4 - 1)) + 1.0e-12));
    }

    pain.reset (sampleRate, 0.025);
    makeup.reset (sampleRate, 0.025);
    painRamp.assign ((size_t) maxBlockSize, 0.0f);
    stressRamp.assign ((size_t) maxBlockSize, 0.0f);
    makeupRamp.assign ((size_t) maxBlockSize, 1.0f);

    reset();
}

void PainEngine::reset()
{
    if (oversampling != nullptr)
        oversampling->reset();

    flutter.reset();
    dryDelay.reset();
    throatBand.reset();
    for (auto* b : { &strainHp, &strainLp, &tremorHp, &tremorLp1, &tremorLp2 })
        b->reset();
    for (auto& d : dcBlock)
        d.reset();
    std::fill (hysteresis.begin(), hysteresis.end(), 0.0f);

    reflex = fatigue = throatPower = sagMakeup = 0.0f;
    noise.setSeed (0x9a1);
    stressMeter.store (0.0f);
    pain.setCurrentAndTargetValue (pain.getTargetValue());
    makeup.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (compensationDb (pain.getTargetValue())));
    wasActive = false;
}

void PainEngine::process (juce::AudioBuffer<float>& buffer)
{
    const int n = buffer.getNumSamples();
    const int chans = juce::jmin (numChannels, buffer.getNumChannels());
    if (chans == 0 || n == 0)
        return;

    makeup.setTargetValue (juce::Decibels::decibelsToGain (compensationDb (pain.getTargetValue())));

    // The nervous system: strain-band reflex and fatigue → stress.
    const float norm = 1.0f / (float) chans;
    float peakStress = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        painRamp[(size_t) i] = pain.getNextValue();
        makeupRamp[(size_t) i] = makeup.getNextValue();

        float x = 0.0f;
        for (int ch = 0; ch < chans; ++ch)
            x += buffer.getSample (ch, i);
        const float strain = std::abs (strainLp.process (strainHp.process (x * norm)));

        reflex = strain + (reflex - strain) * (strain > reflex ? reflexAttack : reflexRelease);
        fatigue = strain + (fatigue - strain) * (strain > fatigue ? fatigueAttack : fatigueRelease);

        const float reflexDb = juce::Decibels::gainToDecibels (reflex, -120.0f);
        const float fatigueDb = juce::Decibels::gainToDecibels (fatigue, -120.0f);
        const float r = juce::jlimit (0.0f, 1.0f, (reflexDb + 30.0f) / 24.0f);   // -30..-6 dBFS in the strain band
        const float f = juce::jlimit (0.0f, 1.0f, (fatigueDb + 34.0f) / 24.0f);  // -34..-10 dBFS, sustained

        const float stress = painRamp[(size_t) i] * (1.0f - (1.0f - r) * (1.0f - f));
        stressRamp[(size_t) i] = stress;
        peakStress = juce::jmax (peakStress, stress);

        // Give back what the sagging rail takes, so belts keep their level.
        sagMakeup = stress + (sagMakeup - stress) * sagMakeupCoeff;
        makeupRamp[(size_t) i] *= juce::Decibels::decibelsToGain (sagMakeupDb * sagMakeup);
    }
    stressMeter.store (peakStress, std::memory_order_relaxed);

    // Latency-matched dry copy, for the bypass and the fade-in below 2%.
    for (int ch = 0; ch < chans; ++ch)
    {
        const auto* in = buffer.getReadPointer (ch);
        auto* d = dry.getWritePointer (ch);
        for (int i = 0; i < n; ++i)
        {
            dryDelay.pushSample (ch, in[i]);
            d[i] = dryDelay.popSample (ch);
        }
    }

    const bool active = painRamp[(size_t) (n - 1)] > 0.0f || pain.isSmoothing();
    if (! active)
    {
        for (int ch = 0; ch < chans; ++ch)
            buffer.copyFrom (ch, 0, dry, ch, 0, n);
        wasActive = false;
        return;
    }

    if (! wasActive)
    {
        // Waking up: start every stage from silence rather than stale history.
        oversampling->reset();
        flutter.reset();
        throatBand.reset();
        for (auto& d : dcBlock)
            d.reset();
        std::fill (hysteresis.begin(), hysteresis.end(), 0.0f);
        wasActive = true;
    }

    processWet (buffer, n, chans);

    for (int ch = 0; ch < chans; ++ch)
    {
        auto* wet = buffer.getWritePointer (ch);
        const auto* d = dry.getReadPointer (ch);
        for (int i = 0; i < n; ++i)
        {
            const float w = juce::jmin (1.0f, painRamp[(size_t) i] / bypassBelow);
            if (w < 1.0f)
                wet[i] = d[i] + (wet[i] - d[i]) * w;
        }
    }
}

void PainEngine::processWet (juce::AudioBuffer<float>& buffer, int n, int chans)
{
    // 1. Starved-plate breakdown, at 4x.
    {
        juce::dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(), (size_t) chans, (size_t) n);
        auto up = oversampling->processSamplesUp (block);
        const int factor = 1 << oversamplingOrder;

        for (int ch = 0; ch < chans; ++ch)
        {
            auto* x = up.getChannelPointer ((size_t) ch);
            float& h = hysteresis[(size_t) ch];

            for (int j = 0; j < (int) up.getNumSamples(); ++j)
            {
                const float p = painRamp[(size_t) (j / factor)];
                const float stress = stressRamp[(size_t) (j / factor)];

                const float drive = 1.0f + 2.5f * p * (0.3f + 0.7f * stress);
                const float rail = 1.0f - 0.55f * stress;          // B+ sag
                const float bias = 0.12f * stress;                  // bias starvation
                const float memory = 0.08f * stress;                // a hint of hysteresis

                const float u = drive * x[j] + bias + memory * h;
                const float y = (plate (u, rail) - plate (bias, rail)) / (drive * softSlope (bias / rail));
                h = y;
                x[j] = y;
            }
        }

        oversampling->processSamplesDown (block);

        for (int ch = 0; ch < chans; ++ch)
        {
            auto* x = buffer.getWritePointer (ch);
            auto& dc = dcBlock[(size_t) ch];
            for (int i = 0; i < n; ++i)
                x[i] = dc.process (x[i]);
        }
    }

    // 2. Throat ache: upward compression of the quiet 1.1-3 kHz band, in parallel.
    for (int ch = 0; ch < chans; ++ch)
    {
        const auto* x = buffer.getReadPointer (ch);
        auto* b = band.getWritePointer (ch);
        for (int i = 0; i < n; ++i)
            b[i] = throatBand.processSample (ch, x[i]);
    }

    const float norm = 1.0f / (float) chans;
    for (int i = 0; i < n; ++i)
    {
        float power = 0.0f;
        for (int ch = 0; ch < chans; ++ch)
            power += band.getSample (ch, i) * band.getSample (ch, i);
        power *= norm;
        throatPower = power + (throatPower - power) * throatCoeff;

        const float levelDb = 10.0f * std::log10 (throatPower + 1.0e-12f) + 3.01f;
        float liftDb = juce::jlimit (0.0f, 12.0f, (-18.0f - levelDb) * 0.5f);
        liftDb *= juce::jlimit (0.0f, 1.0f, (levelDb + 54.0f) / 12.0f);   // never lift the noise floor

        const float amount = painRamp[(size_t) i] * 0.6f * (juce::Decibels::decibelsToGain (liftDb) - 1.0f);
        for (int ch = 0; ch < chans; ++ch)
            buffer.getWritePointer (ch)[i] += amount * band.getSample (ch, i);
    }

    // 3. Trauma flutter, then 4. level compensation.
    const float maxExcursion = (float) (flutterMaxMs * 0.001 * sampleRate);
    for (int i = 0; i < n; ++i)
    {
        const float tremor = juce::jlimit (-1.0f, 1.0f, tremorNorm * tremorLp2.process (tremorLp1.process (
                                 tremorHp.process (noise.nextFloat() * 2.0f - 1.0f))));
        const float delay = (float) flutterBase + maxExcursion * painRamp[(size_t) i] * stressRamp[(size_t) i] * tremor;

        for (int ch = 0; ch < chans; ++ch)
        {
            auto* x = buffer.getWritePointer (ch);
            flutter.pushSample (ch, x[i]);
            x[i] = flutter.popSample (ch, delay) * makeupRamp[(size_t) i];
        }
    }
}

} // namespace linen
