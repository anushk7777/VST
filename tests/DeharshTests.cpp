#include "TestSignals.h"

#include "dsp/PitchTracker.h"

namespace linen::test
{

namespace
{
    constexpr double sr = 48000.0;
    constexpr int block = 512;

    /** Long-term average level (dB) at a frequency: Welch average of Hann-windowed FFTs. */
    struct Spectrum
    {
        explicit Spectrum (const juce::AudioBuffer<float>& audio, int start = 0, int channel = 0)
        {
            constexpr int order = 13, size = 1 << order;
            juce::dsp::FFT fft (order);
            std::vector<float> buf ((size_t) size * 2);
            power.assign ((size_t) size / 2 + 1, 0.0);

            int frames = 0;
            for (int pos = start; pos + size <= audio.getNumSamples(); pos += size / 2, ++frames)
            {
                std::fill (buf.begin(), buf.end(), 0.0f);
                for (int j = 0; j < size; ++j)
                    buf[(size_t) j] = audio.getSample (channel, pos + j)
                                      * 0.5f * (1.0f - std::cos (juce::MathConstants<float>::twoPi * (float) j / (float) size));
                fft.performRealOnlyForwardTransform (buf.data(), true);
                for (size_t k = 0; k < power.size(); ++k)
                    power[k] += (double) buf[2 * k] * buf[2 * k] + (double) buf[2 * k + 1] * buf[2 * k + 1];
            }

            for (auto& p : power)
                p /= juce::jmax (1, frames);
            binHz = sr / size;
        }

        /** Average level over ±1/12 octave around hz. */
        double levelAt (double hz) const
        {
            const auto lo = (size_t) std::floor (hz * std::pow (2.0, -1.0 / 12.0) / binHz);
            const auto hi = (size_t) std::ceil (hz * std::pow (2.0, 1.0 / 12.0) / binHz);
            double sum = 0.0;
            for (auto k = lo; k <= hi; ++k)
                sum += power[k];
            return 10.0 * std::log10 (sum / (double) (hi - lo + 1) + 1.0e-30);
        }

        std::vector<double> power;
        double binHz = 1.0;
    };

    juce::AudioBuffer<float> resonantNoise (int samples, float resonanceHz, float boostDb)
    {
        auto noise = whiteNoise (1, samples, 0.2f, 7);
        juce::dsp::IIR::Filter<float> peak (juce::dsp::IIR::Coefficients<float>::makePeakFilter (sr, resonanceHz, 6.0f,
                                                                                                juce::Decibels::decibelsToGain (boostDb)));
        auto* d = noise.getWritePointer (0);
        for (int i = 0; i < samples; ++i)
            d[i] = peak.processSample (d[i]);
        return noise;
    }

    /** A sung-vowel-like tone: harmonics of f0 with a 1/n roll-off and a strong fundamental. */
    juce::AudioBuffer<float> harmonicVoice (int samples, float f0)
    {
        juce::AudioBuffer<float> buffer (1, samples);
        buffer.clear();
        for (int n = 1; f0 * (float) n < 9000.0f; ++n)
        {
            const float amp = (n == 1 ? 0.35f : 0.12f / (float) n);
            for (int i = 0; i < samples; ++i)
                buffer.addSample (0, i, amp * std::sin (juce::MathConstants<float>::twoPi * f0 * (float) n * (float) i / (float) sr));
        }
        return buffer;
    }

    EngineParams neutralDeharsh()
    {
        EngineParams p;
        p.deharsh.glareFocus = false;
        p.deharsh.bodyProtect = false;
        return p;
    }

    juce::AudioBuffer<float> render (const EngineParams& p, const juce::AudioBuffer<float>& in, int& latency)
    {
        Engine engine;
        engine.setParams (p);
        engine.prepare (sr, block, in.getNumChannels());
        latency = engine.getLatencySamples();
        return runInBlocks (engine, in, block);
    }
}

class DeharshTests final : public juce::UnitTest
{
public:
    DeharshTests() : juce::UnitTest ("De-harsh", "Linen") {}

