#include "TestSignals.h"

#include "dsp/Air.h"
#include "dsp/SilkCompressor.h"

namespace linen::test
{

namespace
{
    constexpr double sr = 48000.0;
    constexpr int block = 512;

    void addSine (juce::AudioBuffer<float>& b, double hz, float levelDb, int start = 0, int end = -1)
    {
        const double amp = juce::Decibels::decibelsToGain ((double) levelDb);
        end = end < 0 ? b.getNumSamples() : end;
        for (int i = start; i < end; ++i)
            b.addSample (0, i, (float) (amp * std::sin (juce::MathConstants<double>::twoPi * hz * i / sr)));
    }

    juce::AudioBuffer<float> silence (int samples)
    {
        juce::AudioBuffer<float> b (1, samples);
        b.clear();
        return b;
    }

    template <typename Processor>
    void runBlocks (Processor& p, juce::AudioBuffer<float>& audio)
    {
        for (int pos = 0; pos < audio.getNumSamples(); pos += block)
        {
            juce::AudioBuffer<float> b (audio.getArrayOfWritePointers(), audio.getNumChannels(), pos,
                                        juce::jmin (block, audio.getNumSamples() - pos));
            p.process (b);
        }
    }

    float rmsDb (const juce::AudioBuffer<float>& b, int start, int length)
    {
        return juce::Decibels::gainToDecibels (b.getRMSLevel (0, start, length), -200.0f);
    }

    /** Level (dB) of the part of a signal inside a band, via a 4th-order band-pass. */
    float bandDb (const juce::AudioBuffer<float>& b, double lowHz, double highHz, int start, int length)
    {
        Biquad hp1, hp2, lp1, lp2;
        hp1.c = Biquad::Coeffs::highPass (sr, lowHz, 0.54);
        hp2.c = Biquad::Coeffs::highPass (sr, lowHz, 1.31);
        lp1.c = Biquad::Coeffs::lowPass (sr, highHz, 0.54);
        lp2.c = Biquad::Coeffs::lowPass (sr, highHz, 1.31);
        double sum = 0.0;
        for (int i = 0; i < start + length; ++i)
        {
            const float y = lp2.process (lp1.process (hp2.process (hp1.process (b.getSample (0, i)))));
            if (i >= start)
                sum += (double) y * y;
        }
        return (float) (10.0 * std::log10 (sum / length + 1.0e-30));
    }

    /** A sung vowel with real top end: harmonics of 220 Hz out to 16 kHz. */
    juce::AudioBuffer<float> brightVowel (int samples, float levelDb = -14.0f)
    {
        auto b = silence (samples);
        for (int n = 1; 220.0 * n < 16000.0; ++n)
            addSine (b, 220.0 * n, levelDb - 6.0f * std::log2 ((float) n), 0, samples);
        return b;
    }
}

class CompressorTests final : public juce::UnitTest
{
public:
    CompressorTests() : juce::UnitTest ("Silk compressor", "Linen") {}

    void runTest() override
    {
        beginTest ("Static curve follows threshold, ratio and the 10 dB soft knee");
        {
            for (const float level : { -40.0f, -26.0f, -20.0f, -15.0f, -6.0f })
            {
                SilkCompressor comp;
                CompParams p;
                p.thresholdDb = -20.0f;
                p.ratio = 3.0f;
                p.autoMakeup = false;
                comp.setParams (p);
                comp.prepare (sr, block, 1);

                auto tone = silence (48000);
                addSine (tone, 1000.0, level);
                runBlocks (comp, tone);

                const float measured = rmsDb (tone, 36000, 9600) - (level - 3.01f);
                const float expected = SilkCompressor::staticGainDb (level, -20.0f, 3.0f);
                expectWithinAbsoluteError (measured, expected, 0.5f, "input " + juce::String (level) + " dB");
            }
        }

        beginTest ("Opto-style release: slower after long compression than after a short peak");
        {
            const auto recovery = [this] (int burstSamples)
            {
                SilkCompressor comp;
                CompParams p;
                p.thresholdDb = -30.0f;
                p.ratio = 4.0f;
                p.releaseMs = 80.0f;
                p.autoMakeup = false;
                comp.setParams (p);
                comp.prepare (sr, block, 1);

                // Loud burst, then a quiet tone: time until the quiet tone is back within 1 dB.
                const int total = burstSamples + 48000 * 3;
                auto audio = silence (total);
                addSine (audio, 1000.0, -6.0f, 0, burstSamples);
                addSine (audio, 1000.0, -50.0f, burstSamples, total);
                runBlocks (comp, audio);

                const float settled = rmsDb (audio, total - 4800, 4800);
                for (int pos = burstSamples; pos + 480 <= total; pos += 480)
                    if (rmsDb (audio, pos, 480) > settled - 1.0f)
                        return (double) (pos - burstSamples) / sr;

                expect (false, "never recovered");
                return 10.0;
            };

            const double afterPeak = recovery (4800);       // 0.1 s
            const double afterHold = recovery (48000 * 3);  // 3 s
            logMessage ("recovery after a peak " + juce::String (afterPeak, 3) + " s, after a long hold " + juce::String (afterHold, 3) + " s");
            expectLessThan (afterPeak, 0.6);
            expectGreaterThan (afterHold, afterPeak * 1.8);
        }

        beginTest ("Side-chain high-pass: low boom pumps it far less than the voice does");
        {
            const auto reduction = [] (double hz)
            {
                SilkCompressor comp;
                CompParams p;
                p.thresholdDb = -24.0f;
                p.ratio = 3.0f;
                p.autoMakeup = false;
                comp.setParams (p);
                comp.prepare (sr, block, 1);
                auto tone = silence (48000);
                addSine (tone, hz, -10.0f);
                runBlocks (comp, tone);
                return -(rmsDb (tone, 36000, 9600) - (-10.0f - 3.01f));
            };

            const float lowGr = reduction (45.0), voiceGr = reduction (1000.0);
            logMessage ("GR at 45 Hz " + juce::String (lowGr, 1) + " dB, at 1 kHz " + juce::String (voiceGr, 1) + " dB");
            expectLessThan (lowGr, voiceGr - 4.0f);
        }

        beginTest ("Ratio 1 is a clean bypass");
        {
            SilkCompressor comp;
            comp.prepare (sr, block, 2);
            auto in = whiteNoise (2, 24000, 0.9f);
            auto out = in;
            runBlocks (comp, out);
            expectLessOrEqual (maxAbsDifference (in, out), 1.0e-6f);
        }
    }
};

class AirTests final : public juce::UnitTest
{
public:
    AirTests() : juce::UnitTest ("Air", "Linen") {}

