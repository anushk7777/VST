#include "SilkCompressor.h"

namespace linen
{

namespace
{
    constexpr float makeupReferenceDb = -14.0f;  // a typical sung vocal's RMS
    constexpr int textureInterval = 16;

    // Cotton tuning. The harmonics are a whisper (about -42 dB at 100% and
    // 10 dB of gain reduction on a -6 dBFS vocal): the texture is felt, not
    // heard as distortion. Most of Cotton is the warmth and the softer top.
    constexpr float colourFullAtDb = 10.0f;      // gain reduction where the harmonics are fully in
    constexpr float colourScale = 0.055f;
    constexpr float textureFullAtDb = 12.0f;
    constexpr float warmthDb = 2.0f;             // low-mid bell at full texture
    constexpr float softenDb = -3.0f;            // top shelf at full texture

    float soft (float x) noexcept { return x / std::sqrt (1.0f + x * x); }

    float softSlope (float x) noexcept
    {
        const float d = 1.0f + x * x;
        return 1.0f / (d * std::sqrt (d));
    }

    /** Diode-bridge style transfer: a biased soft curve (2nd harmonic from the
        bias, 3rd from the curve), normalised to unity small-signal gain. */
    float bridge (float u) noexcept
    {
        constexpr float drive = 1.6f, bias = 0.25f;
        static const float offset = soft (bias), norm = 1.0f / (drive * softSlope (bias));
        return (soft (drive * u + bias) - offset) * norm;
    }
}

float SilkCompressor::staticGainDb (float inputDb, float thresholdDb, float ratio)
{
    const float over = inputDb - thresholdDb;
    const float slope = 1.0f / juce::jmax (1.0f, ratio) - 1.0f;

    if (over <= -kneeDb * 0.5f)
        return 0.0f;
    if (over >= kneeDb * 0.5f)
        return slope * over;

    const float x = over + kneeDb * 0.5f;
    return slope * x * x / (2.0f * kneeDb);
}

float SilkCompressor::makeupDb (const CompParams& p)
{
    // Most (not all) of the reduction a typical vocal sees, so the level
    // stays close while the compressor still sounds like it is working.
    return p.autoMakeup ? -0.6f * staticGainDb (makeupReferenceDb, p.thresholdDb, p.ratio) : 0.0f;
}

void SilkCompressor::prepare (double newSampleRate, int maxBlockSize, int newNumChannels)
{
    sampleRate = newSampleRate;
    numChannels = newNumChannels;
    const int chans = juce::jmax (1, numChannels);

    detectorHighPass.c = Biquad::Coeffs::highPass (sampleRate, 100.0, 0.707);
    detectorPresence.c = Biquad::Coeffs::peak (sampleRate, 3500.0, 0.9, 3.0);
    rmsCoeff = (float) std::exp (-1.0 / (0.010 * sampleRate));          // 10 ms RMS window
    memoryRise = (float) std::exp (-1.0 / (2.0 * sampleRate));
    memoryFall = (float) std::exp (-1.0 / (6.0 * sampleRate));

    makeup.reset (sampleRate, 0.05);
    mix.reset (sampleRate, 0.025);
    gainRamp.assign ((size_t) maxBlockSize, 1.0f);
    colourRamp.assign ((size_t) maxBlockSize, 0.0f);

    oversampling = std::make_unique<juce::dsp::Oversampling<float>> ((size_t) chans, 1,
                                                                     juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple,
                                                                     true, true);
    oversampling->initProcessing ((size_t) maxBlockSize);
    latency = juce::roundToInt (oversampling->getLatencyInSamples());

    compressed.setSize (chans, maxBlockSize, false, false, true);
    colour.setSize (chans, maxBlockSize, false, false, true);
    dry.setSize (chans, maxBlockSize, false, false, true);

    const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlockSize, (juce::uint32) chans };
    for (auto* d : { &alignDelay, &dryDelay })
    {
        d->prepare (spec);
        d->setMaximumDelayInSamples (latency + 1);
        d->setDelay ((float) latency);
    }

    colourDcBlock.assign ((size_t) chans, {});
    for (auto& b : colourDcBlock)
        b.c = Biquad::Coeffs::highPassFirstOrder (sampleRate, 10.0);

    texture.assign ((size_t) chans, {});
    reset();
}

void SilkCompressor::reset()
{
    detectorHighPass.reset();
    detectorPresence.reset();
    rmsState = 0.0f;
    fastStage = slowStage = 0.0f;
    memory = 0.0f;
    meterDb.store (0.0f);
    makeup.setCurrentAndTargetValue (1.0f);
    mix.setCurrentAndTargetValue (params.mix);

    if (oversampling != nullptr)
        oversampling->reset();
    alignDelay.reset();
    dryDelay.reset();
    for (auto& b : colourDcBlock)
        b.reset();

    textureAmount = 0.0f;
    for (auto& t : texture)
        for (auto& b : t)
        {
            b.reset();
            b.c = Biquad::Coeffs::identity();
        }
}

void SilkCompressor::updateTexture (float reduction)
{
    const float amount = juce::jlimit (0.0f, 1.0f, params.cotton) * juce::jmin (1.0f, reduction / textureFullAtDb);
    if (std::abs (amount - textureAmount) < 0.002f && (amount > 0.0f || textureAmount <= 0.0f))
        return;

    textureAmount = amount < 0.001f ? 0.0f : amount;
    const auto warm = textureAmount > 0.0f ? Biquad::Coeffs::peak (sampleRate, 280.0, 0.8, warmthDb * textureAmount)
                                           : Biquad::Coeffs::identity();
    const auto softTop = textureAmount > 0.0f ? Biquad::Coeffs::highShelf (sampleRate, 9000.0, 0.6, softenDb * textureAmount)
                                              : Biquad::Coeffs::identity();
    for (auto& t : texture)
    {
        t[0].c = warm;
        t[1].c = softTop;
    }
}

