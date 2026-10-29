#include "TestSignals.h"

#include "dsp/SilkMacro.h"

namespace linen::test
{

namespace
{
    constexpr double sr = 48000.0;
    constexpr int block = 512;

    /** The "House Silk" preset (see Presets.h). */
    EngineParams houseSound()
    {
        EngineParams p;
        p.preampDrive = 0.3f;
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

    /** The "Clean" preset: every default neutral. */
    EngineParams cleanSound()
    {
        EngineParams p;
        p.comp.thresholdDb = -18.0f;   // the parameter's default; idle at 1:1
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

            // A hair either side of the centre is still exactly neutral (a detent).
            const auto nearly = SilkMacro::apply (user, 0.502f);
            expectEquals (nearly.preampDrive, user.preampDrive);
            expectEquals (nearly.outputGainDb, user.outputGainDb);
        }

        beginTest ("From Clean, Silk at or below 50% changes nothing at all");
        {
            const auto vocal = sungPhrase (48000 * 2);
            for (const float silk : { 0.0f, 0.3f, 0.5f })
            {
                auto p = cleanSound();
                p.silk = silk;
                Engine engine;
                engine.setParams (p);
                engine.prepare (sr, block, 2);
                const auto out = runInBlocks (engine, vocal, block);
                expect (identicalAfter (vocal, out, engine.getLatencySamples()), "silk " + juce::String (silk));
            }
        }

        beginTest ("Silk leaves switched-off sections alone");
        {
            auto user = houseSound();
            user.sections.preamp = user.sections.comp = user.sections.air = false;
            const auto p = SilkMacro::apply (user, 1.0f);
            expectEquals (p.preampDrive, user.preampDrive);
            expectEquals (p.comp.thresholdDb, user.comp.thresholdDb);
            expectEquals (p.comp.ratio, user.comp.ratio);
            expectEquals (p.air.amount, user.air.amount);
            expectEquals (p.outputGainDb, user.outputGainDb, "no trim for a compressor that is off");
            expectGreaterThan (p.deharsh.depth, user.deharsh.depth, "De-harsh is still on");
        }

        beginTest ("More silk means more of everything, in order; less pulls back toward transparent");
        {
            const auto user = houseSound();
            const auto up = SilkMacro::apply (user, 1.0f), down = SilkMacro::apply (user, 0.0f);
            expectGreaterThan (up.preampDrive, user.preampDrive);
            expectGreaterThan (up.deharsh.depth, user.deharsh.depth);
            expectLessThan (up.comp.thresholdDb, user.comp.thresholdDb);
            expectGreaterThan (up.comp.ratio, user.comp.ratio);
            expectGreaterThan (up.comp.cotton, user.comp.cotton);
            expectGreaterThan (up.air.amount, user.air.amount);
            expectLessThan (down.preampDrive, user.preampDrive);
            expectLessThan (down.deharsh.depth, user.deharsh.depth);

            // Drive arrives early: by 75% it is fully in, while Air has barely started.
            const auto threeQuarters = SilkMacro::apply (user, 0.75f);
            expectWithinAbsoluteError (threeQuarters.preampDrive, up.preampDrive, 0.001f);
            expectLessThan (threeQuarters.air.amount - user.air.amount, 0.25f * (up.air.amount - user.air.amount));
        }

        beginTest ("Turning Silk changes texture, not loudness (within 0.5 dB on a sung phrase, from House Silk and from Clean)");
        {
            const auto vocal = sungPhrase (48000 * 5);

            for (const auto& [label, base] : { std::pair { "house", houseSound() }, std::pair { "clean", cleanSound() } })
            {
                const auto reference = outputLevelDb (base, vocal);
                juce::String report = juce::String (label) + ":  ";
                for (const float silk : { 0.0f, 0.25f, 0.75f, 1.0f })
                {
                    auto p = base;
                    p.silk = silk;
                    const float drift = outputLevelDb (p, vocal) - reference;
                    report << "silk " << silk << ": " << juce::String (drift, 2) << " dB  ";
                    expectWithinAbsoluteError (drift, 0.0f, 0.5f, juce::String (label) + ", silk " + juce::String (silk));
                }
                logMessage (report);
            }
        }
    }
};

static SilkTests silkTests;

} // namespace linen::test
