#include "TestSignals.h"

#include "dsp/ToneShelves.h"

namespace linen::test
{

namespace
{
    constexpr double sr = 48000.0;
    constexpr int block = 512;

    juce::AudioBuffer<float> runTone (const ToneParams& p, const juce::AudioBuffer<float>& in)
    {
        ToneShelves tone;
        tone.setParams (p);
        tone.prepare (sr, in.getNumChannels());
        auto out = in;
        forEachIrregularBlock (out, block, [&tone] (juce::AudioBuffer<float>& b) { tone.process (b); });
        return out;
    }

    /** Runs a tone through, switching to `after` at sample `at` (a block boundary). */
    juce::AudioBuffer<float> runToneWithChange (const ToneParams& before, const ToneParams& after, int at, const juce::AudioBuffer<float>& in)
    {
        ToneShelves tone;
        tone.setParams (before);
        tone.prepare (sr, in.getNumChannels());
        auto out = in;
        for (int pos = 0; pos < out.getNumSamples(); pos += block)
        {
            if (pos == at)
                tone.setParams (after);
            juce::AudioBuffer<float> b (out.getArrayOfWritePointers(), out.getNumChannels(), pos, juce::jmin (block, out.getNumSamples() - pos));
            tone.process (b);
        }
        return out;
    }

    float biggestStep (const juce::AudioBuffer<float>& audio, int start, int end)
    {
        float biggest = 0.0f;
        for (int i = juce::jmax (1, start); i < end; ++i)
            biggest = juce::jmax (biggest, std::abs (audio.getSample (0, i) - audio.getSample (0, i - 1)));
        return biggest;
    }
}

class ToneTests final : public juce::UnitTest
{
public:
    ToneTests() : juce::UnitTest ("Tone shelves", "Linen") {}

