#include "TestSignals.h"

#include "dsp/HeroMacro.h"

namespace linen::test
{

namespace
{
    constexpr double sr = 48000.0;
    constexpr int block = 512;

    void addTone (juce::AudioBuffer<float>& b, double hz, float levelDb)
    {
        const double amp = juce::Decibels::decibelsToGain ((double) levelDb);
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i)
                b.addSample (ch, i, (float) (amp * std::sin (juce::MathConstants<double>::twoPi * hz * i / sr)));
    }

    /** A held A (220 Hz) with a falling top, the way a sung vowel looks. */
    juce::AudioBuffer<float> heldA (int samples)
    {
        juce::AudioBuffer<float> b (2, samples);
        b.clear();
        for (int n = 1; 220.0 * n < 12000.0; ++n)
            addTone (b, 220.0 * n, -16.0f - 6.0f * std::log2 ((float) n) - (float) juce::jmax (0.0, 12.0 * std::log2 (220.0 * n / 2000.0)));
        return b;
    }

    juce::AudioBuffer<float> render (const EngineParams& p, const juce::AudioBuffer<float>& in, int& latency)
    {
        Engine engine;
        engine.setParams (p);
        engine.prepare (sr, block, in.getNumChannels());
        latency = engine.getLatencySamples();
        return runInBlocks (engine, in, block);
    }

    /** A tone's level change (dB) through p, measured late in the render. */
    double change (const EngineParams& p, const juce::AudioBuffer<float>& in, double hz)
    {
        int latency = 0;
        const auto out = render (p, in, latency);
        const int start = in.getNumSamples() - 32768 - 4096;
        return toneDb (out, hz, start, 32768) - toneDb (in, hz, start - latency, 32768);
    }

    EngineParams aMinor (float tune)
    {
        EngineParams p;
        p.comp.thresholdDb = -18.0f;
        p.deharsh.key = 9;        // A
        p.deharsh.minor = true;
        p.deharsh.tune = tune;
        return p;
    }
}

class KeyTests final : public juce::UnitTest
{
public:
    KeyTests() : juce::UnitTest ("De-harsh Key", "Linen") {}

    void runTest() override
    {
        beginTest ("Key set at Tune 0, or Tune up with the key Off, changes nothing, bit for bit");
        {
            const auto vocal = sungPhrase (72000);
            int latency = 0;
            auto offTune = aMinor (1.0f);
            offTune.deharsh.key = -1;
            for (const auto& p : { aMinor (0.0f), offTune })
            {
                const auto out = render (p, vocal, latency);
                expect (identicalAfter (vocal, out, latency));
            }
        }

        beginTest ("In A minor: an out-of-key ring (F#) is tamed, an in-key one (E) is spared");
        {
            auto in = heldA (48000 * 3);
            addTone (in, 1479.98, -24.0f);   // F#6: out of A minor, ringing above the voice
            addTone (in, 2637.02, -24.0f);   // E7: in A minor
            const auto p = aMinor (1.0f);
            const double outOfKey = change (p, in, 1479.98), inKey = change (p, in, 2637.02);
            logMessage ("F# ring " + juce::String (outOfKey, 2) + " dB, E ring " + juce::String (inKey, 2) + " dB");
            expectLessThan (outOfKey, -3.0);
            expectGreaterThan (inKey, -0.5);
        }

        beginTest ("The sung note's in-key harmonics are lifted a little, its out-of-key ones lowered");
        {
            const auto in = heldA (48000 * 3);
            const double e = change (aMinor (1.0f), in, 1320.0);         // 6th harmonic of A: E, in key
            const double cSharp = change (aMinor (1.0f), in, 1100.0);    // 5th harmonic of A: C#, out of A minor
            logMessage ("E harmonic " + juce::String (e, 2) + " dB, C# harmonic " + juce::String (cSharp, 2) + " dB");
            expectGreaterThan (e, 0.5);
            expectLessThan (e, 2.5);
            expectLessThan (cSharp, -0.5);
            expectGreaterThan (cSharp, -2.5);
        }
    }
};

class HeroTests final : public juce::UnitTest
{
public:
    HeroTests() : juce::UnitTest ("LINEN knob", "Linen") {}

    void runTest() override
    {
        EngineParams clean;
        clean.comp.thresholdDb = -18.0f;

        beginTest ("At 0 it changes nothing");
        {
            const auto p = HeroMacro::apply (clean, 0.0f);
            expectEquals (p.preampDrive, clean.preampDrive);
            expectEquals (p.comp.ratio, clean.comp.ratio);
            expectEquals (p.air.amount, clean.air.amount);
            expectEquals (p.outputGainDb, clean.outputGainDb);
        }

        beginTest ("Turning it up brings everything in, in step");
        {
            float lastDrive = 0.0f, lastRatio = 1.0f, lastAir = 0.0f, lastDepth = 0.0f;
            for (const float h : { 0.25f, 0.5f, 0.75f, 1.0f })
            {
                const auto p = HeroMacro::apply (clean, h);
                expectGreaterOrEqual (p.preampDrive, lastDrive);
                expectGreaterOrEqual (p.comp.ratio, lastRatio);
                expectGreaterOrEqual (p.air.amount, lastAir);
                expectGreaterOrEqual (p.deharsh.depth, lastDepth);
                lastDrive = p.preampDrive; lastRatio = p.comp.ratio; lastAir = p.air.amount; lastDepth = p.deharsh.depth;
            }
            expectGreaterThan (lastRatio, 2.0f);
            expectGreaterThan (lastAir, 0.4f);
        }

        beginTest ("It never takes away what the user set");
        {
            auto user = clean;
            user.preampDrive = 0.8f;
            user.comp.ratio = 5.0f;
            const auto p = HeroMacro::apply (user, 1.0f);
            expectEquals (p.preampDrive, 0.8f);
            expectEquals (p.comp.ratio, 5.0f);
        }

        beginTest ("Changes the sound, not the loudness (within 1 dB on a sung phrase)");
        {
            const auto vocal = sungPhrase (48000 * 5);
            const auto level = [&vocal] (const EngineParams& p)
            {
                int latency = 0;
                const auto out = render (p, vocal, latency);
                const int start = 48000 + latency;
                return juce::Decibels::gainToDecibels (out.getRMSLevel (0, start, out.getNumSamples() - start));
            };
            const float reference = level (clean);
            juce::String report;
            for (const float h : { 0.25f, 0.5f, 0.75f, 1.0f })
            {
                auto p = clean;
                p.linen = h;
                const float drift = level (p) - reference;
                report << juce::String (juce::roundToInt (h * 100.0f)) << "%: " << juce::String (drift, 2) << " dB  ";
                expectWithinAbsoluteError (drift, 0.0f, 1.0f, juce::String (h));
            }
            logMessage (report);
        }

        beginTest ("At 100% it is bold but stays inside the distortion budget");
        {
            auto p = clean;
            p.linen = 1.0f;
            const auto tone = sineWave (1000.0, -6.0f, 65536 + 16384, sr, 2);
            int latency = 0;
            const auto out = render (p, tone, latency);
            const double thd = thdDb (out, 1000.0, 16384 + 8192, 32768);
            logMessage ("THD at 100% on a -6 dBFS note: " + juce::String (thd, 1) + " dB");
            expectLessThan (thd, -45.0);
        }
    }
};

static KeyTests keyTests;
static HeroTests heroTests;

} // namespace linen::test
