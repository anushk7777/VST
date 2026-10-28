#include "TestSignals.h"

#include "dsp/SilkMacro.h"

namespace linen::test
{

namespace
{
    constexpr double sr = 48000.0;
    constexpr int block = 512;

    /** The plugin's default settings (see Parameters.h). */
    EngineParams houseSound()
    {
        EngineParams p;
        p.preampDrive = 0.25f;
        p.deharsh.depth = 5.0f;
        p.comp.thresholdDb = -18.0f;
        p.comp.ratio = 2.0f;
        p.comp.attackMs = 12.0f;
        p.comp.releaseMs = 60.0f;
        p.comp.cotton = 0.5f;
        p.air.amount = 0.3f;
        p.air.deEss = 0.5f;
        return p;
    }

    float outputLevelDb (const EngineParams& p, const juce::AudioBuffer<float>& in)
    {
        Engine engine;
        engine.setParams (p);
        engine.prepare (sr, block, in.getNumChannels());
        const auto out = runInBlocks (engine, in, block);
        const int start = 48000 + engine.getLatencySamples();
        return juce::Decibels::gainToDecibels (out.getRMSLevel (0, start, out.getNumSamples() - start));
    }
}

class SilkTests final : public juce::UnitTest
{
public:
    SilkTests() : juce::UnitTest ("Silk macro", "Linen") {}

    void runTest() override
    {
        beginTest ("50% leaves every setting as it is");
        {
            const auto user = houseSound();
            const auto p = SilkMacro::apply (user, 0.5f);
            expectEquals (p.preampDrive, user.preampDrive);
            expectEquals (p.deharsh.depth, user.deharsh.depth);
            expectEquals (p.comp.thresholdDb, user.comp.thresholdDb);
            expectEquals (p.comp.cotton, user.comp.cotton);
            expectEquals (p.air.amount, user.air.amount);
            expectEquals (p.outputGainDb, user.outputGainDb);
        }

        beginTest ("More silk means more of everything, in order; less pulls back toward transparent");
        {
            const auto user = houseSound();
            const auto up = SilkMacro::apply (user, 1.0f), down = SilkMacro::apply (user, 0.0f);
            expectGreaterThan (up.preampDrive, user.preampDrive);
            expectGreaterThan (up.deharsh.depth, user.deharsh.depth);
            expectLessThan (up.comp.thresholdDb, user.comp.thresholdDb);
            expectGreaterThan (up.comp.cotton, user.comp.cotton);
            expectGreaterThan (up.air.amount, user.air.amount);
            expectLessThan (down.preampDrive, user.preampDrive);
            expectLessThan (down.deharsh.depth, user.deharsh.depth);

            // Drive arrives early: by 75% it is fully in, while Air has barely started.
            const auto threeQuarters = SilkMacro::apply (user, 0.75f);
            expectWithinAbsoluteError (threeQuarters.preampDrive, up.preampDrive, 0.001f);
            expectLessThan (threeQuarters.air.amount - user.air.amount, 0.25f * (up.air.amount - user.air.amount));
        }

        beginTest ("Turning Silk changes texture, not loudness (within 0.5 dB on a sung phrase)");
        {
            const auto vocal = sungPhrase (48000 * 5);
            const auto reference = outputLevelDb (houseSound(), vocal);

            juce::String report;
            for (const float silk : { 0.0f, 0.25f, 0.75f, 1.0f })
            {
                auto p = houseSound();
                p.silk = silk;
                const float drift = outputLevelDb (p, vocal) - reference;
                report << "silk " << silk << ": " << juce::String (drift, 2) << " dB  ";
                expectWithinAbsoluteError (drift, 0.0f, 0.5f, "silk " + juce::String (silk));
            }
            logMessage (report);
        }
    }
};

static SilkTests silkTests;

} // namespace linen::test