    void runTest() override
    {
        beginTest ("Latency is one FFT length, scaled with sample rate");
        {
            ResonanceSuppressor suppressor;
            suppressor.prepare (48000.0, 2);
            expectEquals (suppressor.getLatencySamples(), 2048);
            suppressor.prepare (96000.0, 2);
            expectEquals (suppressor.getLatencySamples(), 4096);
        }

        beginTest ("Depth 0 is an exact, latency-aligned pass-through");
        {
            auto p = neutralDeharsh();
            p.deharsh.depth = 0.0f;
            const auto in = whiteNoise (2, 48000);
            int latency = 0;
            const auto out = render (p, in, latency);
            expect (identicalAfter (in, out, latency), "bit-exact");
        }

        beginTest ("Turned down to 0, it becomes exact again once the cuts have let go, without a jump");
        {
            Engine engine;
            auto p = neutralDeharsh();
            p.deharsh.depth = 10.0f;
            engine.setParams (p);
            engine.prepare (sr, block, 1);
            const int latency = engine.getLatencySamples();

            // A resonant voice for a second (so there is plenty to cut), then Depth 0.
            const auto in = resonantNoise (48000 * 4, 3000.0f, 15.0f);
            auto out = in;
            for (int pos = 0; pos < out.getNumSamples(); pos += block)
            {
                if (pos >= 48000)
                {
                    p.deharsh.depth = 0.0f;
                    engine.setParams (p);
                }
                juce::AudioBuffer<float> b (out.getArrayOfWritePointers(), 1, pos, juce::jmin (block, out.getNumSamples() - pos));
                engine.process (b);
            }

            // From 2.5 s on (well after the release), bit-exact.
            juce::AudioBuffer<float> inTail (1, 48000), outTail (1, 48000);
            inTail.copyFrom (0, 0, in, 0, 48000 * 3 - latency, 48000);
            outTail.copyFrom (0, 0, out, 0, 48000 * 3, 48000);
            expect (identicalAfter (inTail, outTail, 0), "exact after the release");

            // ...and the hand-over itself is far below hearing: in the second
            // around it the output strays from the input by less than -60 dB
            // of the signal's peak.
            float stray = 0.0f;
            for (int i = 48000 * 2; i < 48000 * 3; ++i)
                stray = juce::jmax (stray, std::abs (out.getSample (0, i) - in.getSample (0, i - latency)));
            const float strayDb = juce::Decibels::gainToDecibels (stray / in.getMagnitude (0, 0, in.getNumSamples()), -300.0f);
            logMessage ("largest difference around the hand-over: " + juce::String (strayDb, 1) + " dB re peak");
            expectLessThan (strayDb, -60.0f);
        }

        beginTest ("50% Mix with De-harsh latency sums an impulse with no comb filtering");
        {
            auto p = neutralDeharsh();
            p.deharsh.depth = 0.0f;
            p.mix = 0.5f;
            int latency = 0;
            const auto out = render (p, impulse (2, 8192, 100), latency);

            float stray = 0.0f;
            for (int i = 0; i < out.getNumSamples(); ++i)
                if (i != latency + 100)
                    stray = juce::jmax (stray, std::abs (out.getSample (0, i)));

            expectWithinAbsoluteError (out.getSample (0, latency + 100), 1.0f, 1.0e-4f);
            expectLessOrEqual (stray, 1.0e-4f);
        }

        beginTest ("A resonance is cut while its neighbours stay put");
        {
            const auto in = resonantNoise (48000 * 6, 3000.0f, 12.0f);
            auto p = neutralDeharsh();
            p.deharsh.depth = 10.0f;
            p.deharsh.hard = true;
            int latency = 0;
            const auto out = render (p, in, latency);

            const Spectrum before (in, 48000), after (out, 48000 + latency);
            const double cutAtPeak = before.levelAt (3000.0) - after.levelAt (3000.0);
            logMessage ("cut at 3 kHz: " + juce::String (cutAtPeak, 2) + " dB");
            expectGreaterThan (cutAtPeak, 6.0, "resonance should be clearly reduced");

            for (const double hz : { 500.0, 1000.0, 8000.0, 12000.0 })
            {
                const double change = std::abs (before.levelAt (hz) - after.levelAt (hz));
                logMessage ("change at " + juce::String (hz) + " Hz: " + juce::String (change, 2) + " dB");
                expectLessThan (change, 1.0, "neighbours at " + juce::String (hz) + " Hz should not move");
            }
        }

        beginTest ("Soft mode still cuts a resonance, more gently than Hard");
        {
            const auto in = resonantNoise (48000 * 6, 3000.0f, 12.0f);
            auto p = neutralDeharsh();
            p.deharsh.depth = 10.0f;
            int latency = 0;
            const auto soft = render (p, in, latency);
            p.deharsh.hard = true;
            const auto hard = render (p, in, latency);

            const Spectrum before (in, 48000), afterSoft (soft, 48000 + latency), afterHard (hard, 48000 + latency);
            const double softCut = before.levelAt (3000.0) - afterSoft.levelAt (3000.0);
            const double hardCut = before.levelAt (3000.0) - afterHard.levelAt (3000.0);
            logMessage ("soft cut " + juce::String (softCut, 2) + " dB, hard cut " + juce::String (hardCut, 2) + " dB");
            expectGreaterThan (softCut, 1.0);
            expectLessThan (softCut, hardCut);
        }

        beginTest ("Max Cut caps the reduction");
        {
            const auto in = resonantNoise (48000 * 6, 3000.0f, 18.0f);
            auto p = neutralDeharsh();
            p.deharsh.depth = 15.0f;
            p.deharsh.hard = true;
            p.deharsh.maxCutDb = 3.0f;
            int latency = 0;
            const auto out = render (p, in, latency);

            const Spectrum before (in, 48000), after (out, 48000 + latency);
            const double cut = before.levelAt (3000.0) - after.levelAt (3000.0);
            logMessage ("capped cut: " + juce::String (cut, 2) + " dB");
            expectLessOrEqual (cut, 3.3);
            expectGreaterThan (cut, 1.0);
        }

        beginTest ("Body protect keeps the sung fundamental at full weight");
        {
            const auto in = harmonicVoice (48000 * 4, 200.0f);
            auto p = neutralDeharsh();
            p.deharsh.depth = 15.0f;
            p.deharsh.hard = true;
            p.deharsh.focusLowHz = 20.0f;
            int latency = 0;

            p.deharsh.bodyProtect = false;
            const auto unprotected = render (p, in, latency);
            p.deharsh.bodyProtect = true;
            const auto protectedOut = render (p, in, latency);

            const Spectrum before (in, 24000), afterOff (unprotected, 24000 + latency), afterOn (protectedOut, 24000 + latency);
            const double lossOff = before.levelAt (200.0) - afterOff.levelAt (200.0);
            const double lossOn = before.levelAt (200.0) - afterOn.levelAt (200.0);
            logMessage ("f0 loss without protection " + juce::String (lossOff, 2) + " dB, with " + juce::String (lossOn, 2) + " dB");
            expectGreaterThan (lossOff, 2.0, "without protection a dominant fundamental does get cut");
            expectLessThan (lossOn, 1.0);
        }

        beginTest ("Steady cuts do not buzz at the frame rate");
        {
            // A loud pure tone is a 'resonance' that gets a constant cut once settled.
            juce::AudioBuffer<float> sine (1, 48000 * 2);
            for (int i = 0; i < sine.getNumSamples(); ++i)
                sine.setSample (0, i, 0.5f * std::sin (juce::MathConstants<float>::twoPi * 1000.0f * (float) i / (float) sr)
                                      + 0.02f * (juce::Random (i).nextFloat() - 0.5f));

            auto p = neutralDeharsh();
            p.deharsh.depth = 10.0f;
            p.deharsh.hard = true;
            int latency = 0;
            const auto out = render (p, sine, latency);

            // RMS over 4 tone periods (192 samples) should be flat; hop-rate buzz would ripple it.
            float lo = 1.0e9f, hi = 0.0f;
            for (int pos = 48000; pos + 192 <= out.getNumSamples(); pos += 64)
            {
                const float rms = out.getRMSLevel (0, pos, 192);
                lo = juce::jmin (lo, rms);
                hi = juce::jmax (hi, rms);
            }
            const float rippleDb = juce::Decibels::gainToDecibels (hi / lo);
            logMessage ("ripple: " + juce::String (rippleDb, 3) + " dB");
            expectLessThan (rippleDb, 0.3f);
        }

        beginTest ("Delta outputs exactly what is removed (normal + delta = input)");
        {
            const auto in = resonantNoise (48000 * 2, 3000.0f, 12.0f);
            auto p = neutralDeharsh();
            p.deharsh.depth = 10.0f;
            int latency = 0;
            const auto normal = render (p, in, latency);
            p.deharsh.delta = true;
            const auto delta = render (p, in, latency);

            juce::AudioBuffer<float> sum (normal);
            sum.addFrom (0, 0, delta, 0, 0, sum.getNumSamples());
            expectLessOrEqual (maxAbsDifference (in, sum, latency), 1.0e-4f);
        }
    }
};

class PitchTrackerTests final : public juce::UnitTest
{
public:
    PitchTrackerTests() : juce::UnitTest ("Pitch tracker", "Linen") {}

