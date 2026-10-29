#include "TestSignals.h"

#include "dsp/Eq1073.h"
#include "dsp/Preamp.h"

namespace linen::test
{

namespace
{
    constexpr double sr = 48000.0;
    constexpr int block = 512;

    juce::AudioBuffer<float> sine (float hz, float levelDb, int samples)
    {
        juce::AudioBuffer<float> buffer (1, samples);
        // Phase in double: a float phase jitters enough at large i to show up as -70 dB spurs.
        const double amp = juce::Decibels::decibelsToGain ((double) levelDb);
        for (int i = 0; i < samples; ++i)
            buffer.setSample (0, i, (float) (amp * std::sin (juce::MathConstants<double>::twoPi * hz * i / sr)));
        return buffer;
    }

    juce::AudioBuffer<float> runPreamp (float drive, const juce::AudioBuffer<float>& in)
    {
        Preamp preamp;
        preamp.setDrive (drive);
        preamp.prepare (sr, block, in.getNumChannels());
        juce::AudioBuffer<float> out (in);
        for (int pos = 0; pos < out.getNumSamples(); pos += block)
        {
            juce::AudioBuffer<float> b (out.getArrayOfWritePointers(), out.getNumChannels(), pos, juce::jmin (block, out.getNumSamples() - pos));
            preamp.process (b);
        }
        return out;
    }

    /** Highest spectral level (relative to the fundamental) away from the
        fundamental, its in-band harmonics and DC: aliasing and noise. */
    double worstSpurDb (const juce::AudioBuffer<float>& audio, double fundamentalHz)
    {
        constexpr int order = 15, n = 1 << order;
        juce::dsp::FFT fft (order);
        std::vector<float> buf ((size_t) n * 2, 0.0f);
        for (int i = 0; i < n; ++i)
            buf[(size_t) i] = audio.getSample (0, 16384 + i) * blackmanHarris (i, n);
        fft.performFrequencyOnlyForwardTransform (buf.data(), true);

        const double binHz = sr / n;
        double fundamental = 0.0, worst = 0.0;
        for (int k = 1; k < n / 2; ++k)
        {
            const double hz = k * binHz;
            const double nearestHarmonic = std::round (hz / fundamentalHz) * fundamentalHz;
            const bool isHarmonic = nearestHarmonic > 0.0 && nearestHarmonic < sr / 2 && std::abs (hz - nearestHarmonic) < 8 * binHz;
            if (std::abs (hz - fundamentalHz) < 8 * binHz)
                fundamental = std::max (fundamental, (double) buf[(size_t) k]);
            else if (! isHarmonic && hz > 30.0)
                worst = std::max (worst, (double) buf[(size_t) k]);
        }
        return 20.0 * std::log10 (worst / fundamental + 1.0e-30);
    }
}

class PreampTests final : public juce::UnitTest
{
public:
    PreampTests() : juce::UnitTest ("Preamp", "Linen") {}

    void runTest() override
    {
        beginTest ("Drive 0 is an exact, latency-aligned bypass");
        {
            Preamp preamp;
            preamp.prepare (sr, block, 2);
            const int latency = preamp.getLatencySamples();
            expectGreaterThan (latency, 0);

            const auto in = whiteNoise (2, 24000);
            const auto out = runPreamp (0.0f, in);
            expect (identicalAfter (in, out, latency), "bit-exact");
        }

        beginTest ("House drive (30%) stays clean on a normally recorded vocal");
        {
            // A belted note peaking at -6 dBFS, a hot one at -1 dBFS, and a low note.
            const double belt = thdDb (runPreamp (0.3f, sine (1000.0f, -6.0f, 65536)), 1000.0);
            const double hot = thdDb (runPreamp (0.3f, sine (1000.0f, -1.0f, 65536)), 1000.0);
            const double low = thdDb (runPreamp (0.3f, sine (100.0f, -6.0f, 65536)), 100.0);
            logMessage ("THD at 30%: 1 kHz -6 dBFS " + juce::String (belt, 1) + " dB, -1 dBFS " + juce::String (hot, 1)
                        + " dB, 100 Hz -6 dBFS " + juce::String (low, 1) + " dB");
            expectLessThan (belt, -55.0);
            expectLessThan (hot, -48.0);
            expectLessThan (low, -50.0);
        }

        beginTest ("Full drive is warm, never fuzz: mostly 2nd harmonic, -40 to -28 dB on a -6 dBFS note");
        {
            const auto out = runPreamp (1.0f, sine (1000.0f, -6.0f, 65536));
            const double h1 = toneDb (out, 1000.0), h2 = toneDb (out, 2000.0), h3 = toneDb (out, 3000.0);
            const double thd = thdDb (out, 1000.0);
            logMessage ("THD " + juce::String (thd, 1) + " dB, H2 " + juce::String (h2 - h1, 1) + " dB, H3 " + juce::String (h3 - h1, 1) + " dB");
            expectGreaterThan (h2, h3 + 10.0, "2nd harmonic should lead");
            expectGreaterThan (thd, -40.0, "audible colour at the top of the knob");
            expectLessThan (thd, -28.0, "...but never fuzz");
        }

        beginTest ("Drive rises evenly: each step up the knob adds colour");
        {
            double last = -200.0;
            juce::String report;
            for (const float drive : { 0.1f, 0.3f, 0.5f, 0.7f, 0.9f })
            {
                const double thd = thdDb (runPreamp (drive, sine (1000.0f, -6.0f, 65536)), 1000.0);
                report << juce::roundToInt (drive * 100) << "%: " << juce::String (thd, 1) << " dB  ";
                expectGreaterThan (thd, last + 4.0, "drive " + juce::String (drive));
                last = thd;
            }
            logMessage (report);
        }

        beginTest ("Drive changes colour, not loudness (small-signal gain is 1)");
        {
            for (const float drive : { 0.25f, 0.5f, 1.0f })
            {
                const auto out = runPreamp (drive, sine (1000.0f, -40.0f, 65536));
                expectWithinAbsoluteError (toneDb (out, 1000.0), -40.0, 0.5, "drive " + juce::String (drive));
            }
        }

        beginTest ("4x oversampling keeps aliasing below -90 dB");
        {
            const auto out = runPreamp (1.0f, sine (9000.0f, -6.0f, 65536));
            const double spur = worstSpurDb (out, 9000.0);
            logMessage ("worst spur: " + juce::String (spur, 1) + " dB");
            expectLessThan (spur, -90.0);
        }

        beginTest ("Loud notes bloom: more 2nd harmonic when belted than when soft");
        {
            const auto loud = runPreamp (0.7f, sine (300.0f, -6.0f, 65536));
            const auto soft = runPreamp (0.7f, sine (300.0f, -30.0f, 65536));
            const double loudH2 = toneDb (loud, 600.0) - toneDb (loud, 300.0);
            const double softH2 = toneDb (soft, 600.0) - toneDb (soft, 300.0);
            logMessage ("H2 loud " + juce::String (loudH2, 1) + " dB, soft " + juce::String (softH2, 1) + " dB");
            expectGreaterThan (loudH2, softH2 + 6.0);
        }

        beginTest ("Transformer iron thickens low notes before the mids");
        {
            const auto low = runPreamp (0.7f, sine (60.0f, -6.0f, 65536));
            const auto mid = runPreamp (0.7f, sine (1000.0f, -6.0f, 65536));
            const double lowH3 = toneDb (low, 180.0) - toneDb (low, 60.0);
            const double midH3 = toneDb (mid, 3000.0) - toneDb (mid, 1000.0);
            logMessage ("H3 at 60 Hz " + juce::String (lowH3, 1) + " dB, at 1 kHz " + juce::String (midH3, 1) + " dB");
            expectGreaterThan (lowH3, midH3 + 6.0);
        }
    }
};

class EqTests final : public juce::UnitTest
{
public:
    EqTests() : juce::UnitTest ("1073-style EQ", "Linen") {}

