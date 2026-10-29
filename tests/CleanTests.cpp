#include "TestSignals.h"

namespace linen::test
{

namespace
{
    constexpr double sr = 48000.0;
    constexpr int block = 512;

    /** What a fresh Linen runs with: every parameter at its default (the Clean preset). */
    EngineParams clean()
    {
        EngineParams p;
        p.comp.thresholdDb = -18.0f;   // the parameter's default; idle at 1:1
        return p;
    }

    /** Every knob somewhere loud, to prove the power switches really switch. */
    EngineParams everythingCranked()
    {
        EngineParams p;
        p.tone = { 250.0f, -12.0f, 3000.0f, 12.0f };
        p.preampDrive = 1.0f;
        p.eq.lowFreq = 4;
        p.eq.lowGainDb = 16.0f;
        p.eq.midGainDb = -18.0f;
        p.eq.highGainDb = 16.0f;
        p.deharsh.depth = 15.0f;
        p.deharsh.delta = true;
        p.comp.thresholdDb = -48.0f;
        p.comp.ratio = 6.0f;
        p.comp.cotton = 1.0f;
        p.air.amount = 1.0f;
        p.air.deEss = 1.0f;
        p.silk = 1.0f;
        return p;
    }

    /** Noise, a sung phrase and a full-scale sweep, back to back. */
    juce::AudioBuffer<float> programme (double sampleRate, int channels)
    {
        const int n1 = (int) (0.5 * sampleRate), n2 = (int) (1.5 * sampleRate), n3 = (int) sampleRate;
        juce::AudioBuffer<float> out (channels, n1 + n2 + n3);
        const auto noise = whiteNoise (channels, n1, 0.5f, 3);
        const auto voice = sungPhrase (n2, sampleRate, channels);
        const auto sweep = fullScaleSweep (n3, sampleRate, channels);
        for (int ch = 0; ch < channels; ++ch)
        {
            out.copyFrom (ch, 0, noise, ch, 0, n1);
            out.copyFrom (ch, n1, voice, ch, 0, n2);
            out.copyFrom (ch, n1 + n2, sweep, ch, 0, n3);
        }
        return out;
    }

    juce::AudioBuffer<float> render (const EngineParams& p, const juce::AudioBuffer<float>& in, double sampleRate, int& latency)
    {
        Engine engine;
        engine.setParams (p);
        engine.prepare (sampleRate, block, in.getNumChannels());
        latency = engine.getLatencySamples();
        return runInBlocks (engine, in, block);
    }

    /** in, delayed by some samples (zeros in front, same length). */
    juce::AudioBuffer<float> delayed (const juce::AudioBuffer<float>& in, int samples)
    {
        juce::AudioBuffer<float> out (in.getNumChannels(), in.getNumSamples());
        out.clear();
        for (int ch = 0; ch < in.getNumChannels(); ++ch)
            out.copyFrom (ch, samples, in, ch, 0, in.getNumSamples() - samples);
        return out;
    }

    float biggestStep (const juce::AudioBuffer<float>& audio, int start, int end)
    {
        float biggest = 0.0f;
        for (int i = juce::jmax (1, start); i < end; ++i)
            biggest = juce::jmax (biggest, std::abs (audio.getSample (0, i) - audio.getSample (0, i - 1)));
        return biggest;
    }

    /** Each module's latency at 48 kHz, as the engine sums them. */
    struct Latencies
    {
        Latencies()
        {
            Preamp p;
            p.prepare (sr, block, 2);
            PainEngine pe;
            pe.prepare (sr, block, 2);
            ResonanceSuppressor d;
            d.prepare (sr, 2);
            SilkCompressor c;
            c.prepare (sr, block, 2);
            preamp = p.getLatencySamples();
            pain = pe.getLatencySamples();
            deharsh = d.getLatencySamples();
            comp = c.getLatencySamples();
        }

        int total() const { return preamp + pain + deharsh + comp; }

        int preamp = 0, pain = 0, deharsh = 0, comp = 0;
    };
}

class CleanTests final : public juce::UnitTest
{
public:
    CleanTests() : juce::UnitTest ("Clean and isolation", "Linen") {}