    void runTest() override
    {
        beginTest ("Off is bit-exact");
        {
            Air air;
            air.prepare (sr, block, 2);
            auto in = whiteNoise (2, 24000);
            auto out = in;
            runBlocks (air, out);
            expectEquals (maxAbsDifference (in, out), 0.0f);
        }

        beginTest ("Air opens the top of a sung vowel and leaves the body alone");
        {
            Air air;
            AirParams p;
            p.amount = 1.0f;
            air.setParams (p);
            air.prepare (sr, block, 1);

            const auto in = brightVowel (48000);
            auto out = in;
            runBlocks (air, out);

            const float top = bandDb (out, 11000, 16000, 24000, 24000) - bandDb (in, 11000, 16000, 24000, 24000);
            const float body = bandDb (out, 200, 1500, 24000, 24000) - bandDb (in, 200, 1500, 24000, 24000);
            logMessage ("air: top " + juce::String (top, 2) + " dB, body " + juce::String (body, 2) + " dB");
            expectGreaterThan (top, 3.0f);
            expectWithinAbsoluteError (body, 0.0f, 0.3f);
        }

        beginTest ("On an S the air closes and turns into a cut; on the vowel it opens");
        {
            Air air;
            AirParams p;
            p.amount = 0.6f;
            p.deEss = 1.0f;
            air.setParams (p);
            air.prepare (sr, block, 1);

            // 0.6 s vowel, then 0.4 s of 'S' (loud 5-12 kHz noise over a faint vowel).
            const int vowelEnd = 28800, total = 48000;
            auto in = brightVowel (total);
            in.applyGain (vowelEnd, total - vowelEnd, 0.1f);
            {
                auto hiss = whiteNoise (1, total, 0.5f, 11);
                Biquad hp, lp;
                hp.c = Biquad::Coeffs::highPass (sr, 5000.0, 0.707);
                lp.c = Biquad::Coeffs::lowPass (sr, 12000.0, 0.707);
                for (int i = 0; i < total; ++i)
                {
                    const float h = lp.process (hp.process (hiss.getSample (0, i)));
                    if (i >= vowelEnd)
                        in.addSample (0, i, h);
                }
            }

            auto out = in;
            runBlocks (air, out);

            const float vowelTop = bandDb (out, 11000, 16000, 9600, 14400) - bandDb (in, 11000, 16000, 9600, 14400);
            const float sTop = bandDb (out, 7000, 12000, vowelEnd + 2400, 14400) - bandDb (in, 7000, 12000, vowelEnd + 2400, 14400);
            logMessage ("vowel top " + juce::String (vowelTop, 2) + " dB, S band " + juce::String (sTop, 2) + " dB");
            expectGreaterThan (vowelTop, 2.0f, "air opens on the vowel");
            expectLessThan (sTop, -1.5f, "S is softened, not brightened");
        }

        beginTest ("The exciter's sparkle does not alias");
        {
            Air air;
            AirParams p;
            p.amount = 1.0f;
            air.setParams (p);
            air.prepare (sr, block, 1);

            // A loud low vowel tone (so nothing reads as an S) plus a 9.5 kHz partial.
            auto in = silence (65536);
            addSine (in, 220.0, -10.0f);
            addSine (in, 9500.0, -24.0f);
            auto out = in;
            runBlocks (air, out);

            constexpr int order = 15, n = 1 << order;
            juce::dsp::FFT fft (order);
            std::vector<float> buf ((size_t) n * 2, 0.0f);
            for (int i = 0; i < n; ++i)
            {
                const double x = juce::MathConstants<double>::twoPi * i / (n - 1);
                const double w = 0.35875 - 0.48829 * std::cos (x) + 0.14128 * std::cos (2 * x) - 0.01168 * std::cos (3 * x);
                buf[(size_t) i] = (float) (out.getSample (0, 16384 + i) * w);
            }
            fft.performFrequencyOnlyForwardTransform (buf.data(), true);

            const double binHz = sr / n;
            double partial = 0.0, worst = 0.0;
            for (int k = 1; k < n / 2; ++k)
            {
                const double hz = k * binHz;
                const bool expected = std::abs (hz - 220.0) < 10 || std::abs (hz - 9500.0) < 10 || std::abs (hz - 19000.0) < 10;
                if (std::abs (hz - 9500.0) < 10)
                    partial = std::max (partial, (double) buf[(size_t) k]);
                else if (! expected && hz > 30.0)
                    worst = std::max (worst, (double) buf[(size_t) k]);
            }
            const double spur = 20.0 * std::log10 (worst / partial);
            logMessage ("worst spur re the 9.5 kHz partial: " + juce::String (spur, 1) + " dB");
            expectLessThan (spur, -80.0);
        }
    }
};

static CompressorTests compressorTests;
static AirTests airTests;

} // namespace linen::test