void SilkCompressor::process (juce::AudioBuffer<float>& buffer)
{
    const int n = buffer.getNumSamples();
    const int chans = juce::jmin (numChannels, buffer.getNumChannels());
    if (chans == 0 || n == 0)
        return;

    const float attack = std::exp (-1.0f / (juce::jmax (0.1f, params.attackMs) * 0.001f * (float) sampleRate));
    const float fastRelease = std::exp (-1.0f / (juce::jmax (5.0f, params.releaseMs) * 0.001f * (float) sampleRate));
    const float cotton = juce::jlimit (0.0f, 1.0f, params.cotton);
    const float norm = 1.0f / (float) chans;

    makeup.setTargetValue (juce::Decibels::decibelsToGain (makeupDb (params)));
    mix.setTargetValue (juce::jlimit (0.0f, 1.0f, params.mix));

    // 1. Detector and the two-stage, memory-dependent release.
    for (int i = 0; i < n; ++i)
    {
        float x = 0.0f;
        for (int ch = 0; ch < chans; ++ch)
            x += buffer.getSample (ch, i);
        x = detectorPresence.process (detectorHighPass.process (x * norm));
        rmsState = x * x + (rmsState - x * x) * rmsCoeff;
        const float levelDb = 10.0f * std::log10 (rmsState + 1.0e-12f) + 3.01f;  // +3 dB: sine RMS → peak level

        const float target = -staticGainDb (levelDb, params.thresholdDb, params.ratio);
        const float reduction = 0.5f * (fastStage + slowStage);

        // The cell's memory: builds while it holds the voice down, fades slowly.
        const float held = juce::jmin (1.0f, reduction / 8.0f);
        memory = held + (memory - held) * (held > memory ? memoryRise : memoryFall);
        const float slowMs = 400.0f + 4600.0f * memory * std::sqrt (memory);
        const float slowRelease = std::exp (-1.0f / (slowMs * 0.001f * (float) sampleRate));

        fastStage = target + (fastStage - target) * (target > fastStage ? attack : fastRelease);
        slowStage = target + (slowStage - target) * (target > slowStage ? attack : slowRelease);

        const float gr = 0.5f * (fastStage + slowStage);
        gainRamp[(size_t) i] = juce::Decibels::decibelsToGain (-gr) * makeup.getNextValue();
        colourRamp[(size_t) i] = cotton * juce::jmin (1.0f, gr / colourFullAtDb);
    }

    const float reductionNow = 0.5f * (fastStage + slowStage);
    meterDb.store (reductionNow, std::memory_order_relaxed);

    // 2. Pure gain, kept for the latency-aligned path and the colour stage.
    for (int ch = 0; ch < chans; ++ch)
    {
        const auto* in = buffer.getReadPointer (ch);
        auto* c = compressed.getWritePointer (ch);
        auto* d = dry.getWritePointer (ch);
        for (int i = 0; i < n; ++i)
        {
            c[i] = in[i] * gainRamp[(size_t) i];
            dryDelay.pushSample (ch, in[i]);
            d[i] = dryDelay.popSample (ch);
        }
    }

    // 3. Cotton harmonics: (bridge(u) - u) at 2x, so with no colour it adds exactly nothing.
    const bool colouring = cotton > 0.0f;
    if (colouring)
    {
        juce::dsp::AudioBlock<float> in (compressed.getArrayOfWritePointers(), (size_t) chans, (size_t) n);
        auto up = oversampling->processSamplesUp (in);
        for (int ch = 0; ch < chans; ++ch)
        {
            auto* u = up.getChannelPointer ((size_t) ch);
            for (int j = 0; j < (int) up.getNumSamples(); ++j)
            {
                const float a = colourRamp[(size_t) (j / 2)] * colourScale;
                u[j] = a > 0.0f ? a * (bridge (u[j]) - u[j]) : 0.0f;
            }
        }
        juce::dsp::AudioBlock<float> out (colour.getArrayOfWritePointers(), (size_t) chans, (size_t) n);
        oversampling->processSamplesDown (out);
    }

    // 4. Assemble: aligned gain + colour → texture → parallel blend with the dry voice.
    for (int start = 0; start < n; start += textureInterval)
    {
        const int len = juce::jmin (textureInterval, n - start);
        updateTexture (0.5f * (fastStage + slowStage));

        for (int ch = 0; ch < chans; ++ch)
        {
            auto* outData = buffer.getWritePointer (ch);
            const auto* c = compressed.getReadPointer (ch);
            const auto* col = colour.getReadPointer (ch);
            auto& dc = colourDcBlock[(size_t) ch];
            auto& t = texture[(size_t) ch];

            for (int i = start; i < start + len; ++i)
            {
                alignDelay.pushSample (ch, c[i]);
                float wet = alignDelay.popSample (ch);
                if (colouring)
                    wet += dc.process (col[i]);
                outData[i] = t[1].process (t[0].process (wet));
            }
        }
    }

    for (int i = 0; i < n; ++i)
    {
        const float m = mix.getNextValue();
        for (int ch = 0; ch < chans; ++ch)
        {
            auto* outData = buffer.getWritePointer (ch);
            if (m < 1.0f)
                outData[i] = dry.getSample (ch, i) + (outData[i] - dry.getSample (ch, i)) * m;
        }
    }
}

} // namespace linen
