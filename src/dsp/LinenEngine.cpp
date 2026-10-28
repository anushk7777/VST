#include "LinenEngine.h"
#include "SilkMacro.h"

namespace linen
{

namespace
{
    constexpr double smoothingSeconds = 0.025;

    int maxDelayFor (int latency) { return juce::jmax (1, latency + 1); }

    /** Pain reaches into its neighbours: more preamp drive, a De-harsh that
        clamps the fizz above 5.5 kHz but leaves the throat band room to ache,
        and (past 50%) a darker, dustier Air. */
    EngineParams withPainCoupling (EngineParams p)
    {
        const float pain = juce::jlimit (0.0f, 1.0f, p.pain);
        const float t = juce::jlimit (0.0f, 1.0f, (pain - 0.5f) / 0.5f);

        p.preampDrive = juce::jlimit (0.0f, 1.0f, p.preampDrive + 0.25f * pain);
        p.deharsh.painTilt = pain;
        p.air.scorch = t * t * (3.0f - 2.0f * t);
        return p;
    }
}

void Engine::prepare (double newSampleRate, int maxBlockSize, int numChannels)
{
    sampleRate = newSampleRate;
    maxBlock = maxBlockSize;
    numChans = numChannels;

    const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlock, (juce::uint32) numChans };

    preamp.prepare (sampleRate, maxBlock, numChans);
    eq.prepare (sampleRate, numChans);
    painEngine.prepare (sampleRate, maxBlock, numChans);
    deharsh.prepare (sampleRate, numChans);
    comp.prepare (sampleRate, maxBlock, numChans);
    air.prepare (sampleRate, maxBlock, numChans);
    detector.assign ((size_t) maxBlock, 0.0f);

    dryDelay.prepare (spec);
    testWetDelay.prepare (spec);
    dryBuffer.setSize (numChans, maxBlock, false, false, true);

    inputGain.reset (sampleRate, smoothingSeconds);
    outputGain.reset (sampleRate, smoothingSeconds);
    mix.reset (sampleRate, smoothingSeconds);

    updateLatency();
    reset();
}

void Engine::reset()
{
    preamp.reset();
    eq.reset();
    painEngine.reset();
    deharsh.reset();
    comp.reset();
    air.reset();
    dryDelay.reset();
    testWetDelay.reset();

    inputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (params.inputGainDb));
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (params.outputGainDb));
    mix.setCurrentAndTargetValue (params.deharsh.delta ? 1.0f : params.mix);
}

void Engine::setParams (const EngineParams& newParams)
{
    params = withPainCoupling (SilkMacro::apply (newParams, newParams.silk));
    painEngine.setPain (params.pain);
    preamp.setDrive (params.preampDrive);
    eq.setParams (params.eq);
    deharsh.setParams (params.deharsh);
    comp.setParams (params.comp);
    air.setParams (params.air);

    inputGain.setTargetValue (juce::Decibels::decibelsToGain (params.inputGainDb));
    outputGain.setTargetValue (juce::Decibels::decibelsToGain (params.outputGainDb));

    // Delta is for listening to what De-harsh removes, so never blend dry into it.
    mix.setTargetValue (params.deharsh.delta ? 1.0f : juce::jlimit (0.0f, 1.0f, params.mix));
}

void Engine::setExtraWetLatencyForTesting (int samples)
{
    testWetLatency = juce::jmax (0, samples);
    updateLatency();
}

void Engine::updateLatency()
{
    latencySamples = preamp.getLatencySamples() + painEngine.getLatencySamples() + deharsh.getLatencySamples()
                   + comp.getLatencySamples() + testWetLatency;

    dryDelay.setMaximumDelayInSamples (maxDelayFor (latencySamples));
    dryDelay.setDelay ((float) latencySamples);

    testWetDelay.setMaximumDelayInSamples (maxDelayFor (testWetLatency));
    testWetDelay.setDelay ((float) testWetLatency);
}

void Engine::process (juce::AudioBuffer<float>& buffer, const juce::AudioBuffer<float>* sidechain)
{
    // Some hosts occasionally send more than the announced block size,
    // so work through anything larger in prepared-size chunks.
    for (int pos = 0; pos < buffer.getNumSamples(); pos += maxBlock)
    {
        const int n = juce::jmin (maxBlock, buffer.getNumSamples() - pos);
        juce::AudioBuffer<float> chunk (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), pos, n);
        processChunk (chunk, sidechain, pos);
    }
}

void Engine::processChunk (juce::AudioBuffer<float>& buffer, const juce::AudioBuffer<float>* sidechain, int offset)
{
    const int numSamples = buffer.getNumSamples();
    const int channels = juce::jmin (buffer.getNumChannels(), numChans);

    if (channels > 0)
        inputLevel.store (buffer.getRMSLevel (0, 0, numSamples), std::memory_order_relaxed);

    // Dry path: untouched input, delayed to line up with the wet path.
    for (int ch = 0; ch < channels; ++ch)
    {
        const auto* in = buffer.getReadPointer (ch);
        auto* dry = dryBuffer.getWritePointer (ch);

        for (int i = 0; i < numSamples; ++i)
        {
            dryDelay.pushSample (ch, in[i]);
            dry[i] = dryDelay.popSample (ch);
        }
    }

    // Input gain.
    for (int i = 0; i < numSamples; ++i)
    {
        const float g = inputGain.getNextValue();
        for (int ch = 0; ch < channels; ++ch)
            buffer.getWritePointer (ch)[i] *= g;
    }

    juce::AudioBuffer<float> wet (buffer.getArrayOfWritePointers(), channels, numSamples);

    // Colour first: preamp, then the console EQ.
    preamp.process (wet);
    eq.process (wet);
    painEngine.process (wet);

    // De-harsh, detecting from the vocal itself or from the sidechain.
    {
        const bool useSidechain = params.deharshSidechain && sidechain != nullptr && sidechain->getNumChannels() > 0;
        const auto& source = useSidechain ? *sidechain : buffer;
        const int sourceOffset = useSidechain ? offset : 0;
        const int sourceChannels = useSidechain ? sidechain->getNumChannels() : channels;
        const float norm = 1.0f / (float) juce::jmax (1, sourceChannels);

        std::fill (detector.begin(), detector.begin() + numSamples, 0.0f);
        for (int ch = 0; ch < sourceChannels; ++ch)
        {
            const auto* src = source.getReadPointer (ch, sourceOffset);
            for (int i = 0; i < numSamples; ++i)
                detector[(size_t) i] += src[i] * norm;
        }

        deharsh.process (wet, detector.data(), ! useSidechain);
    }

    // Glue, then air on top of the levelled voice.
    comp.process (wet);
    air.process (wet);

    if (testWetLatency > 0)
    {
        for (int ch = 0; ch < channels; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            for (int i = 0; i < numSamples; ++i)
            {
                testWetDelay.pushSample (ch, data[i]);
                data[i] = testWetDelay.popSample (ch);
            }
        }
    }

    // Output gain, then a linear (phase-coherent) dry/wet blend.
    for (int i = 0; i < numSamples; ++i)
    {
        const float g = outputGain.getNextValue();
        const float m = mix.getNextValue();

        for (int ch = 0; ch < channels; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            data[i] = data[i] * g * m + dryBuffer.getSample (ch, i) * (1.0f - m);
        }
    }
}

} // namespace linen
