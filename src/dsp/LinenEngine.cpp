#include "LinenEngine.h"

namespace linen
{

namespace
{
    constexpr double smoothingSeconds = 0.025;

    int maxDelayFor (int latency) { return juce::jmax (1, latency + 1); }
}

void Engine::prepare (double newSampleRate, int maxBlockSize, int numChannels)
{
    sampleRate = newSampleRate;
    maxBlock = maxBlockSize;
    numChans = numChannels;

    const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlock, (juce::uint32) numChans };

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
    dryDelay.reset();
    testWetDelay.reset();

    inputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (params.inputGainDb));
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (params.outputGainDb));
    mix.setCurrentAndTargetValue (params.mix);
}

void Engine::setParams (const EngineParams& newParams)
{
    params = newParams;
    inputGain.setTargetValue (juce::Decibels::decibelsToGain (params.inputGainDb));
    outputGain.setTargetValue (juce::Decibels::decibelsToGain (params.outputGainDb));
    mix.setTargetValue (juce::jlimit (0.0f, 1.0f, params.mix));
}

void Engine::setExtraWetLatencyForTesting (int samples)
{
    testWetLatency = juce::jmax (0, samples);
    updateLatency();
}

void Engine::updateLatency()
{
    latencySamples = testWetLatency;

    dryDelay.setMaximumDelayInSamples (maxDelayFor (latencySamples));
    dryDelay.setDelay ((float) latencySamples);

    testWetDelay.setMaximumDelayInSamples (maxDelayFor (testWetLatency));
    testWetDelay.setDelay ((float) testWetLatency);
}

void Engine::process (juce::AudioBuffer<float>& buffer)
{
    // Some hosts occasionally send more than the announced block size,
    // so work through anything larger in prepared-size chunks.
    for (int pos = 0; pos < buffer.getNumSamples(); pos += maxBlock)
    {
        const int n = juce::jmin (maxBlock, buffer.getNumSamples() - pos);
        juce::AudioBuffer<float> chunk (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), pos, n);
        processChunk (chunk);
    }
}

void Engine::processChunk (juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();
    const int channels = juce::jmin (buffer.getNumChannels(), numChans);

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

    // Wet path.
    for (int i = 0; i < numSamples; ++i)
    {
        const float g = inputGain.getNextValue();
        for (int ch = 0; ch < channels; ++ch)
            buffer.getWritePointer (ch)[i] *= g;
    }

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
