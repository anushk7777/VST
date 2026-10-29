#include "TestSignals.h"

namespace linen::test
{

namespace
{
    /** Neutral chain: every module at a setting that leaves the sound alone
        (the defaults). */
    EngineParams neutral()
    {
        return {};
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
            expect (identicalAfter (in, out, engine.getLatencySamples()), "bit-exact");
        }

        beginTest ("Any Mix on a neutral chain is still an exact pass-through");
        {
            for (const float mixAmount : { 0.0f, 0.37f, 0.5f })
            {
                Engine engine;
                auto p = neutral();
                p.mix = mixAmount;
                engine.setParams (p);
                engine.prepare (sr, block, 2);
                const auto in = whiteNoise (2, 24000);
                const auto out = runInBlocks (engine, in, block);
                expect (identicalAfter (in, out, engine.getLatencySamples()), "mix " + juce::String (mixAmount));
            }
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

        beginTest ("Mix stays comb-free with the preamp engaged (its oversampling is linear-phase)");
        {
            // At low level the preamp is linear, so 50% Mix must sum flat: any
            // phase mismatch between dry and wet would show up as notches.
            Engine engine;
            auto p = neutral();
            p.preampDrive = 0.5f;
            p.mix = 0.5f;
            engine.setParams (p);
            engine.prepare (sr, block, 1);

            const auto in = whiteNoise (1, 48000 * 4, 0.003f);
            const auto out = runInBlocks (engine, in, block);
            const int latency = engine.getLatencySamples();

            juce::dsp::FFT fft (12);
            std::vector<double> pin (2049, 0.0), pout (2049, 0.0);
            std::vector<float> buf (8192);
            for (int pos = 48000; pos + 4096 + latency < in.getNumSamples(); pos += 2048)
            {
                for (auto [src, offset, acc] : { std::tuple { &in, 0, &pin }, std::tuple { &out, latency, &pout } })
                {
                    std::fill (buf.begin(), buf.end(), 0.0f);
                    for (int j = 0; j < 4096; ++j)
                        buf[(size_t) j] = src->getSample (0, pos + offset + j)
                                          * 0.5f * (1.0f - std::cos (juce::MathConstants<float>::twoPi * (float) j / 4096.0f));
                    fft.performFrequencyOnlyForwardTransform (buf.data(), true);
                    for (size_t k = 0; k < acc->size(); ++k)
                        (*acc)[k] += (double) buf[k] * buf[k];
                }
            }

            double worst = 0.0, worstHz = 0.0;
            for (int k = 0; k < 2049; ++k)
            {
                const double hz = k * sr / 4096.0;
                const double dev = std::abs (10.0 * std::log10 (pout[(size_t) k] / pin[(size_t) k]));
                if (hz > 60.0 && hz < 18000.0 && dev > worst)
                {
                    worst = dev;
                    worstHz = hz;
                }
            }
            logMessage ("worst deviation 60 Hz-18 kHz: " + juce::String (worst, 2) + " dB at " + juce::String (worstHz, 0) + " Hz");
            expectLessThan (worst, 0.5);
        }

        beginTest ("Blocks larger than announced are handled");
        {
            Engine engine;
            engine.setParams (neutral());
            engine.prepare (sr, 64, 2);
            auto in = whiteNoise (2, 10000);
            auto out = in;
            engine.process (out);
            expect (identicalAfter (in, out, engine.getLatencySamples()), "bit-exact");
        }
    }
};

static EngineTests engineTests;

} // namespace linen::test
