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

        beginTest ("Optical two-stage release: half lets go fast, the rest slowly, slower after long holds");
        {
            struct Recovery { double half, full; };

            const auto recovery = [this] (int burstSamples) -> Recovery
            {
                SilkCompressor comp;
                CompParams p;
                p.thresholdDb = -30.0f;
                p.ratio = 4.0f;
                p.releaseMs = 60.0f;
                p.autoMakeup = false;
                comp.setParams (p);
                comp.prepare (sr, block, 1);

                // Loud burst, then a quiet tone: track the meter as the quiet tone comes back.
                const int total = burstSamples + 48000 * 8;
                auto audio = silence (total);
                addSine (audio, 1000.0, -6.0f, 0, burstSamples);
                addSine (audio, 1000.0, -50.0f, burstSamples, total);

                float peakGr = 0.0f;
                Recovery r { -1.0, -1.0 };
                for (int pos = 0; pos < total; pos += 240)
                {
                    juce::AudioBuffer<float> b (audio.getArrayOfWritePointers(), 1, pos, juce::jmin (240, total - pos));
                    comp.process (b);
                    const float gr = comp.getGainReductionDb();
                    if (pos < burstSamples)
                        peakGr = gr;
                    else
                    {
                        const double t = (double) (pos + 240 - burstSamples) / sr;
                        if (r.half < 0.0 && gr < peakGr * 0.5f) r.half = t;
                        if (r.full < 0.0 && gr < 1.0f) r.full = t;
                    }
                }
                expect (r.full > 0.0, "never recovered");
                return r;
            };

            const auto afterPeak = recovery (4800);       // 0.1 s
            const auto afterHold = recovery (48000 * 4);  // 4 s
            logMessage ("after a peak: half " + juce::String (afterPeak.half, 3) + " s, full " + juce::String (afterPeak.full, 3)
                        + " s; after a long hold: half " + juce::String (afterHold.half, 3) + " s, full " + juce::String (afterHold.full, 3) + " s");
            expectLessThan (afterPeak.half, 0.2, "first half releases quickly");
            expectLessThan (afterHold.half, 0.3, "...even after a long hold");
            expectGreaterThan (afterHold.full, afterPeak.full * 2.0, "the cell's memory slows the tail after a long hold");
        }

        beginTest ("Cotton: no colour at all while it isn't compressing");
        {
            SilkCompressor comp;
            CompParams p;
            p.thresholdDb = -10.0f;
            p.ratio = 3.0f;
            p.cotton = 1.0f;
            p.autoMakeup = false;
            comp.setParams (p);
            comp.prepare (sr, block, 1);
            auto tone = silence (65536);
            addSine (tone, 500.0, -30.0f);
            runBlocks (comp, tone);
            const double h1 = toneDb (tone, 500.0), h2 = toneDb (tone, 1000.0), h3 = toneDb (tone, 1500.0);
            expectLessThan (h2 - h1, -100.0);
            expectLessThan (h3 - h1, -100.0);
        }

        beginTest ("Cotton stays a whisper: under -52 dB at 50% (6 dB GR), under -42 dB at 100% (10 dB GR)");
        {
            // A belted 1 kHz note at -6 dBFS peak, with auto gain, as on a real vocal.
            struct Reading { double thd, h2, h3; float gr; };
            const auto measure = [] (float cotton, float ratio, float thresholdDb)
            {
                SilkCompressor comp;
                CompParams p;
                p.thresholdDb = thresholdDb;
                p.ratio = ratio;
                p.cotton = cotton;
                comp.setParams (p);
                comp.prepare (sr, block, 1);
                auto tone = sineWave (1000.0, -6.0f, 65536);
                runBlocks (comp, tone);
                const double h1 = toneDb (tone, 1000.0);
                return Reading { thdDb (tone, 1000.0), toneDb (tone, 2000.0) - h1, toneDb (tone, 3000.0) - h1, comp.getGainReductionDb() };
            };

            const auto half = measure (0.5f, 2.0f, -18.0f);
            const auto full = measure (1.0f, 3.0f, -21.0f);
            const auto none = measure (0.0f, 3.0f, -21.0f);
            logMessage ("cotton 50% at " + juce::String (half.gr, 1) + " dB GR: THD " + juce::String (half.thd, 1)
                        + " dB; 100% at " + juce::String (full.gr, 1) + " dB GR: THD " + juce::String (full.thd, 1)
                        + " dB (H2 " + juce::String (full.h2, 1) + ", H3 " + juce::String (full.h3, 1)
                        + "); cotton 0: " + juce::String (none.thd, 1) + " dB");
            expectWithinAbsoluteError (half.gr, 6.0f, 1.5f);
            expectWithinAbsoluteError (full.gr, 10.0f, 1.5f);
            expectLessThan (half.thd, -52.0);
            expectLessThan (full.thd, -42.0);
            expectGreaterThan (full.thd, none.thd + 20.0, "...but the texture is there");
            expectGreaterThan (full.h2, full.h3, "mostly 2nd harmonic");
            expectLessThan (none.thd, -90.0, "Cotton 0 adds nothing");
        }

        beginTest ("Cotton: loud lines turn warmer and softer on top");
        {
            const auto shape = [] (float cotton)
            {
                SilkCompressor comp;
                CompParams p;
                p.thresholdDb = -36.0f;
                p.ratio = 4.0f;
                p.cotton = cotton;
                p.autoMakeup = false;
                comp.setParams (p);
                comp.prepare (sr, block, 1);
                auto noise = whiteNoise (1, 48000 * 2, 0.4f, 4);
                runBlocks (comp, noise);
                return std::pair { bandDb (noise, 220, 340, 24000, 48000) - bandDb (noise, 900, 1200, 24000, 48000),
                                   bandDb (noise, 11000, 15000, 24000, 48000) - bandDb (noise, 900, 1200, 24000, 48000) };
            };

            const auto [warm1, top1] = shape (1.0f);
            const auto [warm0, top0] = shape (0.0f);
            logMessage ("low-mid " + juce::String (warm1 - warm0, 2) + " dB, top " + juce::String (top1 - top0, 2) + " dB");
            expectGreaterThan (warm1 - warm0, 0.8f, "warmer");
            expectLessThan (top1 - top0, -1.5f, "softer on top");
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

        beginTest ("Ratio 1 is a clean, latency-aligned bypass, even with Cotton up");
        {
            SilkCompressor comp;
            CompParams p;
            p.cotton = 1.0f;
            comp.setParams (p);
            comp.prepare (sr, block, 2);
            auto in = whiteNoise (2, 24000, 0.9f);
            auto out = in;
            runBlocks (comp, out);
            expect (identicalAfter (in, out, comp.getLatencySamples()), "bit-exact");
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

static CompressorTests compressorTests;
static AirTests airTests;

} // namespace linen::test
