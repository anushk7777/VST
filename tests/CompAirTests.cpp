#include "TestSignals.h"

#include "dsp/Air.h"

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

        beginTest ("The sparkle stays above 9 kHz: no intermodulation grit down in the voice");
        {
            Air air;
            AirParams p;
            p.amount = 1.0f;
            air.setParams (p);
            air.prepare (sr, block, 1);

            // A vowel with two upper partials whose difference (800 Hz) and
            // sum products land in and above the voice.
            auto in = silence (65536);
            addSine (in, 220.0, -10.0f);
            addSine (in, 6300.0, -26.0f);
            addSine (in, 7100.0, -26.0f);
            auto out = in;
            runBlocks (air, out);

            const double partial = toneDb (out, 6300.0);
            const double difference = toneDb (out, 800.0) - partial;
            const double sparkle = toneDb (out, 13400.0) - partial;
            logMessage ("800 Hz difference tone " + juce::String (difference, 1) + " dB, 13.4 kHz sparkle "
                        + juce::String (sparkle, 1) + " dB, re the 6.3 kHz partial");
            expectLessThan (difference, -80.0, "no grit in the voice band");
            expectLessThan (sparkle, -20.0, "sparkle is gentle");
            expectGreaterThan (sparkle, -60.0, "...but there");
        }
    }
};

class LoomTests final : public juce::UnitTest
{
public:
    LoomTests() : juce::UnitTest ("Silk loom", "Linen") {}

    void runTest() override
    {
        // A bright vowel with a harsh 3.4 kHz resonance for De-harsh to cut.
        auto vowel = brightVowel (96000, -16.0f);
        addSine (vowel, 3400.0, -14.0f);
        juce::AudioBuffer<float> stereo (2, vowel.getNumSamples());
        for (int ch = 0; ch < 2; ++ch)
            stereo.copyFrom (ch, 0, vowel, 0, 0, vowel.getNumSamples());

        const auto render = [&stereo] (float depth, float loom)
        {
            EngineParams p;
            p.comp.thresholdDb = -18.0f;
            p.deharsh.depth = depth;
            p.air.amount = 0.6f;
            p.air.loom = loom;
            Engine engine;
            engine.setParams (p);
            engine.prepare (sr, block, 2);
            return runInBlocks (engine, stereo, block);
        };

        beginTest ("Where De-harsh cuts, the loom gives the top back as sparkle");
        {
            const auto woven = render (10.0f, 1.0f), plain = render (10.0f, 0.0f);
            const float gain = bandDb (woven, 12000, 16000, 48000, 48000) - bandDb (plain, 12000, 16000, 48000, 48000);
            const float body = bandDb (woven, 200, 1500, 48000, 48000) - bandDb (plain, 200, 1500, 48000, 48000);
            logMessage ("sparkle above 12 kHz +" + juce::String (gain, 2) + " dB, body " + juce::String (body, 3) + " dB");
            expectGreaterThan (gain, 0.3f);
            expectWithinAbsoluteError (body, 0.0f, 0.05f);
        }

        beginTest ("Without De-harsh cutting, the loom changes nothing, bit for bit");
        {
            const auto woven = render (0.0f, 1.0f), plain = render (0.0f, 0.0f);
            expect (identicalAfter (plain, woven, 0));
        }
    }
};

static AirTests airTests;
static LoomTests loomTests;

} // namespace linen::test
