#pragma once

#include <juce_dsp/juce_dsp.h>

namespace linen
{

/** Switches one section of the chain in and out without a click.

    The section's input is kept in a delay line as long as the section's
    latency, and faded against the section's output over 20 ms. Fully in,
    the section's output is left exactly as it is; fully out, the output is
    exactly the input, delayed by the latency, so the chain stays aligned.
    The section keeps running while out, so it comes back in warm. */
class SectionBypass
{
public:
    void prepare (double sampleRate, int maxBlockSize, int numChannels, int latencySamples)
    {
        latency = juce::jmax (0, latencySamples);
        dry.setSize (juce::jmax (1, numChannels), maxBlockSize, false, false, true);

        const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlockSize, (juce::uint32) juce::jmax (1, numChannels) };
        delay.prepare (spec);
        delay.setMaximumDelayInSamples (latency + 1);
        delay.setDelay ((float) latency);

        fade.reset (sampleRate, 0.02);
        reset();
    }

    void reset()
    {
        delay.reset();
        fade.setCurrentAndTargetValue (fade.getTargetValue());
    }

    void setOn (bool shouldBeOn) { fade.setTargetValue (shouldBeOn ? 1.0f : 0.0f); }

    bool isFullyOn() const noexcept { return ! fade.isSmoothing() && fade.getTargetValue() >= 1.0f; }

    /** Call with the section's input, before the section runs. */
    void capture (const juce::AudioBuffer<float>& input)
    {
        const int n = input.getNumSamples();
        const int chans = juce::jmin (input.getNumChannels(), dry.getNumChannels());

        for (int ch = 0; ch < chans; ++ch)
        {
            const auto* in = input.getReadPointer (ch);
            auto* d = dry.getWritePointer (ch);

            if (latency == 0)
            {
                std::copy (in, in + n, d);
                continue;
            }

            for (int i = 0; i < n; ++i)
            {
                delay.pushSample (ch, in[i]);
                d[i] = delay.popSample (ch);
            }
        }
    }

    /** Call with the section's output: fades it toward the captured input. */
    void apply (juce::AudioBuffer<float>& output)
    {
        if (isFullyOn())
            return;

        const int n = output.getNumSamples();
        const int chans = juce::jmin (output.getNumChannels(), dry.getNumChannels());

        for (int i = 0; i < n; ++i)
        {
            const float f = fade.getNextValue();
            for (int ch = 0; ch < chans; ++ch)
            {
                auto* out = output.getWritePointer (ch);
                const float d = dry.getSample (ch, i);
                out[i] = f <= 0.0f ? d : d + (out[i] - d) * f;
            }
        }
    }

private:
    int latency = 0;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> delay;
    juce::AudioBuffer<float> dry;
    juce::SmoothedValue<float> fade { 1.0f };
};

} // namespace linen
