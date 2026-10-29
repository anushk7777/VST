#include "Preamp.h"

namespace linen
{

namespace
{
    constexpr int oversamplingOrder = 2;          // 2^2 = 4x
    constexpr float bypassBelow = 0.04f;          // Drive under this fades to the untouched signal

    // Drive law: how hard each stage is hit, rising evenly in dB across the
    // knob up to its value at 100%. Tuned on sines at vocal levels
    // (tests/PreampEqTests.cpp holds the budgets):
    //   class-A, 1 kHz at -6 dBFS peak: about -55 dB THD at 30%, -30 dB at 100%;
    //   irons, 100 Hz at -6 dBFS peak: under -50 dB at 30%, thick at 100%.
    constexpr float classAAtFull = 0.36f, classARangeDb = 36.0f;
    constexpr float ironAtFull = 0.41f, ironRangeDb = 13.0f;
    constexpr float outputIronShare = 0.5f;     // the output iron is driven half as hard

    // The Cotton iron: the input transformer's leftover magnetism. A trace at
    // the house drive (its budget leaves no room), growing past 30% Drive so
    // pushed lows turn round and 2nd-heavy while the top stays glassy.
    constexpr float remanenceHouse = 0.012f, remanencePushed = 0.15f;

    float remanenceFor (float drive01)
    {
        const float t = juce::jlimit (0.0f, 1.0f, (drive01 - 0.3f) / 0.7f);
        return remanenceHouse + remanencePushed * t * t * (3.0f - 2.0f * t);
    }

    // Class-A bias: leans from quiet to bloom as the voice nears a belt.
    constexpr float biasQuiet = 0.10f, biasBloom = 0.15f, beltPeak = 0.5f;

    float lawGain (float drive01, float atFull, float rangeDb)
    {
        return atFull * juce::Decibels::decibelsToGain (-(1.0f - drive01) * rangeDb);
    }

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

float Preamp::Transformer::process (float x, float fluxGain, float remanence) noexcept
{
    // Leaky integrator → saturate the flux → matching differentiator.
    // Doubles keep the differentiator from amplifying rounding noise.
    // Remanence offsets the core's operating point, so it saturates a little
    // earlier one way than the other: even harmonics, and only where the flux
    // is large, which is in the lows (the integrator's flux falls with frequency).
    const double gain = fluxGain, r = remanence;
    const double f = (double) flux * (1.0 - leak) + (double) k * x;
    const double saturated = r > 0.0 ? (soft (f * gain + r) - soft (r)) / (gain * softSlope ((float) r))
                                     : soft (f * gain) / gain;
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

        // Output iron: a slight resonance above the audio band (the top "sheen"
        // these transformers are known for), then a gentle roll-off.
        c.outputLowPass.c = Biquad::Coeffs::lowPass (overSampleRate, 40000.0, 0.85);
        c.dcBlock.c = Biquad::Coeffs::highPassFirstOrder (sampleRate, 8.0);
    }

    envAttack = (float) std::exp (-1.0 / (0.002 * overSampleRate));
    envRelease = (float) std::exp (-1.0 / (0.090 * overSampleRate));

    drive.reset (sampleRate, 0.03);
    driveRamp.assign ((size_t) maxBlockSize, 0.0f);
    classARamp.assign ((size_t) maxBlockSize, 0.0f);
    ironRamp.assign ((size_t) maxBlockSize, 0.0f);
    remanenceRamp.assign ((size_t) maxBlockSize, 0.0f);

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
        const float d = drive.getNextValue();
        driveRamp[(size_t) i] = d;
        classARamp[(size_t) i] = lawGain (d, classAAtFull, classARangeDb);
        ironRamp[(size_t) i] = lawGain (d, ironAtFull, ironRangeDb);
        remanenceRamp[(size_t) i] = remanenceFor (d);
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

        float bias = biasQuiet, biasOffset = 0.0f, biasNorm = 1.0f, ga = 1.0f, iron = 1.0f, remanence = 0.0f;

        for (int i = 0; i < (int) up.getNumSamples(); ++i)
        {
            if (i % factor == 0)
            {
                iron = ironRamp[(size_t) (i / factor)];
                remanence = remanenceRamp[(size_t) (i / factor)];
            }

            float s = c.inputIron.process (x[i], iron, remanence);

            // Class-A stage: bias leans further as the voice gets louder. The
            // bias only moves at envelope speed, so refresh it once per input sample.
            const float level = std::abs (s);
            c.envelope = level + (c.envelope - level) * (level > c.envelope ? envAttack : envRelease);

            if (i % factor == 0)
            {
                ga = classARamp[(size_t) (i / factor)];
                bias = biasQuiet + biasBloom * juce::jmin (1.0f, c.envelope / beltPeak);
                biasOffset = soft (bias);
                biasNorm = 1.0f / (ga * softSlope (bias));
            }

            s = (soft (ga * s + bias) - biasOffset) * biasNorm;

            s = c.outputLowPass.process (s);
            x[i] = c.outputIron.process (s, iron * outputIronShare);
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
