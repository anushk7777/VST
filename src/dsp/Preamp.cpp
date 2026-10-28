#include "Preamp.h"

namespace linen
{

namespace
{
    constexpr int oversamplingOrder = 2;          // 2^2 = 4x
    constexpr float maxDriveDb = 18.0f;
    constexpr float classAHeadroom = 0.35f;     // class-A stage sees drive × this
    constexpr float bypassBelow = 0.04f;          // Drive under this fades to the untouched signal

    float driveGainFor (float drive01) { return juce::Decibels::decibelsToGain (drive01 * maxDriveDb); }

    /** Smooth soft-saturation curve, x / sqrt(1 + x²): tanh-like, but several
        times cheaper and smooth everywhere (no piecewise joins, which the
        transformer's differentiator would turn into clicks). Slope 1 at 0. */
    template <typename T>
    T soft (T x) noexcept { return x / std::sqrt (T (1) + x * x); }

    /** Its slope at x, for normalising the biased class-A stage. */
    float softSlope (float x) noexcept
    {
        const float d = 1.0f + x * x;
        return 1.0f / (d * std::sqrt (d));
    }
}

float Preamp::Transformer::process (float x, float driveGain) noexcept
{
    // Leaky integrator → saturate the flux → matching differentiator.
    // Doubles keep the differentiator from amplifying rounding noise.
    const double gain = (double) fluxGain * driveGain;
    const double f = (double) flux * (1.0 - leak) + (double) k * x;
    const double saturated = soft (f * gain) / gain;
    const double y = (saturated - (double) lastSaturated * (1.0 - leak)) / k;

    flux = (float) f;
    lastSaturated = (float) saturated;
    return (float) y;
}

void Preamp::prepare (double newSampleRate, int maxBlockSize, int newNumChannels)
{
    sampleRate = newSampleRate;
    numChannels = newNumChannels;

    // Linear-phase FIR halfbands: dearer than IIR, but the preamp's output then
    // stays phase-aligned with the dry path, so Mix never combs the top end.
    oversampling = std::make_unique<juce::dsp::Oversampling<float>> ((size_t) juce::jmax (1, numChannels), oversamplingOrder,
                                                                     juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple,
                                                                     true, true);
    oversampling->initProcessing ((size_t) maxBlockSize);
    latency = juce::roundToInt (oversampling->getLatencyInSamples());
    overSampleRate = sampleRate * (1 << oversamplingOrder);

    channels.assign ((size_t) numChannels, {});
    for (auto& c : channels)
    {
        for (auto* iron : { &c.inputIron, &c.outputIron })
        {
            iron->k = (float) (juce::MathConstants<double>::twoPi * 90.0 / overSampleRate);
            iron->leak = (float) (juce::MathConstants<double>::twoPi * 6.0 / overSampleRate);
        }
        c.inputIron.fluxGain = 0.35f;
        c.outputIron.fluxGain = 0.18f;

        // Output iron: a slight resonance above the audio band (the top "sheen"
        // these transformers are known for), then a gentle roll-off.
        c.outputLowPass.c = Biquad::Coeffs::lowPass (overSampleRate, 40000.0, 0.85);
        c.dcBlock.c = Biquad::Coeffs::highPassFirstOrder (sampleRate, 8.0);
    }

    envAttack = (float) std::exp (-1.0 / (0.002 * overSampleRate));
    envRelease = (float) std::exp (-1.0 / (0.090 * overSampleRate));

    drive.reset (sampleRate, 0.03);
    driveRamp.assign ((size_t) maxBlockSize, 0.0f);
    gainRamp.assign ((size_t) maxBlockSize, 1.0f);

    const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlockSize, (juce::uint32) juce::jmax (1, numChannels) };
    dryDelay.prepare (spec);
    dryDelay.setMaximumDelayInSamples (latency + 1);
    dryDelay.setDelay ((float) latency);
    dry.setSize (juce::jmax (1, numChannels), maxBlockSize, false, false, true);

    reset();
}

void Preamp::reset()
{
    if (oversampling != nullptr)
        oversampling->reset();

    for (auto& c : channels)
    {
        c.inputIron.reset();
        c.outputIron.reset();
        c.envelope = 0.0f;
        c.outputLowPass.reset();
        c.dcBlock.reset();
    }

    dryDelay.reset();
    drive.setCurrentAndTargetValue (drive.getTargetValue());
}

void Preamp::process (juce::AudioBuffer<float>& buffer)
{
    const int n = buffer.getNumSamples();
    const int chans = juce::jmin (numChannels, buffer.getNumChannels());
    if (chans == 0 || n == 0)
        return;

    for (int i = 0; i < n; ++i)
    {
        driveRamp[(size_t) i] = drive.getNextValue();
        gainRamp[(size_t) i] = driveGainFor (driveRamp[(size_t) i]);
    }

    // Latency-matched copy of the input, for the Drive-0 bypass blend.
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

    // The oversampled path always runs, so its filters never hold stale audio.
    juce::dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(), (size_t) chans, (size_t) n);
    auto up = oversampling->processSamplesUp (block);
    const int factor = 1 << oversamplingOrder;

    for (int ch = 0; ch < chans; ++ch)
    {
        auto& c = channels[(size_t) ch];
        auto* x = up.getChannelPointer ((size_t) ch);

        float bias = 0.1f, biasOffset = 0.0f, biasNorm = 1.0f, ga = 1.0f;

        for (int i = 0; i < (int) up.getNumSamples(); ++i)
        {
            const float g = gainRamp[(size_t) (i / factor)];
            float s = c.inputIron.process (x[i], g);

            // Class-A stage: bias leans further as the voice gets louder. The
            // bias only moves at envelope speed, so refresh it once per input sample.
            const float level = std::abs (s);
            c.envelope = level + (c.envelope - level) * (level > c.envelope ? envAttack : envRelease);

            if (i % factor == 0)
            {
                ga = classAHeadroom * g;
                bias = 0.10f + 0.30f * juce::jmin (1.0f, c.envelope * ga);
                biasOffset = soft (bias);
                biasNorm = 1.0f / (ga * softSlope (bias));
            }

            s = (soft (ga * s + bias) - biasOffset) * biasNorm;

            s = c.outputLowPass.process (s);
            x[i] = c.outputIron.process (s, g);
        }
    }

    oversampling->processSamplesDown (block);

    for (int ch = 0; ch < chans; ++ch)
    {
        auto& c = channels[(size_t) ch];
        auto* wet = buffer.getWritePointer (ch);
        const auto* d = dry.getReadPointer (ch);

        for (int i = 0; i < n; ++i)
        {
            const float w = juce::jmin (1.0f, driveRamp[(size_t) i] / bypassBelow);
            const float processed = c.dcBlock.process (wet[i]);
            wet[i] = w >= 1.0f ? processed : d[i] + (processed - d[i]) * w;
        }
    }
}

} // namespace linen
