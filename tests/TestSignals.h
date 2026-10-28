#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "dsp/LinenEngine.h"

namespace linen::test
{

inline juce::AudioBuffer<float> whiteNoise (int channels, int samples, float level = 0.25f, juce::int64 seed = 1)
{
    juce::AudioBuffer<float> buffer (channels, samples);
    juce::Random rng (seed);
    for (int ch = 0; ch < channels; ++ch)
        for (int i = 0; i < samples; ++i)
            buffer.setSample (ch, i, (rng.nextFloat() * 2.0f - 1.0f) * level);
    return buffer;
}

inline juce::AudioBuffer<float> impulse (int channels, int samples, int position = 0)
{
    juce::AudioBuffer<float> buffer (channels, samples);
    buffer.clear();
    for (int ch = 0; ch < channels; ++ch)
        buffer.setSample (ch, position, 1.0f);
    return buffer;
}

/** Runs a whole buffer through the engine in irregular block sizes, the way a
    host might, and returns the result. */
inline juce::AudioBuffer<float> runInBlocks (Engine& engine, const juce::AudioBuffer<float>& input, int maxBlock)
{
    juce::AudioBuffer<float> output (input);
    const int sizes[] { maxBlock, 1, maxBlock / 2, 17, maxBlock - 3 };
    int pos = 0, which = 0;

    while (pos < output.getNumSamples())
    {
        const int n = juce::jmin (juce::jmax (1, sizes[which++ % 5]), output.getNumSamples() - pos);
        juce::AudioBuffer<float> block (output.getArrayOfWritePointers(), output.getNumChannels(), pos, n);
        engine.process (block);
        pos += n;
    }

    return output;
}

inline float maxAbsDifference (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int bOffset = 0)
{
    float worst = 0.0f;
    for (int ch = 0; ch < a.getNumChannels(); ++ch)
        for (int i = 0; i + bOffset < b.getNumSamples() && i < a.getNumSamples(); ++i)
            worst = juce::jmax (worst, std::abs (a.getSample (ch, i) - b.getSample (ch, i + bOffset)));
    return worst;
}

} // namespace linen::test