    void runTest() override
    {
        beginTest ("Clean is bit-exact: noise, a sung phrase and a full-scale sweep, at 44.1/48/96 kHz, mono and stereo");
        {
            for (const double rate : { 44100.0, 48000.0, 96000.0 })
                for (const int channels : { 1, 2 })
                {
                    const auto in = programme (rate, channels);
                    int latency = 0;
                    const auto out = render (clean(), in, rate, latency);
                    expect (identicalAfter (in, out, latency),
                            juce::String (rate / 1000.0, 1) + " kHz, " + juce::String (channels) + " channel(s)");
                }
        }

        beginTest ("Every section switched off is bit-exact, whatever its knobs say");
        {
            auto p = everythingCranked();
            p.sections = { false, false, false, false, false };
            const auto in = programme (sr, 2);
            int latency = 0;
            const auto out = render (p, in, sr, latency);
            expect (identicalAfter (in, out, latency));
        }

        // From Clean, turning up one section's knob must change the sound by
        // exactly what that module does on its own: everything else stays a
        // pure delay. Each module is run alone on the input it sees inside the
        // chain (delayed by the sections before it), in the same blocks.
        const Latencies lat;
        const auto vocal = sungPhrase (72000, sr, 2);

        const auto isolated = [this, &vocal] (const EngineParams& p, int upstream, int downstream, auto&& runAlone, const juce::String& knob)
        {
            int latency = 0;
            const auto engineOut = render (p, vocal, sr, latency);
            auto alone = delayed (vocal, upstream);
            runAlone (alone);
            expect (! identicalAfter (vocal, engineOut, latency), knob + " should change the sound");
            expect (identicalAfter (alone, engineOut, downstream), knob + " should be the only thing that does");
        };

        beginTest ("Each knob works alone from Clean: only its own section changes the sound");
        {
            {
                auto p = clean();
                p.tone.lowGainDb = -6.0f;
                p.tone.highGainDb = 4.0f;
                isolated (p, 0, lat.total(), [&p] (juce::AudioBuffer<float>& audio)
                {
                    ToneShelves tone;
                    tone.setParams (p.tone);
                    tone.prepare (sr, audio.getNumChannels());
                    forEachIrregularBlock (audio, block, [&tone] (juce::AudioBuffer<float>& b) { tone.process (b); });
                }, "Tone");
            }
            {
                auto p = clean();
                p.preampDrive = 0.5f;
                isolated (p, 0, lat.total() - lat.preamp, [&p] (juce::AudioBuffer<float>& audio)
                {
                    Preamp preamp;
                    preamp.setDrive (p.preampDrive);
                    preamp.prepare (sr, block, audio.getNumChannels());
                    forEachIrregularBlock (audio, block, [&preamp] (juce::AudioBuffer<float>& b) { preamp.process (b); });
                }, "Drive");
            }
            {
                auto p = clean();
                p.deharsh.depth = 8.0f;
                isolated (p, lat.preamp + lat.pain, lat.comp, [&p] (juce::AudioBuffer<float>& audio)
                {
                    ResonanceSuppressor deharsh;
                    deharsh.setParams (p.deharsh);
                    deharsh.prepare (sr, audio.getNumChannels());
                    std::vector<float> detector ((size_t) block);
                    forEachIrregularBlock (audio, block, [&] (juce::AudioBuffer<float>& b)
                    {
                        const float norm = 1.0f / (float) b.getNumChannels();
                        std::fill (detector.begin(), detector.end(), 0.0f);
                        for (int ch = 0; ch < b.getNumChannels(); ++ch)
                            for (int i = 0; i < b.getNumSamples(); ++i)
                                detector[(size_t) i] += b.getSample (ch, i) * norm;
                        deharsh.process (b, detector.data(), true);
                    });
                }, "De-harsh");
            }
            {
                auto p = clean();
                p.comp.ratio = 3.0f;
                p.comp.cotton = 0.6f;
                isolated (p, lat.preamp + lat.pain + lat.deharsh, 0, [&p] (juce::AudioBuffer<float>& audio)
                {
                    SilkCompressor comp;
                    comp.setParams (p.comp);
                    comp.prepare (sr, block, audio.getNumChannels());
                    forEachIrregularBlock (audio, block, [&comp] (juce::AudioBuffer<float>& b) { comp.process (b); });
                }, "Ratio and Cotton");
            }
            {
                auto p = clean();
                p.air.amount = 0.6f;
                p.air.deEss = 0.5f;
                isolated (p, lat.total(), 0, [&p] (juce::AudioBuffer<float>& audio)
                {
                    Air air;
                    air.setParams (p.air);
                    air.prepare (sr, block, audio.getNumChannels());
                    forEachIrregularBlock (audio, block, [&air] (juce::AudioBuffer<float>& b) { air.process (b); });
                }, "Air");
            }
        }

        beginTest ("Pain alone from Clean: the Pain engine plus its own small preamp push, nothing else");
        {
            auto p = clean();
            p.pain = 0.35f;
            isolated (p, 0, lat.deharsh + lat.comp, [&p] (juce::AudioBuffer<float>& audio)
            {
                Preamp preamp;
                preamp.setDrive (0.25f * p.pain);
                preamp.prepare (sr, block, audio.getNumChannels());
                PainEngine pain;
                pain.setPain (p.pain);
                pain.prepare (sr, block, audio.getNumChannels());
                forEachIrregularBlock (audio, block, [&] (juce::AudioBuffer<float>& b)
                {
                    preamp.process (b);
                    pain.process (b);
                });
            }, "Pain");
        }

        beginTest ("Switching a section out is click-free, and exact once the 20 ms fade is done");
        {
            auto p = clean();
            p.tone.lowHz = 300.0f;
            p.tone.lowGainDb = 12.0f;

            Engine engine;
            engine.setParams (p);
            engine.prepare (sr, block, 1);
            const int latency = engine.getLatencySamples();

            const auto in = sineWave (100.0, -12.0f, 48000);
            auto out = in;
            for (int pos = 0; pos < out.getNumSamples(); pos += block)
            {
                if (pos == 24064)   // about half way: switch Tone out
                {
                    p.sections.tone = false;
                    engine.setParams (p);
                }
                juce::AudioBuffer<float> b (out.getArrayOfWritePointers(), 1, pos, juce::jmin (block, out.getNumSamples() - pos));
                engine.process (b);
            }

            // A sine's own steepest step at its loudest (the lifted level), for comparison.
            const float loudest = out.getMagnitude (0, latency, 20000);
            const float ownStep = loudest * 2.0f * std::sin (juce::MathConstants<float>::pi * 100.0f / (float) sr);
            const float step = biggestStep (out, 24064 - 2000, 24064 + 4000);
            logMessage ("biggest step while switching " + juce::String (step, 5) + ", the sine's own " + juce::String (ownStep, 5));
            expectLessThan (step, ownStep * 1.05f);

            juce::AudioBuffer<float> inTail (1, 12000), outTail (1, 12000);
            inTail.copyFrom (0, 0, in, 0, 36000 - latency, 12000);
            outTail.copyFrom (0, 0, out, 0, 36000, 12000);
            expect (identicalAfter (inTail, outTail, 0), "exact after the fade");
        }

        beginTest ("Null plays digital silence on Clean, and exactly what Linen changes otherwise");
        {
            const auto in = programme (sr, 2);
            int latency = 0;

            auto p = clean();
            p.nullTest = true;
            const auto silent = render (p, in, sr, latency);
            expectEquals (silent.getMagnitude (0, silent.getNumSamples()), 0.0f, "every sample exactly 0");

            auto house = clean();
            house.preampDrive = 0.3f;
            house.deharsh.depth = 5.0f;
            house.comp.ratio = 2.0f;
            house.comp.cotton = 0.5f;
            house.air.amount = 0.3f;
            house.air.deEss = 0.5f;
            const auto normal = render (house, in, sr, latency);
            house.nullTest = true;
            const auto difference = render (house, in, sr, latency);

            bool exact = true;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = latency; i < in.getNumSamples(); ++i)
                    exact = exact && juce::exactlyEqual (difference.getSample (ch, i), normal.getSample (ch, i) - in.getSample (ch, i - latency));
            expect (exact, "null = processed minus the aligned input, sample for sample");
        }
    }
};

static CleanTests cleanTests;

} // namespace linen::test
