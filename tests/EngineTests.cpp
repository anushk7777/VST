#include "TestSignals.h"

namespace linen::test
{

class EngineTests final : public juce::UnitTest
{
public:
    EngineTests() : juce::UnitTest ("Engine", "Linen") {}

    void runTest() override
    {
        constexpr double sr = 48000.0;
        constexpr int block = 512;

        beginTest ("Neutral settings pass audio through untouched");
        {
            Engine engine;
            engine.prepare (sr, block, 2);
            const auto in = whiteNoise (2, 48000);
            const auto out = runInBlocks (engine, in, block);
            expectEquals (engine.getLatencySamples(), 0);
            expectLessOrEqual (maxAbsDifference (in, out), 1.0e-7f);
        }

        beginTest ("Mix at 50% with no latency is still a clean pass-through");
        {
            Engine engine;
            EngineParams p;
            p.mix = 0.5f;
            engine.setParams (p);
            engine.prepare (sr, block, 2);
            const auto in = whiteNoise (2, 24000);
            const auto out = runInBlocks (engine, in, block);
            expectLessOrEqual (maxAbsDifference (in, out), 1.0e-6f);
        }

        beginTest ("Dry path is delayed to match wet latency: 50% Mix sums an impulse with no comb filtering");
        {
            for (const int latency : { 1, 37, 512, 2048 })
            {
                Engine engine;
                EngineParams p;
                p.mix = 0.5f;
                engine.setParams (p);
                engine.prepare (sr, block, 2);
                engine.setExtraWetLatencyForTesting (latency);
                expectEquals (engine.getLatencySamples(), latency);

                const auto out = runInBlocks (engine, impulse (2, latency + 4096, 0), block);

                // Exactly one unit impulse, at the reported latency, and silence elsewhere.
                float stray = 0.0f;
                for (int i = 0; i < out.getNumSamples(); ++i)
                    if (i != latency)
                        stray = juce::jmax (stray, std::abs (out.getSample (0, i)), std::abs (out.getSample (1, i)));

                expectWithinAbsoluteError (out.getSample (0, latency), 1.0f, 1.0e-6f, "latency " + juce::String (latency));
                expectWithinAbsoluteError (out.getSample (1, latency), 1.0f, 1.0e-6f);
                expectLessOrEqual (stray, 1.0e-7f);
            }
        }

        beginTest ("Gain changes are smoothed (no zipper steps)");
        {
            Engine engine;
            engine.prepare (sr, block, 1);

            juce::AudioBuffer<float> dc (1, 4800);
            for (int i = 0; i < dc.getNumSamples(); ++i)
                dc.setSample (0, i, 0.5f);

            EngineParams p;
            p.outputGainDb = -12.0f;
            engine.setParams (p);
            engine.process (dc);

            float biggestStep = 0.0f;
            for (int i = 1; i < dc.getNumSamples(); ++i)
                biggestStep = juce::jmax (biggestStep, std::abs (dc.getSample (0, i) - dc.getSample (0, i - 1)));

            expectLessOrEqual (biggestStep, 0.001f, "a -12 dB jump should ramp, not step");
            expectWithinAbsoluteError (dc.getSample (0, dc.getNumSamples() - 1), 0.5f * juce::Decibels::decibelsToGain (-12.0f), 1.0e-4f);
        }

        beginTest ("Input and output gain are applied in dB");
        {
            Engine engine;
            EngineParams p;
            p.inputGainDb = 6.0f;
            p.outputGainDb = -3.0f;
            engine.setParams (p);
            engine.prepare (sr, block, 1);

            juce::AudioBuffer<float> dc (1, 256);
            for (int i = 0; i < 256; ++i)
                dc.setSample (0, i, 0.1f);
            engine.process (dc);

            expectWithinAbsoluteError (dc.getSample (0, 100), 0.1f * juce::Decibels::decibelsToGain (3.0f), 1.0e-5f);
        }
    }
};

static EngineTests engineTests;

} // namespace linen::test