    void runTest() override
    {
        PitchTracker tracker;
        tracker.prepare (sr, 2048);

        beginTest ("Finds a sine's pitch");
        {
            std::vector<float> frame (2048);
            for (int i = 0; i < 2048; ++i)
                frame[(size_t) i] = 0.3f * std::sin (juce::MathConstants<float>::twoPi * 220.0f * (float) i / (float) sr);
            const auto r = tracker.analyse (frame.data());
            expectWithinAbsoluteError (r.frequency, 220.0f, 1.0f);
            expectGreaterThan (r.clarity, 0.9f);
        }

        beginTest ("Finds the fundamental of a harmonic tone, not a harmonic");
        {
            const auto voice = harmonicVoice (2048, 150.0f);
            const auto r = tracker.analyse (voice.getReadPointer (0));
            expectWithinAbsoluteError (r.frequency, 150.0f, 1.5f);
        }

        beginTest ("Noise is not mistaken for a voice");
        {
            const auto noise = whiteNoise (1, 2048, 0.3f, 3);
            const auto r = tracker.analyse (noise.getReadPointer (0));
            expectLessThan (r.clarity, 0.7f);
        }
    }
};

static DeharshTests deharshTests;
static PitchTrackerTests pitchTrackerTests;

} // namespace linen::test
