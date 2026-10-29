#include <tuple>

#include "TestSignals.h"

namespace linen::test
{

namespace
{
    constexpr double sr = 48000.0;
    constexpr int block = 512;

    void runBlocks (AdcCompressor& comp, juce::AudioBuffer<float>& audio, int maxBlock = block)
    {
        forEachIrregularBlock (audio, maxBlock, [&comp] (juce::AudioBuffer<float>& b) { comp.process (b); });
    }

    CompParams tube (float thresholdDb, float ratio)
    {
        CompParams p;
        p.thresholdDb = thresholdDb;
        p.ratio = ratio;
        p.autoMakeup = false;
        return p;
    }

    CompParams opto (float reduction)
    {
        CompParams p;
        p.optoReduction = reduction;
        p.autoMakeup = false;
        return p;
    }

    /** House Silk's ADC (see Presets.h). */
    CompParams house()
    {
        CompParams p;
        p.thresholdDb = -18.0f;
        p.ratio = 2.0f;
        p.optoReduction = 0.35f;
        p.warmth = 0.5f;
        return p;
    }

    /** Steady gain reduction (dB) on a steady sine whose peak is at levelDb, read from its fundamental. */
    float steadyReductionDb (const CompParams& p, float levelDb, double hz = 1000.0)
    {
        AdcCompressor comp;
        comp.setParams (p);
        comp.prepare (sr, block, 1);
        auto tone = sineWave (hz, levelDb, 96000);
        runBlocks (comp, tone);
        return levelDb - (float) toneDb (tone, hz, 96000 - 32768, 32768);
    }

    /** A quiet tone, a loud stretch of it from 'from' for 'length' samples, then quiet again. */
    juce::AudioBuffer<float> burst (float quietDb, float loudDb, int from, int length, int total, double hz = 1000.0)
    {
        auto audio = sineWave (hz, quietDb, total);
        const float lift = juce::Decibels::decibelsToGain (loudDb - quietDb);
        audio.applyGain (from, length, lift);
        return audio;
    }

    /** Reads a meter every millisecond while the audio plays. */
    template <typename Meter>
    std::vector<float> meterTrace (AdcCompressor& comp, juce::AudioBuffer<float>& audio, Meter&& meter)
    {
        constexpr int hop = 48;
        std::vector<float> trace;
        for (int pos = 0; pos < audio.getNumSamples(); pos += hop)
        {
            juce::AudioBuffer<float> b (audio.getArrayOfWritePointers(), audio.getNumChannels(), pos,
                                        juce::jmin (hop, audio.getNumSamples() - pos));
            comp.process (b);
            trace.push_back (meter (comp));
        }
        return trace;
    }

    /** Seconds from index 'from' of a millisecond trace until it first drops under 'level' (-1 if never). */
    double secondsUntilBelow (const std::vector<float>& trace, size_t from, float level)
    {
        for (size_t i = from; i < trace.size(); ++i)
            if (trace[i] < level)
                return (double) (i - from + 1) / 1000.0;
        return -1.0;
    }

    /** The local ratio (input dB per output dB) between successive points of a static curve. */
    std::vector<float> localRatios (const std::vector<float>& levels, const std::vector<float>& reductions)
    {
        std::vector<float> ratios;
        for (size_t i = 1; i < levels.size(); ++i)
        {
            const float dIn = levels[i] - levels[i - 1];
            const float dOut = dIn - (reductions[i] - reductions[i - 1]);
            ratios.push_back (dIn / dOut);
        }
        return ratios;
    }

    juce::String list (const std::vector<float>& values, int decimals = 2)
    {
        juce::StringArray s;
        for (auto v : values)
            s.add (juce::String (v, decimals));
        return s.joinIntoString (" ");
    }
}

class AdcTests final : public juce::UnitTest
{
public:
    AdcTests() : juce::UnitTest ("ADC compressor", "Linen") {}