    void runTest() override
    {
        beginTest ("Flat is bit-exact, wherever the frequencies sit");
        {
            const auto in = whiteNoise (2, 24000);
            expect (identicalAfter (in, runTone ({}, in), 0));
            expect (identicalAfter (in, runTone ({ 40.0f, 0.0f, 12000.0f, 0.0f }, in), 0));
        }

        beginTest ("Half the gain at the corner, all of it three octaves out, and never past it (no overshoot)");
        {
            for (const float hz : { 60.0f, 250.0f, 1000.0f })
                for (const float gain : { -12.0f, -2.0f, 6.0f })
                {
                    ToneParams p;
                    p.lowHz = hz;
                    p.lowGainDb = gain;
                    const auto label = "low shelf " + juce::String (hz) + " Hz, " + juce::String (gain) + " dB";
                    expectWithinAbsoluteError (ToneShelves::magnitudeDb (p, sr, hz), (double) gain * 0.5, 0.05, label + " at the corner");
                    expectWithinAbsoluteError (ToneShelves::magnitudeDb (p, sr, hz / 8.0), (double) gain, 0.05, label + " 3 octaves below");
                    expectWithinAbsoluteError (ToneShelves::magnitudeDb (p, sr, hz * 8.0), 0.0, 0.05, label + " 3 octaves above");
                }

            for (const float hz : { 1500.0f, 3000.0f, 6000.0f })
                for (const float gain : { -6.0f, 1.2f, 12.0f })
                {
                    ToneParams p;
                    p.highHz = hz;
                    p.highGainDb = gain;
                    const auto label = "high shelf " + juce::String (hz) + " Hz, " + juce::String (gain) + " dB";
                    expectWithinAbsoluteError (ToneShelves::magnitudeDb (p, sr, hz), (double) gain * 0.5, 0.05, label + " at the corner");
                    expectWithinAbsoluteError (ToneShelves::magnitudeDb (p, sr, juce::jmin (hz * 8.0f, 22000.0f)), (double) gain, 0.1, label + " far above");
                    expectWithinAbsoluteError (ToneShelves::magnitudeDb (p, sr, hz / 8.0), 0.0, 0.05, label + " 3 octaves below");
                }

            // Sweep the whole band: the response always stays between 0 and the gain.
            ToneParams both;
            both.lowHz = 450.0f;
            both.lowGainDb = -2.0f;
            both.highHz = 3000.0f;
            both.highGainDb = 1.2f;
            double lowest = 0.0, highest = 0.0;
            for (double hz = 20.0; hz < 20000.0; hz *= 1.01)
            {
                const double db = ToneShelves::magnitudeDb (both, sr, hz);
                lowest = std::min (lowest, db);
                highest = std::max (highest, db);
            }
            expectGreaterOrEqual (lowest, -2.0 - 0.01, "never cuts past the low shelf's -2 dB");
            expectLessOrEqual (highest, 1.2 + 0.01, "never lifts past the high shelf's +1.2 dB");
        }

        beginTest ("The processed audio matches the designed response");
        {
            ToneParams p;
            p.lowHz = 450.0f;
            p.lowGainDb = -6.0f;
            p.highHz = 3000.0f;
            p.highGainDb = 4.0f;

            for (const double hz : { 60.0, 450.0, 1200.0, 3000.0, 12000.0 })
            {
                const auto out = runTone (p, sineWave (hz, -20.0f, 65536));
                expectWithinAbsoluteError (toneDb (out, hz) + 20.0, ToneShelves::magnitudeDb (p, sr, hz), 0.05, juce::String (hz) + " Hz");
            }
        }

        beginTest ("Moving a gain or a frequency glides: no zipper or click");
        {
            const auto in = sineWave (100.0, -12.0f, 48000);
            const float amp = juce::Decibels::decibelsToGain (-12.0f);

            // Gain: 0 to +12 dB at once, on a 100 Hz note under a 300 Hz shelf.
            ToneParams flat, lifted;
            flat.lowHz = lifted.lowHz = 300.0f;
            lifted.lowGainDb = 12.0f;
            const auto gainMove = runToneWithChange (flat, lifted, 24064, in);
            const float loud = amp * juce::Decibels::decibelsToGain ((float) ToneShelves::magnitudeDb (lifted, sr, 100.0));
            const float ownStep = loud * 2.0f * std::sin (juce::MathConstants<float>::pi * 100.0f / (float) sr);
            const float gainStep = biggestStep (gainMove, 23000, 27000);
            logMessage ("gain move: biggest step " + juce::String (gainStep, 5) + " (the louder sine's own: " + juce::String (ownStep, 5) + ")");
            expectLessThan (gainStep, ownStep * 1.1f);

            // Frequency: the shelf jumps from 60 Hz to 600 Hz with -12 dB of cut.
            ToneParams narrow, wide;
            narrow.lowHz = 60.0f;
            wide.lowHz = 600.0f;
            narrow.lowGainDb = wide.lowGainDb = -12.0f;
            const auto freqMove = runToneWithChange (narrow, wide, 24064, in);
            const float quietStep = amp * 2.0f * std::sin (juce::MathConstants<float>::pi * 100.0f / (float) sr);
            const float freqStep = biggestStep (freqMove, 23000, 27000);
            logMessage ("frequency move: biggest step " + juce::String (freqStep, 5) + " (the sine's own at full level: " + juce::String (quietStep, 5) + ")");
            expectLessThan (freqStep, quietStep * 1.1f);
        }

        beginTest ("Back to flat, it is bit-exact again once the shelf has settled");
        {
            ToneParams cut, flat;
            cut.lowGainDb = -6.0f;
            const auto in = whiteNoise (1, 48000 * 2, 0.3f, 5);
            const auto out = runToneWithChange (cut, flat, 9728, in);

            juce::AudioBuffer<float> inTail (1, 48000), outTail (1, 48000);
            inTail.copyFrom (0, 0, in, 0, 48000, 48000);
            outTail.copyFrom (0, 0, out, 0, 48000, 48000);
            expect (identicalAfter (inTail, outTail, 0));
        }
    }
};

static ToneTests toneTests;

} // namespace linen::test
