#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "dsp/LinenEngine.h"

namespace linen::test
{

inline juce::AudioBuffer<float> whiteNoise (int channels, int samples, float level = 0.25f, juce::int64 seed = 1)
{
    juce::AudioBuffer<float> buffer (channels, samples);
    juce::Random rng (seed);
    for (int ch = 0; ch < channels; ++ch)
        for (int i = 0; i < samples; ++i)
            buffer.setSample (ch, i, (rng.nextFloat() * 2.0f - 1.0f) * level);
    return buffer;
}

inline juce::AudioBuffer<float> impulse (int channels, int samples, int position = 0)
{
    juce::AudioBuffer<float> buffer (channels, samples);
    buffer.clear();
    for (int ch = 0; ch < channels; ++ch)
        buffer.setSample (ch, position, 1.0f);
    return buffer;
}

/** Runs a whole buffer through the engine in irregular block sizes, the way a
    host might, and returns the result. */
inline juce::AudioBuffer<float> runInBlocks (Engine& engine, const juce::AudioBuffer<float>& input, int maxBlock)
{
    juce::AudioBuffer<float> output (input);
    const int sizes[] { maxBlock, 1, maxBlock / 2, 17, maxBlock - 3 };
    int pos = 0, which = 0;

    while (pos < output.getNumSamples())
    {
        const int n = juce::jmin (juce::jmax (1, sizes[which++ % 5]), output.getNumSamples() - pos);
        juce::AudioBuffer<float> block (output.getArrayOfWritePointers(), output.getNumChannels(), pos, n);
        engine.process (block);
        pos += n;
    }

    return output;
}

inline float maxAbsDifference (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int bOffset = 0)
{
    float worst = 0.0f;
    for (int ch = 0; ch < a.getNumChannels(); ++ch)
        for (int i = 0; i + bOffset < b.getNumSamples() && i < a.getNumSamples(); ++i)
            worst = juce::jmax (worst, std::abs (a.getSample (ch, i) - b.getSample (ch, i + bOffset)));
    return worst;
}

/** A sung phrase with a realistic tonal balance: notes of a vowel on
    196-294 Hz with vibrato and swells, formants near 700 Hz and 1.2 kHz, a
    singer's ring near 3.2 kHz, harmonics out to 18 kHz falling the way a
    real voice's long-term spectrum does, and breath between notes. Peaks
    around -6 dBFS, RMS around -20 dBFS: a typical tracked vocal. */
inline juce::AudioBuffer<float> sungPhrase (int samples, double sampleRate = 48000.0, int channels = 2)
{
    // Long-term spectrum of a voice, relative to its mids (dB), by frequency.
    const auto spectrumDb = [] (double hz)
    {
        static constexpr std::array<std::pair<double, double>, 9> anchors {{
            { 100.0, -6.0 }, { 200.0, -2.0 }, { 300.0, 0.0 }, { 1000.0, 0.0 }, { 2000.0, -5.0 },
            { 4000.0, -11.0 }, { 8000.0, -17.0 }, { 12000.0, -22.0 }, { 18000.0, -30.0 } }};
        if (hz <= anchors.front().first) return anchors.front().second;
        for (size_t i = 1; i < anchors.size(); ++i)
            if (hz <= anchors[i].first)
            {
                const double t = std::log (hz / anchors[i - 1].first) / std::log (anchors[i].first / anchors[i - 1].first);
                return anchors[i - 1].second + (anchors[i].second - anchors[i - 1].second) * t;
            }
        return anchors.back().second;
    };

    const auto bump = [] (double hz, double centre, double db, double octaves)
    {
        const double d = std::log2 (hz / centre) / octaves;
        return db * std::exp (-0.5 * d * d);
    };

    juce::AudioBuffer<float> buffer (channels, samples);
    buffer.clear();
    juce::Random rng (21);
    const double notes[] { 220.0, 247.0, 196.0, 294.0, 262.0, 220.0 };
    double phase = 0.0;

    for (int i = 0; i < samples; ++i)
    {
        const double t = i / sampleRate;
        const int note = (int) (t / 0.5) % 6;
        const double inNote = std::fmod (t, 0.5);
        const double envelope = inNote < 0.42 ? std::sin (juce::MathConstants<double>::pi * inNote / 0.42) * (0.6 + 0.4 * (note % 3) / 2.0)
                                              : 0.0;
        const double f0 = notes[note] * (1.0 + 0.012 * std::sin (juce::MathConstants<double>::twoPi * 5.5 * t));
        phase += juce::MathConstants<double>::twoPi * f0 / sampleRate;

        double v = 0.0;
        for (int n = 1; n * f0 < 18000.0; ++n)
        {
            const double hz = n * f0;
            // Per-harmonic level: the long-term spectrum, less the extra harmonics
            // each third-octave band holds at higher frequencies.
            const double db = spectrumDb (hz) - 10.0 * std::log10 (std::max (1.0, 0.23 * hz / f0))
                            + bump (hz, 700.0, 4.0, 0.5) + bump (hz, 1200.0, 2.0, 0.4) + bump (hz, 3200.0, 3.0, 0.3);
            v += std::pow (10.0, db / 20.0) * std::sin (phase * n);
        }

        const double breath = (inNote >= 0.42 ? 0.01 : 0.003) * (rng.nextFloat() - 0.5);
        const float sample = (float) (0.07 * envelope * v + breath);
        for (int ch = 0; ch < channels; ++ch)
            buffer.setSample (ch, i, sample);
    }
    return buffer;
}

/** 4-term Blackman-Harris window (sidelobes below -92 dB). */
inline float blackmanHarris (int i, int n)
{
    const double x = juce::MathConstants<double>::twoPi * i / (n - 1);
    return (float) (0.35875 - 0.48829 * std::cos (x) + 0.14128 * std::cos (2 * x) - 0.01168 * std::cos (3 * x));
}

/** Level of a single tone (dBFS) from a windowed DFT at exactly hz. */
inline double toneDb (const juce::AudioBuffer<float>& audio, double hz, int start = 16384, int length = 32768, double sampleRate = 48000.0)
{
    double re = 0.0, im = 0.0, wsum = 0.0;
    for (int i = 0; i < length; ++i)
    {
        const double w = blackmanHarris (i, length);
        const double phase = juce::MathConstants<double>::twoPi * hz * i / sampleRate;
        const double x = audio.getSample (0, start + i) * w;
        re += x * std::cos (phase);
        im -= x * std::sin (phase);
        wsum += w;
    }
    return 20.0 * std::log10 (2.0 * std::sqrt (re * re + im * im) / wsum + 1.0e-20);
}

} // namespace linen::test