    void runTest() override
    {
        beginTest ("Idle is bit-exact: 1:1, Peak Reduction 0, Warmth 0, any blend; a switched-off slot is out");
        {
            const auto check = [this] (const CompParams& p, const juce::String& what)
            {
                AdcCompressor comp;
                comp.setParams (p);
                comp.prepare (sr, block, 2);
                const auto in = whiteNoise (2, 48000, 0.9f);
                auto out = in;
                runBlocks (comp, out);
                expect (identicalAfter (in, out, comp.getLatencySamples()), what);
            };

            check (CompParams {}, "defaults");

            CompParams blend;
            blend.mix = 0.5f;
            check (blend, "50% blend");

            auto tubeOut = tube (-48.0f, 6.0f);
            tubeOut.tubeOn = false;
            tubeOut.autoMakeup = true;
            check (tubeOut, "TUBE switched off at its firmest");

            auto optoOut = opto (1.0f);
            optoOut.optoOn = false;
            optoOut.autoMakeup = true;
            check (optoOut, "OPTO switched off at 100%");

            auto dryOnly = house();
            dryOnly.ratio = 6.0f;
            dryOnly.warmth = 1.0f;
            dryOnly.mix = 0.0f;
            check (dryOnly, "Blend 0% is the dry voice");
        }

        beginTest ("A TUBE waiting under its knee changes nothing: the quiet line passes bit for bit");
        {
            AdcCompressor comp;
            comp.setParams (tube (-6.0f, 3.0f));
            comp.prepare (sr, block, 2);
            auto in = sungPhrase (96000);
            in.applyGain (juce::Decibels::decibelsToGain (-14.0f));   // peaks near -20 dBFS
            auto out = in;
            runBlocks (comp, out);
            expect (identicalAfter (in, out, comp.getLatencySamples()));
        }

        beginTest ("The Ratio knob reads what the TUBE does");
        {
            expectEquals (AdcCompressor::tubeRatioFor (1.0f), 1.0f);
            expectLessThan (AdcCompressor::tubeRatioFor (6.0f), 2.5f);
            for (const float knob : { 1.5f, 2.0f, 3.0f, 4.5f, 6.0f })
                expectWithinAbsoluteError (AdcCompressor::ratioKnobFor (AdcCompressor::tubeRatioFor (knob)), knob, 0.01f);
        }

        beginTest ("TUBE: a 12 dB knee, a ratio that firms up gently and never passes about 3:1, as its curve says");
        {
            const auto p = tube (-24.0f, 6.0f);   // the firmest it goes
            std::vector<float> levels, measured;
            for (int level = -40; level <= 0; level += 2)
            {
                const float gr = steadyReductionDb (p, (float) level);
                expectWithinAbsoluteError (gr, AdcCompressor::staticReductionDb (p, (float) level), 0.75f,
                                           "curve at " + juce::String (level) + " dB");
                levels.push_back ((float) level);
                measured.push_back (gr);
            }
            const auto ratios = localRatios (levels, measured);
            logMessage ("GR -40..0 dB: " + list (measured, 1));
            logMessage ("local ratios: " + list (ratios));
            for (auto r : ratios)
                expectLessThan (r, 3.1f);
        }

        beginTest ("TUBE auto release: a single peak lets go quickly, a long loud line slowly");
        {
            const auto recovery = [] (int loudSamples)
            {
                AdcCompressor comp;
                comp.setParams (tube (-30.0f, 3.0f));
                comp.prepare (sr, block, 1);
                const int from = 4800, total = from + loudSamples + 48000 * 8;
                auto audio = burst (-50.0f, -6.0f, from, loudSamples, total);
                const auto trace = meterTrace (comp, audio, [] (const AdcCompressor& c) { return c.getTubeReductionDb(); });
                const size_t end = (size_t) ((from + loudSamples) / 48);
                return std::pair { trace[end - 1], secondsUntilBelow (trace, end, 1.0f) };
            };

            const auto [peakGr, afterPeak] = recovery (2400);       // 50 ms
            const auto [lineGr, afterLine] = recovery (48000 * 3);  // 3 s
            logMessage ("single peak (" + juce::String (peakGr, 1) + " dB): under 1 dB after " + juce::String (afterPeak, 3)
                        + " s; long line (" + juce::String (lineGr, 1) + " dB): " + juce::String (afterLine, 3) + " s");
            expectGreaterThan (afterPeak, 0.0);
            expectLessThan (afterPeak, 0.4, "a single peak recovers quickly");
            expectGreaterThan (afterLine, 1.0, "a long line recovers slowly");
        }

        beginTest ("OPTO: about 10 ms attack; half lets go in 60-100 ms, the rest slowly, slower after a long hold");
        {
            struct Timing { double attackHalf, attack90, half, full; float peak; };
            const auto timing = [] (int holdSamples)
            {
                AdcCompressor comp;
                comp.setParams (opto (0.6f));
                comp.prepare (sr, block, 1);
                const int from = 24000, total = from + holdSamples + 48000 * 10;
                auto audio = burst (-60.0f, -17.0f, from, holdSamples, total);
                const auto trace = meterTrace (comp, audio, [] (const AdcCompressor& c) { return c.getOptoReductionDb(); });
                const size_t on = (size_t) (from / 48), off = (size_t) ((from + holdSamples) / 48);
                const float peak = trace[off - 1];
                Timing t {};
                t.peak = peak;
                for (size_t i = on; i < off; ++i)
                {
                    if (t.attackHalf <= 0.0 && trace[i] > 0.5f * peak) t.attackHalf = (double) (i - on + 1) / 1000.0;
                    if (t.attack90 <= 0.0 && trace[i] > 0.9f * peak) t.attack90 = (double) (i - on + 1) / 1000.0;
                }
                t.half = secondsUntilBelow (trace, off, 0.5f * peak);
                t.full = secondsUntilBelow (trace, off, 0.5f);
                return t;
            };

            const auto shortHold = timing (9600);      // 0.2 s
            const auto longHold = timing (48000 * 4);  // 4 s
            for (const auto& [label, t] : { std::pair { "0.2 s hold", shortHold }, std::pair { "4 s hold", longHold } })
                logMessage (juce::String (label) + " (" + juce::String (t.peak, 1) + " dB): attack to half " + juce::String (t.attackHalf * 1000.0, 1)
                            + " ms, to 90% " + juce::String (t.attack90 * 1000.0, 1) + " ms; release to half "
                            + juce::String (t.half * 1000.0, 0) + " ms, under 0.5 dB " + juce::String (t.full, 2) + " s");

            expectWithinAbsoluteError (longHold.peak, 6.0f, 1.0f, "about 6 dB of reduction");
            expectGreaterThan (longHold.attackHalf, 0.003);
            expectLessThan (longHold.attackHalf, 0.02);
            expectLessThan (longHold.attack90, 0.06);
            expectGreaterThan (shortHold.half, 0.04);
            expectLessThan (shortHold.half, 0.11);
            expectLessThan (longHold.half, 0.15);
            expectGreaterThan (longHold.full, 1.0, "the rest lets go over seconds after a long hold...");
            expectGreaterThan (longHold.full, 3.0 * shortHold.full, "...and much sooner after a short one (memory)");
        }

        beginTest ("OPTO: a natural soft knee, never past 3:1, as its curve says");
        {
            const auto p = opto (1.0f);   // the most it does
            std::vector<float> levels, measured;
            for (int level = -60; level <= 0; level += 3)
            {
                const float gr = steadyReductionDb (p, (float) level);
                expectWithinAbsoluteError (gr, AdcCompressor::staticReductionDb (p, (float) level), 0.75f,
                                           "curve at " + juce::String (level) + " dB");
                levels.push_back ((float) level);
                measured.push_back (gr);
            }
            const auto ratios = localRatios (levels, measured);
            logMessage ("GR -60..0 dB: " + list (measured, 1));
            logMessage ("local ratios: " + list (ratios));
            for (auto r : ratios)
                expectLessThan (r, 3.05f);
        }

        beginTest ("One loop: the two cells share the work instead of stacking, and the pair stays soft");
        {
            for (const auto& [label, both] : { std::pair { "house", house() }, std::pair { "firmest", [] { auto p = tube (-48.0f, 6.0f); p.optoReduction = 1.0f; return p; }() } })
            {
                auto tubeAlone = both, optoAlone = both;
                tubeAlone.optoOn = false;
                optoAlone.tubeOn = false;

                std::vector<float> levels, measured;
                juce::String report;
                for (int level = -36; level <= 0; level += 3)
                {
                    const float together = AdcCompressor::staticReductionDb (both, (float) level);
                    const float stacked = AdcCompressor::staticReductionDb (tubeAlone, (float) level)
                                        + AdcCompressor::staticReductionDb (optoAlone, (float) level);
                    expectLessThan (together, stacked + 0.01f, juce::String (label) + " at " + juce::String (level));
                    levels.push_back ((float) level);
                    measured.push_back (together);
                    report << level << ": " << juce::String (together, 1) << " (" << juce::String (stacked, 1) << ")  ";
                }
                logMessage (juce::String (label) + " GR together (stacked): " + report);
                const auto ratios = localRatios (levels, measured);
                logMessage (juce::String (label) + " local ratios: " + list (ratios));
                for (auto r : ratios)
                    expectLessThan (r, juce::String (label) == "house" ? 3.0f : 5.0f, label);
            }
        }

        beginTest ("A phrase's onset is not over-compressed, and a held note does not pump, even at the fastest, firmest settings");
        {
            auto fastest = tube (-36.0f, 6.0f);
            fastest.attackMs = 1.0f;
            fastest.optoReduction = 0.8f;

            // The firmest pair hands over: the TUBE catches the onset, then lets
            // the slower OPTO take the line, so their sum peaks a little on the way.
            for (const auto& [label, p, allowed] : { std::tuple { "house", house(), 0.25f }, std::tuple { "fastest, firmest", fastest, 1.5f } })
                for (const double hz : { 1000.0, 110.0 })
                {
                    AdcCompressor comp;
                    comp.setParams (p);
                    comp.prepare (sr, block, 1);
                    auto audio = burst (-60.0f, -6.0f, 4800, 48000 * 3, 48000 * 3 + 4800, hz);
                    const auto trace = meterTrace (comp, audio, [] (const AdcCompressor& c) { return c.getGainReductionDb(); });
                    const float settled = trace.back();
                    const float most = *std::max_element (trace.begin(), trace.end());
                    float wobble = 0.0f;
                    for (size_t i = trace.size() - 500; i < trace.size(); ++i)
                        wobble = juce::jmax (wobble, std::abs (trace[i] - settled));
                    const auto what = juce::String (label) + " at " + juce::String (hz, 0) + " Hz";
                    logMessage (what + ": settled " + juce::String (settled, 2) + " dB, most " + juce::String (most, 2)
                                + " dB, wobble over the last 0.5 s " + juce::String (wobble, 3) + " dB");
                    expectLessThan (most - settled, allowed, what + ": overshoot");
                    expectLessThan (wobble, 0.1f, what + ": pumping");
                }
        }

        beginTest ("Warmth: hardware-level harmonics, the 2nd leading; nothing but the tube's whisper at 0");
        {
            struct Reading { double thd, h2, h3; float gr; };
            const auto measure = [] (CompParams p, float levelDb)
            {
                AdcCompressor comp;
                comp.setParams (p);
                comp.prepare (sr, block, 1);
                auto tone = sineWave (1000.0, levelDb, 65536);
                runBlocks (comp, tone);
                const double h1 = toneDb (tone, 1000.0);
                return Reading { thdDb (tone, 1000.0), toneDb (tone, 2000.0) - h1, toneDb (tone, 3000.0) - h1, comp.getGainReductionDb() };
            };

            // A voice held down about 6 dB, at House Silk's Warmth.
            auto p = house();
            p.thresholdDb = -24.0f;
            const auto houseNote = measure (p, -12.0f);
            p.warmth = 0.0f;
            const auto noWarmth = measure (p, -12.0f);
            p.warmth = 1.0f;
            p.thresholdDb = -24.0f;
            p.ratio = 3.0f;
            p.optoReduction = 0.5f;
            const auto pushed = measure (p, -6.0f);

            for (const auto& [label, r] : { std::pair { "Warmth 50%, -12 dB note", houseNote }, std::pair { "the same at Warmth 0", noWarmth },
                                           std::pair { "Warmth 100%, -6 dB note", pushed } })
                logMessage (juce::String (label) + ": " + juce::String (r.gr, 1) + " dB GR, THD " + juce::String (r.thd, 1)
                            + " dB (H2 " + juce::String (r.h2, 1) + ", H3 " + juce::String (r.h3, 1) + ")");

            expectWithinAbsoluteError (houseNote.gr, 6.0f, 1.0f, "about 6 dB of reduction");
            expectGreaterThan (houseNote.thd, -60.0, "warmth you can hear...");
            expectLessThan (houseNote.thd, -45.0, "...at hardware level (0.1-0.5%)");
            expectGreaterThan (houseNote.h2, houseNote.h3, "2nd harmonic leading");
            expectLessThan (noWarmth.thd, -55.0, "Warmth 0: only the tube's whisper");
            expectLessThan (pushed.thd, -45.0, "even pushed, inside the budget");
        }

        beginTest ("Weave: the colour deepens the longer the voice is held down, and only with Weave");
        {
            const auto growth = [] (float weave)
            {
                auto p = house();
                p.thresholdDb = -24.0f;
                p.warmth = 1.0f;
                p.weave = weave;
                AdcCompressor comp;
                comp.setParams (p);
                comp.prepare (sr, block, 1);
                auto tone = sineWave (1000.0, -12.0f, 48000 * 7);
                runBlocks (comp, tone);
                const auto h2 = [&tone] (int start) { return toneDb (tone, 2000.0, start, 16384) - toneDb (tone, 1000.0, start, 16384); };
                return std::pair { h2 (19200), h2 (48000 * 6) };
            };

            const auto [early1, late1] = growth (1.0f);
            const auto [early0, late0] = growth (0.0f);
            logMessage ("H2 with Weave: " + juce::String (early1, 1) + " dB at 0.4 s, " + juce::String (late1, 1)
                        + " dB at 6 s; without: " + juce::String (early0, 1) + " -> " + juce::String (late0, 1) + " dB");
            expectGreaterThan (late1 - early1, 1.5, "deepens with time");
            expectWithinAbsoluteError (late0 - early0, 0.0, 0.5, "steady without Weave");
            expectLessThan (late1, -45.0, "still inside the budget");
        }

        beginTest ("Breath gate: a breath or an S is not pulled down like a sung vowel");
        {
            const auto peakReduction = [] (bool breathy)
            {
                auto p = tube (-30.0f, 3.0f);
                p.optoReduction = 0.5f;
                AdcCompressor comp;
                comp.setParams (p);
                comp.prepare (sr, block, 1);

                const int from = 24000, length = 7200, total = 48000;   // 150 ms
                juce::AudioBuffer<float> audio (1, total);
                audio.clear();
                if (breathy)
                {
                    auto hiss = whiteNoise (1, total, 1.0f, 9);
                    Biquad hp, lp;
                    hp.c = Biquad::Coeffs::highPass (sr, 5000.0, 0.707);
                    lp.c = Biquad::Coeffs::lowPass (sr, 12000.0, 0.707);
                    for (int i = 0; i < total; ++i)
                        audio.setSample (0, i, lp.process (hp.process (hiss.getSample (0, i))));
                }
                else
                {
                    audio = sineWave (700.0, 0.0f, total);
                }
                // Same level (-16 dBFS RMS), only during the burst.
                const float rms = audio.getRMSLevel (0, from, length);
                audio.applyGain (juce::Decibels::decibelsToGain (-16.0f) / rms);
                audio.clear (0, from);
                audio.clear (from + length, total - from - length);

                const auto trace = meterTrace (comp, audio, [] (const AdcCompressor& c) { return c.getGainReductionDb(); });
                return *std::max_element (trace.begin(), trace.end());
            };

            const float vowel = peakReduction (false), breath = peakReduction (true);
            logMessage ("150 ms at -16 dBFS RMS: vowel " + juce::String (vowel, 1) + " dB, breath/S " + juce::String (breath, 1) + " dB of reduction");
            expectGreaterThan (vowel, 3.0f);
            expectLessThan (breath, 0.5f * vowel);
        }

        beginTest ("Warmth: loud lines turn warmer and softer on top");
        {
            const auto shape = [] (float warmth)
            {
                auto p = tube (-36.0f, 4.0f);
                p.optoReduction = 0.5f;
                p.warmth = warmth;
                AdcCompressor comp;
                comp.setParams (p);
                comp.prepare (sr, block, 1);
                // Voice-like noise (rolled off above 6 kHz), so the breath gate leaves it alone.
                auto noise = whiteNoise (1, 48000 * 2, 0.6f, 4);
                Biquad lp1, lp2;
                lp1.c = Biquad::Coeffs::lowPass (sr, 6000.0, 0.54);
                lp2.c = Biquad::Coeffs::lowPass (sr, 6000.0, 1.31);
                for (int i = 0; i < noise.getNumSamples(); ++i)
                    noise.setSample (0, i, lp2.process (lp1.process (noise.getSample (0, i))));
                runBlocks (comp, noise);
                return std::pair { bandDb (noise, 220, 340) - bandDb (noise, 900, 1200), bandDb (noise, 11000, 15000) - bandDb (noise, 900, 1200) };
            };

            const auto [warm1, top1] = shape (1.0f);
            const auto [warm0, top0] = shape (0.0f);
            logMessage ("low-mid " + juce::String (warm1 - warm0, 2) + " dB, top " + juce::String (top1 - top0, 2) + " dB");
            expectGreaterThan (warm1 - warm0, 0.8f, "warmer");
            expectLessThan (top1 - top0, -1.5f, "softer on top");
        }

        beginTest ("Sidechain high-pass: low boom moves it far less than the voice does");
        {
            for (const auto& [label, p] : { std::pair { "TUBE", tube (-24.0f, 3.0f) }, std::pair { "OPTO", opto (0.5f) } })
            {
                const float lowGr = steadyReductionDb (p, -10.0f, 45.0), voiceGr = steadyReductionDb (p, -10.0f, 1000.0);
                logMessage (juce::String (label) + ": GR at 45 Hz " + juce::String (lowGr, 1) + " dB, at 1 kHz " + juce::String (voiceGr, 1) + " dB");
                expectLessThan (lowGr, voiceGr - 3.0f, label);
            }
        }

        beginTest ("Auto gain keeps a sung phrase near its level");
        {
            for (const auto& [label, p] : { std::pair { "house", house() }, std::pair { "firm", [] { auto q = house(); q.thresholdDb = -24.0f; q.ratio = 3.0f; q.optoReduction = 0.5f; return q; }() } })
            {
                AdcCompressor comp;
                comp.setParams (p);
                comp.prepare (sr, block, 2);
                const auto in = sungPhrase (48000 * 4);
                auto out = in;
                runBlocks (comp, out);
                const int lat = comp.getLatencySamples(), start = 48000;
                const float change = juce::Decibels::gainToDecibels (out.getRMSLevel (0, start + lat, 48000 * 3 - lat))
                                   - juce::Decibels::gainToDecibels (in.getRMSLevel (0, start, 48000 * 3 - lat));
                logMessage (juce::String (label) + ": level change " + juce::String (change, 2) + " dB, GR now " + juce::String (comp.getGainReductionDb(), 1) + " dB");
                expectWithinAbsoluteError (change, 0.0f, 1.5f, label);
            }
        }
    }

private:
    /** Level (dB) of the part of a signal inside a band, over its second half. */
    static float bandDb (const juce::AudioBuffer<float>& b, double lowHz, double highHz)
    {
        Biquad hp1, hp2, lp1, lp2;
        hp1.c = Biquad::Coeffs::highPass (sr, lowHz, 0.54);
        hp2.c = Biquad::Coeffs::highPass (sr, lowHz, 1.31);
        lp1.c = Biquad::Coeffs::lowPass (sr, highHz, 0.54);
        lp2.c = Biquad::Coeffs::lowPass (sr, highHz, 1.31);
        const int n = b.getNumSamples(), start = n / 2;
        double sum = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const float y = lp2.process (lp1.process (hp2.process (hp1.process (b.getSample (0, i)))));
            if (i >= start)
                sum += (double) y * y;
        }
        return (float) (10.0 * std::log10 (sum / (n - start) + 1.0e-30));
    }
};

static AdcTests adcTests;

} // namespace linen::test