    void runTest() override
    {
        beginTest ("Flat settings are bit-exact");
        {
            Eq1073 eq;
            EqParams p;
            eq.setParams (p);
            eq.prepare (sr, 2);
            auto in = whiteNoise (2, 8192);
            auto out = in;
            eq.process (out);
            expectEquals (maxAbsDifference (in, out), 0.0f);
        }

        beginTest ("Mid bell lands its gain at every switch position");
        {
            for (int f = 1; f < (int) EqParams::midHz.size(); ++f)
                for (const float gain : { -12.0f, 6.0f, 15.0f })
                {
                    Eq1073 eq;
                    EqParams p;
                    p.midFreq = f;
                    p.midGainDb = gain;
                    eq.setParams (p);
                    eq.prepare (sr, 1);
                    expectWithinAbsoluteError (eq.magnitudeDb (EqParams::midHz[(size_t) f]), (double) gain, 0.5,
                                               juce::String (EqParams::midHz[(size_t) f]) + " Hz at " + juce::String (gain) + " dB");
                }
        }

        beginTest ("Low shelf reaches its gain below the corner, flat in the mids");
        {
            for (int f = 1; f < (int) EqParams::lowShelfHz.size(); ++f)
            {
                Eq1073 eq;
                EqParams p;
                p.lowFreq = f;
                p.lowGainDb = 10.0f;
                eq.setParams (p);
                eq.prepare (sr, 1);
                const double hz = EqParams::lowShelfHz[(size_t) f];
                expectWithinAbsoluteError (eq.magnitudeDb (hz / 4.0), 10.0, 1.0, juce::String (hz) + " Hz shelf");
                expectWithinAbsoluteError (eq.magnitudeDb (hz * 16.0), 0.0, 0.5);
            }
        }

        beginTest ("High shelf lifts the air, leaves the body alone");
        {
            Eq1073 eq;
            EqParams p;
            p.highGainDb = 10.0f;
            eq.setParams (p);
            eq.prepare (sr, 1);
            expectWithinAbsoluteError (eq.magnitudeDb (20000.0), 10.0, 1.5);
            expectWithinAbsoluteError (eq.magnitudeDb (500.0), 0.0, 0.25);
        }

        beginTest ("The processed audio matches the designed response");
        {
            Eq1073 eq;
            EqParams p;
            p.lowFreq = 3;
            p.lowGainDb = 4.0f;
            p.midFreq = 4;
            p.midGainDb = -6.0f;
            p.highGainDb = 3.0f;
            eq.setParams (p);
            eq.prepare (sr, 1);

            for (const float hz : { 150.0f, 1000.0f, 3200.0f, 15000.0f })
            {
                auto tone = sine (hz, -20.0f, 65536);
                eq.reset();
                eq.process (tone);
                expectWithinAbsoluteError (toneDb (tone, hz) + 20.0, eq.magnitudeDb (hz), 0.05, juce::String (hz) + " Hz");
            }
        }
    }
};

static PreampTests preampTests;
static EqTests eqTests;

} // namespace linen::test
