#include "Air.h"

namespace linen
{

namespace
{
    constexpr int controlInterval = 16;
    constexpr float maxLiftDb = 10.0f, maxDeEssDb = 6.0f;
    constexpr double midHz = 3000.0, highHz = 9000.0, shelfHz = 8000.0;

    // The side signal's ceiling, relative to the whole voice: a band sitting
    // well under the voice (a dull top) gets the full lift, one near the
    // voice's own level (a bright top, an S) hardly any. Relative, so it
    // behaves the same whatever the recording level.
    // The upper mids sit nearer the voice's level than the top does, so Mid
    // Air's ceiling is higher.
    constexpr float highCeiling = 0.15f;        // -16.5 dB under the voice (RMS)
    constexpr float midCeiling = 0.5f;          // -6 dB under the voice
    // Below the noise floor the lift fades out again, so hiss is not raised.
    constexpr float floorLow = 0.00003f, floorHigh = 0.0003f;  // -90 to -70 dBFS

    float toDb (float power) { return 10.0f * std::log10 (power + 1.0e-12f); }

    float sideGain (float k, float envelope, float voice, float ceiling)
    {
        if (k <= 0.0f)
            return 0.0f;
        const float t = juce::jlimit (0.0f, 1.0f, (envelope - floorLow) / (floorHigh - floorLow));
        return k / (1.0f + envelope * k / (ceiling * voice + 1.0e-9f)) * (t * t * (3.0f - 2.0f * t));
    }
}

float Air::liftDbAt (float liftDb, float bandUnderVoiceDb)
{
    const float k = juce::Decibels::decibelsToGain (liftDb) - 1.0f;
    return juce::Decibels::gainToDecibels (1.0f + sideGain (k, juce::Decibels::decibelsToGain (bandUnderVoiceDb - 30.0f),
                                                            juce::Decibels::decibelsToGain (-30.0f), highCeiling));
}

void Air::prepare (double newSampleRate, int maxBlockSize, int newNumChannels)
{
    sampleRate = newSampleRate;
    numChannels = newNumChannels;

    vowelHp.c = Biquad::Coeffs::highPass (sampleRate, 200.0, 0.707);
    vowelLp.c = Biquad::Coeffs::lowPass (sampleRate, 2500.0, 0.707);
    sibHp.c = Biquad::Coeffs::highPass (sampleRate, 6000.0, 0.707);
    sibLp.c = Biquad::Coeffs::lowPass (sampleRate, juce::jmin (14000.0, sampleRate * 0.45), 0.707);

    powerCoeff = (float) std::exp (-1.0 / (0.005 * sampleRate));
    sibAttack = (float) std::exp (-(double) controlInterval / (0.002 * sampleRate));
    sibRelease = (float) std::exp (-(double) controlInterval / (0.060 * sampleRate));

    // RMS levels, smooth enough not to ripple with the voice's pitch (which
    // would modulate the air), quick enough to duck under an S.
    envAttack = (float) std::exp (-1.0 / (0.004 * sampleRate));
    envRelease = (float) std::exp (-1.0 / (0.050 * sampleRate));
    voiceRelease = (float) std::exp (-1.0 / (0.030 * sampleRate));
    gainGlide = (float) std::exp (-1.0 / (0.006 * sampleRate));

    for (auto [band, hz] : { std::pair { &midBand, midHz }, std::pair { &highBand, highHz } })
    {
        const auto coeffs = Biquad::Coeffs::highPass (sampleRate, juce::jmin (hz, sampleRate * 0.4), 0.707);
        band->detector.c = coeffs;
        band->filters.assign ((size_t) numChannels, {});
        for (auto& f : band->filters)
            f.c = coeffs;
    }
    midGain.assign ((size_t) maxBlockSize, 0.0f);
    highGain.assign ((size_t) maxBlockSize, 0.0f);

    shelves.assign ((size_t) numChannels, {});
    reset();
}

void Air::reset()
{
    for (auto* b : { &vowelHp, &vowelLp, &sibHp, &sibLp })
        b->reset();
    vowelPower = sibPower = 0.0f;
    sibilance = 0.0f;
    sibilanceMeter.store (0.0f);

    for (auto* band : { &midBand, &highBand })
    {
        band->detector.reset();
        for (auto& f : band->filters)
            f.reset();
        band->envelope = band->gain = 0.0f;
    }
    voiceEnvelope = 0.0f;

    shelfDb = 0.0f;
    for (auto& s : shelves)
    {
        s.reset();
        s.c = Biquad::Coeffs::identity();
    }
}

void Air::updateControl()
{
    const float vowelDb = toDb (vowelPower), sibDb = toDb (sibPower);

    // Sibilance: the top band louder than the vowel band, and loud in itself.
    const float sibTarget = juce::jlimit (0.0f, 1.0f, (sibDb - vowelDb + 2.0f) / 8.0f)
                          * juce::jlimit (0.0f, 1.0f, (sibDb + 50.0f) / 10.0f);
    sibilance = sibTarget + (sibilance - sibTarget) * (sibTarget > sibilance ? sibAttack : sibRelease);

    // The air closes on S and T sounds; scorched air (Pain) is damped.
    const float scorch = juce::jlimit (0.0f, 1.0f, params.scorch);
    const float open = (1.0f - 0.7f * sibilance) * (1.0f - 0.6f * scorch);

    // Silk loom: where De-harsh cut the top, High Air opens further (up to +4 dB more at 6 dB of cut).
    const float woven = juce::jlimit (0.0f, 1.0f, params.loom) * juce::jlimit (0.0f, 1.0f, harshCutDb / 6.0f);
    const float highLift = maxLiftDb * juce::jlimit (0.0f, 1.0f, params.amount) * open
                         + (params.amount > 0.0f ? 4.0f * woven * open : 0.0f);
    const float midLift = maxLiftDb * juce::jlimit (0.0f, 1.0f, params.mid) * open;
    highBand.k = highLift > 0.0f ? juce::Decibels::decibelsToGain (highLift) - 1.0f : 0.0f;
    midBand.k = midLift > 0.0f ? juce::Decibels::decibelsToGain (midLift) - 1.0f : 0.0f;

    shelfDb = -juce::jlimit (0.0f, 1.0f, params.deEss) * maxDeEssDb * sibilance - 2.5f * scorch;
    const auto c = std::abs (shelfDb) < 0.001f ? Biquad::Coeffs::identity()
                                               : Biquad::Coeffs::highShelf (sampleRate, shelfHz, 0.6, shelfDb);
    for (auto& s : shelves)
        s.c = c;

    sibilanceMeter.store (sibilance, std::memory_order_relaxed);
}

void Air::process (juce::AudioBuffer<float>& buffer)
{
    const int n = buffer.getNumSamples();
    const int chans = juce::jmin (numChannels, buffer.getNumChannels());
    if (chans == 0)
        return;

    const bool idle = params.amount <= 0.0f && params.mid <= 0.0f && params.deEss <= 0.0f && params.scorch <= 0.0f
                   && std::abs (shelfDb) < 0.001f;
    const float norm = 1.0f / (float) chans;

    // Detection (mono, so both channels move together), every sample.
    for (int start = 0; start < n; start += controlInterval)
    {
        const int len = juce::jmin (controlInterval, n - start);
        for (int i = start; i < start + len; ++i)
        {
            float x = 0.0f;
            for (int ch = 0; ch < chans; ++ch)
                x += buffer.getSample (ch, i);
            x *= norm;

            const float v = vowelLp.process (vowelHp.process (x));
            const float s = sibLp.process (sibHp.process (x));
            vowelPower = v * v + (vowelPower - v * v) * powerCoeff;
            sibPower = s * s + (sibPower - s * s) * powerCoeff;

            if (! idle)
            {
                voiceEnvelope = x * x + (voiceEnvelope - x * x) * voiceRelease;   // power
                const float voice = std::sqrt (voiceEnvelope);
                for (auto [band, gains, ceiling] : { std::tuple { &midBand, &midGain, midCeiling }, std::tuple { &highBand, &highGain, highCeiling } })
                {
                    const float b = band->detector.process (x);
                    band->envelope = b * b + (band->envelope - b * b) * (b * b > band->envelope ? envAttack : envRelease);
                    const float target = sideGain (band->k, std::sqrt (band->envelope), voice, ceiling);
                    band->gain = target + (band->gain - target) * gainGlide;
                    (*gains)[(size_t) i] = band->gain;
                }
            }
        }

        updateControl();
        if (idle)
            continue;

        for (int ch = 0; ch < chans; ++ch)
        {
            auto* d = buffer.getWritePointer (ch);
            auto& mid = midBand.filters[(size_t) ch];
            auto& high = highBand.filters[(size_t) ch];
            auto& shelf = shelves[(size_t) ch];
            for (int i = start; i < start + len; ++i)
            {
                const float x = d[i];
                const float side = mid.process (x) * midGain[(size_t) i] + high.process (x) * highGain[(size_t) i];
                d[i] = shelf.process (x + side);
            }
        }
    }
}

} // namespace linen
