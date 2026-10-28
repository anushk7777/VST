#include "TestSignals.h"

#include "dsp/Biquad.h"
#include "dsp/PainEngine.h"

namespace linen::test
{

namespace
{
    constexpr double sr = 48000.0;
    constexpr int block = 512;

    /** 400 Hz + 2.2 kHz, equal amplitudes, combined peak at levelDb. */
    juce::AudioBuffer<float> twoTone (int samples, float levelDb)
    {
        juce::AudioBuffer<float> b (1, samples);
        const double amp = 0.5 * juce::Decibels::decibelsToGain ((double) levelDb);
        for (int i = 0; i < samples; ++i)
            b.setSample (0, i, (float) (amp * (std::sin (juce::MathConstants<double>::twoPi * 400.0 * i / sr)
                                             + std::sin (juce::MathConstants<double>::twoPi * 2200.0 * i / sr))));
        return b;
    }

    struct PainRun
    {
        juce::AudioBuffer<float> out;
        float peakStress = 0.0f;
        int latency = 0;
    };

    PainRun runPain (float pain, const juce::AudioBuffer<float>& in)
    {
        PainEngine engine;
        engine.setPain (pain);
        engine.prepare (sr, block, in.getNumChannels());
        PainRun run { in, 0.0f, engine.getLatencySamples() };
        for (int pos = 0; pos < run.out.getNumSamples(); pos += block)
        {
            juce::AudioBuffer<float> b (run.out.getArrayOfWritePointers(), run.out.getNumChannels(), pos,
                                        juce::jmin (block, run.out.getNumSamples() - pos));
            engine.process (b);
            if (pos > 24000)
                run.peakStress = juce::jmax (run.peakStress, engine.getStressLevel());
        }
        return run;
    }

    EngineParams houseSound()
    {
        EngineParams p;
        p.preampDrive = 0.25f;
        p.deharsh.depth = 5.0f;
        p.comp.thresholdDb = -18.0f;
        p.comp.ratio = 2.0f;
        p.comp.releaseMs = 60.0f;
        p.comp.cotton = 0.5f;
        p.air.amount = 0.3f;
        p.air.deEss = 0.5f;
        return p;
    }

    juce::AudioBuffer<float> renderEngine (const EngineParams& p, const juce::AudioBuffer<float>& in, int& latency)
    {
        Engine engine;
        engine.setParams (p);
        engine.prepare (sr, block, in.getNumChannels());
        latency = engine.getLatencySamples();
        return runInBlocks (engine, in, block);
    }

    float bandEnergyDb (const juce::AudioBuffer<float>& b, double lowHz, int start)
    {
        Biquad hp1, hp2;
        hp1.c = Biquad::Coeffs::highPass (sr, lowHz, 0.54);
        hp2.c = Biquad::Coeffs::highPass (sr, lowHz, 1.31);
        double sum = 0.0;
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const float y = hp2.process (hp1.process (b.getSample (0, i)));
            if (i >= start)
                sum += (double) y * y;
        }
        return (float) (10.0 * std::log10 (sum / (b.getNumSamples() - start) + 1.0e-30));
    }
}

class PainTests final : public juce::UnitTest
{
public:
    PainTests() : juce::UnitTest ("Pain", "Linen") {}

    void runTest() override
    {
        beginTest ("Pain 0 is an exact, latency-aligned bypass");
        {
            const auto in = whiteNoise (2, 24000, 0.5f);
            const auto run = runPain (0.0f, in);
            expectGreaterThan (run.latency, 0);
            expectEquals (maxAbsDifference (in, run.out, run.latency), 0.0f);
        }

        beginTest ("Stress and harmonics rise on a loud belt, not on a quiet line");
        {
            const auto quiet = runPain (0.75f, twoTone (65536, -24.0f));
            const auto loud = runPain (0.75f, twoTone (65536, -3.0f));

            const auto harmonicsDb = [] (const juce::AudioBuffer<float>& b)
            {
                const double h1 = toneDb (b, 400.0);
                return std::pair { toneDb (b, 800.0) - h1, toneDb (b, 1200.0) - h1 };
            };
            const auto [qH2, qH3] = harmonicsDb (quiet.out);
            const auto [lH2, lH3] = harmonicsDb (loud.out);

            logMessage ("stress quiet " + juce::String (quiet.peakStress, 2) + ", loud " + juce::String (loud.peakStress, 2)
                        + "; H2 quiet " + juce::String (qH2, 1) + " / loud " + juce::String (lH2, 1)
                        + " dB; H3 quiet " + juce::String (qH3, 1) + " / loud " + juce::String (lH3, 1) + " dB");
            expectLessThan (quiet.peakStress, 0.25f);
            expectGreaterThan (loud.peakStress, 0.5f);
            expectGreaterThan (lH2, qH2 + 15.0, "even harmonics bloom when pushed");
            expectGreaterThan (lH3, qH3 + 15.0, "odd harmonics crack in when pushed");

            // Level-compensated: the loud burst comes out about as loud as it went in.
            const auto in = twoTone (65536, -3.0f);
            const float inDb = juce::Decibels::gainToDecibels (in.getRMSLevel (0, 24000, 32000));
            const float outDb = juce::Decibels::gainToDecibels (loud.out.getRMSLevel (0, 24000 + loud.latency, 32000));
            logMessage ("loud burst level change: " + juce::String (outDb - inDb, 2) + " dB");
            expectWithinAbsoluteError (outDb - inDb, 0.0f, 1.5f);
        }

        beginTest ("Turning Pain up adds ache, not loudness (sung phrase, within 1 dB)");
        {
            const auto vocal = sungPhrase (48000 * 5);
            int latency = 0;
            const auto reference = renderEngine (houseSound(), vocal, latency);
            const float refDb = juce::Decibels::gainToDecibels (reference.getRMSLevel (0, 48000 + latency, 48000 * 3));

            juce::String report;
            for (const float pain : { 0.25f, 0.5f, 0.75f, 1.0f })
            {
                auto p = houseSound();
                p.pain = pain;
                const auto out = renderEngine (p, vocal, latency);
                const float drift = juce::Decibels::gainToDecibels (out.getRMSLevel (0, 48000 + latency, 48000 * 3)) - refDb;
                report << "pain " << pain << ": " << juce::String (drift, 2) << " dB  ";
                expectWithinAbsoluteError (drift, 0.0f, 1.0f, "pain " + juce::String (pain));
            }
            logMessage (report);
        }

        beginTest ("De-harsh clamps Pain's fizz: above 6 kHz stays within 2 dB of Pain 0");
        {
            const auto vocal = sungPhrase (48000 * 5);
            int latency = 0;
            const auto clean = renderEngine (houseSound(), vocal, latency);
            auto p = houseSound();
            p.pain = 0.8f;
            const auto pained = renderEngine (p, vocal, latency);

            const float rise = bandEnergyDb (pained, 6000.0, 48000) - bandEnergyDb (clean, 6000.0, 48000);
            logMessage ("energy above 6 kHz: " + juce::String (rise, 2) + " dB vs Pain 0");
            expectLessThan (rise, 2.0f);
        }
    }
};

static PainTests painTests;

} // namespace linen::test
