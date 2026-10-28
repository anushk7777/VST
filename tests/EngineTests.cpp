#include "TestSignals.h"

namespace linen::test
{

namespace
{
    /** Neutral chain: every module at a setting that leaves the sound alone. */
    EngineParams neutral()
    {
        EngineParams p;
        p.deharsh.depth = 0.0f;
        return p;
    }

    juce::AudioBuffer<float> constant (int samples, float value)
    {
        juce::AudioBuffer<float> buffer (1, samples);
        for (int i = 0; i < samples; ++i)
            buffer.setSample (0, i, value);
        return buffer;
    }
}

class EngineTests final : public juce::UnitTest
{
public:
    EngineTests() : juce::UnitTest ("Engine", "Linen") {}

    void runTest() override
    {
        constexpr double sr = 48000.0;
        constexpr int block = 512;

        beginTest ("Neutral settings pass audio through untouched, delayed by the reported latency");
        {
            Engine engine;
            engine.setParams (neutral());
            engine.prepare (sr, block, 2);
            const auto in = whiteNoise (2, 48000);
            const auto out = runInBlocks (engine, in, block);
            expectLessOrEqual (maxAbsDifference (in, out, engine.getLatencySamples()), 2.0e-5f);
        }

        beginTest ("Mix at 50% on a neutral chain is still a clean pass-through");
        {
            Engine engine;
            auto p = neutral();
            p.mix = 0.5f;
            engine.setParams (p);
            engine.prepare (sr, block, 2);
            const auto in = whiteNoise (2, 24000);
            const auto out = runInBlocks (engine, in, block);
            expectLessOrEqual (maxAbsDifference (in, out, engine.getLatencySamples()), 2.0e-5f);
        }

        beginTest ("Dry path follows any added wet latency: 50% Mix sums an impulse with no comb filtering");
        {
            for (const int extra : { 1, 37, 512, 2048 })
            {
                Engine engine;
                auto p = neutral();
                p.mix = 0.5f;
                engine.setParams (p);
                engine.prepare (sr, block, 2);
                const int base = engine.getLatencySamples();
                engine.setExtraWetLatencyForTesting (extra);
                const int latency = engine.getLatencySamples();
                expectEquals (latency, base + extra);

                const auto out = runInBlocks (engine, impulse (2, latency + 4096, 0), block);

                // Exactly one unit impulse, at the reported latency, and (near) silence elsewhere.
                float stray = 0.0f;
                for (int i = 0; i < out.getNumSamples(); ++i)
                    if (i != latency)
                        stray = juce::jmax (stray, std::abs (out.getSample (0, i)), std::abs (out.getSample (1, i)));

                expectWithinAbsoluteError (out.getSample (0, latency), 1.0f, 1.0e-4f, "extra latency " + juce::String (extra));
                expectWithinAbsoluteError (out.getSample (1, latency), 1.0f, 1.0e-4f);
                expectLessOrEqual (stray, 1.0e-4f);
            }
        }

        beginTest ("Gain changes are smoothed (no zipper steps)");
        {
            Engine engine;
            engine.setParams (neutral());
            engine.prepare (sr, block, 1);

            auto first = constant (9600, 0.5f);
            engine.process (first);

            auto p = neutral();
            p.outputGainDb = -12.0f;
            engine.setParams (p);
            auto second = constant (9600, 0.5f);
            engine.process (second);

            float biggestStep = 0.0f;
            for (int i = 1; i < second.getNumSamples(); ++i)
                biggestStep = juce::jmax (biggestStep, std::abs (second.getSample (0, i) - second.getSample (0, i - 1)));

            expectLessOrEqual (biggestStep, 0.001f, "a -12 dB jump should ramp, not step");
            expectWithinAbsoluteError (second.getSample (0, second.getNumSamples() - 1),
                                       0.5f * juce::Decibels::decibelsToGain (-12.0f), 1.0e-4f);
        }

        beginTest ("Input and output gain are applied in dB");
        {
            Engine engine;
            auto p = neutral();
            p.inputGainDb = 6.0f;
            p.outputGainDb = -3.0f;
            engine.setParams (p);
            engine.prepare (sr, block, 1);

            auto dc = constant (8192, 0.1f);
            engine.process (dc);

            expectWithinAbsoluteError (dc.getSample (0, 6000), 0.1f * juce::Decibels::decibelsToGain (3.0f), 1.0e-4f);
        }

        beginTest ("Blocks larger than announced are handled");
        {
            Engine engine;
            engine.setParams (neutral());
            engine.prepare (sr, 64, 2);
            auto in = whiteNoise (2, 10000);
            auto out = in;
            engine.process (out);
            expectLessOrEqual (maxAbsDifference (in, out, engine.getLatencySamples()), 2.0e-5f);
        }
    }
};

static EngineTests engineTests;

} // namespace linen::test
