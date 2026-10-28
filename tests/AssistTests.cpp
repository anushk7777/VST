#include "TestSignals.h"

#include "dsp/AutoAssist.h"
#include "dsp/Biquad.h"

namespace linen::test
{

namespace
{
    constexpr double sr = 48000.0;

    juce::AudioBuffer<float> voice (float gainDb = 0.0f)
    {
        auto b = sungPhrase (48000 * 6, sr, 1);
        b.applyGain (juce::Decibels::decibelsToGain (gainDb));
        return b;
    }

    void filter (juce::AudioBuffer<float>& b, std::initializer_list<Biquad::Coeffs> stages)
    {
        for (const auto& c : stages)
        {
            Biquad f;
            f.c = c;
            for (int i = 0; i < b.getNumSamples(); ++i)
                b.setSample (0, i, f.process (b.getSample (0, i)));
        }
    }

    AssistResult listen (const juce::AudioBuffer<float>& b)
    {
        return AutoAssist::analyse (b.getReadPointer (0), b.getNumSamples(), sr);
    }

    juce::String describe (const AssistResult& r)
    {
        return "level " + juce::String (r.levelDb, 1) + " dB, dyn " + juce::String (r.dynamicsDb, 1) + ", pitch " + juce::String (r.pitchHz, 0)
             + " Hz, mud " + juce::String (r.mudDb, 1) + ", boom " + juce::String (r.boomDb, 1) + ", harsh " + juce::String (r.harshDb, 1)
             + ", res " + juce::String (r.resonanceDb, 1) + ", dull " + juce::String (r.dullDb, 1) + ", sib " + juce::String (r.sibilanceDb, 1)
             + " -> depth " + juce::String (r.deharshDepth, 1) + ", low " + juce::String (r.lowGainDb, 1) + ", 12k " + juce::String (r.highGainDb, 1)
             + ", air " + juce::String (r.airAmount, 2) + ", de-ess " + juce::String (r.airDeEss, 2) + ", in " + juce::String (r.inputGainDb, 1)
             + ", thr " + juce::String (r.compThresholdDb, 1) + ", hpf " + juce::String (r.highPass);
    }
}

class AssistTests final : public juce::UnitTest
{
public:
    AssistTests() : juce::UnitTest ("AI Listen", "Linen") {}

    void runTest() override
    {
        const auto base = listen (voice());
        logMessage ("base:     " + describe (base));

        beginTest ("A normal vocal gives a valid, in-range suggestion and a summary");
        {
            expect (base.valid);
            expect (base.summary.isNotEmpty());
            expectWithinAbsoluteError (base.pitchHz, 235.0f, 40.0f, "median of the sung notes");
            expectEquals (base.highPass, 2, "80 Hz for a voice singing around 200-300 Hz");
            expect (base.deharshDepth >= 3.0f && base.deharshDepth <= 10.0f);
            expect (base.compThresholdDb >= -36.0f && base.compThresholdDb <= -6.0f);
            expect (base.airDeEss >= 0.2f && base.airDeEss <= 0.9f);
        }

        beginTest ("A harsh vocal gets more De-harsh");
        {
            auto b = voice();
            filter (b, { Biquad::Coeffs::peak (sr, 3500.0, 2.0, 8.0) });
            const auto r = listen (b);
            logMessage ("harsh:    " + describe (r));
            expectGreaterThan (r.deharshDepth, base.deharshDepth + 1.5f);
            expectLessThan (r.midGainDb, 0.0f, "and a gentle 3.2k dip");
        }

        beginTest ("A dull vocal gets more top and more air");
        {
            auto b = voice();
            filter (b, { Biquad::Coeffs::lowPass (sr, 4000.0, 0.707), Biquad::Coeffs::lowPass (sr, 4000.0, 0.707) });
            const auto r = listen (b);
            logMessage ("dull:     " + describe (r));
            expectGreaterThan (r.highGainDb, base.highGainDb + 1.0f);
            expectGreaterThan (r.airAmount, base.airAmount);
        }

        beginTest ("A muddy vocal gets a low-mid cut");
        {
            auto b = voice();
            filter (b, { Biquad::Coeffs::peak (sr, 300.0, 1.0, 6.0) });
            const auto r = listen (b);
            logMessage ("muddy:    " + describe (r));
            expectLessThan (r.lowGainDb, 0.0f);
            expectEquals (r.lowFreq, 4, "at 220 Hz");
        }

        beginTest ("A quiet recording gets brought up to level");
        {
            const auto r = listen (voice (-8.0f));
            logMessage ("quiet:    " + describe (r));
            expectWithinAbsoluteError (r.inputGainDb, base.inputGainDb + 8.0f, 1.5f);
            expectWithinAbsoluteError (r.compThresholdDb, base.compThresholdDb, 1.5f, "threshold follows the corrected level");
        }

        beginTest ("A sibilant vocal gets more de-essing");
        {
            auto b = voice();
            auto hiss = whiteNoise (1, b.getNumSamples(), 0.6f, 9);
            filter (hiss, { Biquad::Coeffs::highPass (sr, 5000.0, 0.707), Biquad::Coeffs::lowPass (sr, 10000.0, 0.707) });
            for (int i = 0; i < b.getNumSamples(); ++i)
                if (std::fmod (i / sr, 0.5) > 0.38 && std::fmod (i / sr, 0.5) < 0.45)   // an 'S' at the end of each note
                    b.addSample (0, i, hiss.getSample (0, i));
            const auto r = listen (b);
            logMessage ("sibilant: " + describe (r));
            expectGreaterThan (r.airDeEss, base.airDeEss + 0.15f);
        }

        beginTest ("Silence, or too little singing, gives no suggestion");
        {
            juce::AudioBuffer<float> silence (1, 48000 * 6);
            silence.clear();
            expect (! listen (silence).valid);

            auto shortClip = sungPhrase (48000, sr, 1);
            expect (! listen (shortClip).valid);
        }
    }
};

static AssistTests assistTests;

} // namespace linen::test
