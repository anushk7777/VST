#include "AutoAssist.h"
#include "Biquad.h"
#include "PitchTracker.h"

namespace linen
{

namespace
{
    // Third-octave bands the tonal balance is read in.
    constexpr std::array<float, 25> bandHz { 63.0f, 80.0f, 100.0f, 125.0f, 160.0f, 200.0f, 250.0f, 315.0f, 400.0f, 500.0f,
                                             630.0f, 800.0f, 1000.0f, 1250.0f, 1600.0f, 2000.0f, 2500.0f, 3150.0f, 4000.0f,
                                             5000.0f, 6300.0f, 8000.0f, 10000.0f, 12500.0f, 16000.0f };

    /** The long-term spectrum of a smooth, finished pop vocal, relative to its
        mids: full but not muddy below, a steady fall through the presence
        region (about -6 dB/oct, a touch brighter than raw singing), and a
        soft, open top. Read as third-octave band power, so the low mids sit
        lower than a smooth curve would suggest: a sung voice has only one or
        two harmonics per band down there. */
    float targetDb (float hz)
    {
        static constexpr std::array<std::pair<float, float>, 11> anchors {{
            { 63.0f, -21.0f }, { 125.0f, -14.0f }, { 200.0f, -10.0f }, { 300.0f, -6.5f }, { 500.0f, -2.5f }, { 1000.0f, 0.0f },
            { 2000.0f, -5.0f }, { 4000.0f, -11.0f }, { 8000.0f, -17.0f }, { 12000.0f, -22.0f }, { 16000.0f, -28.0f } }};

        if (hz <= anchors.front().first) return anchors.front().second;
        if (hz >= anchors.back().first)  return anchors.back().second;

        for (size_t i = 1; i < anchors.size(); ++i)
            if (hz <= anchors[i].first)
            {
                const auto [f0, d0] = anchors[i - 1];
                const auto [f1, d1] = anchors[i];
                const float t = std::log (hz / f0) / std::log (f1 / f0);
                return d0 + (d1 - d0) * t;
            }
        return 0.0f;
    }

    float percentile (std::vector<float> values, float q)
    {
        if (values.empty())
            return 0.0f;
        const auto index = (size_t) juce::jlimit (0, (int) values.size() - 1, juce::roundToInt (q * (float) (values.size() - 1)));
        std::nth_element (values.begin(), values.begin() + (std::ptrdiff_t) index, values.end());
        return values[index];
    }

    float powerDb (double power) { return (float) (10.0 * std::log10 (power + 1.0e-20)); }

    /** How much more (or less) energy the voice has between lowHz and highHz
        than the target, in dB. Summing energy rather than averaging band
        levels keeps the gaps between harmonics from reading as holes. */
    float rangeDeviation (const std::array<float, bandHz.size()>& relativeLevel, const std::array<bool, bandHz.size()>& present,
                          float lowHz, float highHz)
    {
        double measured = 0.0, target = 0.0;
        for (size_t b = 0; b < bandHz.size(); ++b)
            if (present[b] && bandHz[b] >= lowHz && bandHz[b] <= highHz)
            {
                measured += std::pow (10.0, relativeLevel[b] / 10.0);
                target += std::pow (10.0, targetDb (bandHz[b]) / 10.0);
            }
        return target > 0.0 ? (float) (10.0 * std::log10 ((measured + 1.0e-20) / target)) : 0.0f;
    }

    float roundTo (float value, float step) { return std::round (value / step) * step; }
}

AssistResult AutoAssist::analyse (const float* x, int n, double sr)
{
    AssistResult r;

    // 1. Where is the singing? 50 ms frames, gated relative to the loudest.
    const int frame = juce::jmax (1, juce::roundToInt (0.05 * sr));
    const int numFrames = n / frame;
    std::vector<float> frameDb ((size_t) numFrames);
    float loudest = -200.0f;
    for (int f = 0; f < numFrames; ++f)
    {
        double sum = 0.0;
        for (int i = f * frame; i < (f + 1) * frame; ++i)
            sum += (double) x[i] * x[i];
        frameDb[(size_t) f] = powerDb (sum / frame);
        loudest = juce::jmax (loudest, frameDb[(size_t) f]);
    }

    const float gate = juce::jmax (-50.0f, loudest - 40.0f);
    std::vector<float> activeDb;
    double activePower = 0.0;
    for (int f = 0; f < numFrames; ++f)
        if (frameDb[(size_t) f] > gate)
        {
            activeDb.push_back (frameDb[(size_t) f]);
            activePower += std::pow (10.0, frameDb[(size_t) f] / 10.0);
        }

    if ((double) activeDb.size() * frame / sr < minimumSeconds)
    {
        r.summary = "I couldn't hear enough singing. Play the vocal and press Listen again.";
        return r;
    }

    const auto isActive = [&] (int sample)
    {
        const int f = sample / frame;
        return f >= 0 && f < numFrames && frameDb[(size_t) f] > gate;
    };

    r.levelDb = powerDb (activePower / (double) activeDb.size());
    r.dynamicsDb = percentile (activeDb, 0.9f) - percentile (activeDb, 0.1f);

    // 2. Long-term spectrum of the sung parts, in third-octave bands.
    constexpr int fftOrder = 12, fftSize = 1 << fftOrder;
    juce::dsp::FFT fft (fftOrder);
    std::vector<float> buffer ((size_t) fftSize * 2);
    std::vector<double> power ((size_t) fftSize / 2 + 1, 0.0);
    int spectra = 0;
    for (int pos = 0; pos + fftSize <= n; pos += fftSize / 2)
    {
        if (! isActive (pos + fftSize / 2))
            continue;
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        for (int j = 0; j < fftSize; ++j)
            buffer[(size_t) j] = x[pos + j] * 0.5f * (1.0f - std::cos (juce::MathConstants<float>::twoPi * (float) j / (float) fftSize));
        fft.performFrequencyOnlyForwardTransform (buffer.data(), true);
        for (size_t k = 0; k < power.size(); ++k)
            power[k] += (double) buffer[k] * buffer[k];
        ++spectra;
    }

    std::array<float, bandHz.size()> bandLevel {}, relative {}, deviation {};
    std::array<bool, bandHz.size()> present {};
    const double binHz = sr / fftSize;
    for (size_t b = 0; b < bandHz.size(); ++b)
    {
        const double lo = bandHz[b] * std::pow (2.0, -1.0 / 6.0), hi = bandHz[b] * std::pow (2.0, 1.0 / 6.0);
        present[b] = hi < sr * 0.5 && spectra > 0;
        if (! present[b])
            continue;
        // Band power (the sum over the band), as third-octave levels are read.
        double sum = 0.0;
        for (auto k = (size_t) std::ceil (lo / binHz); k <= (size_t) std::floor (hi / binHz) && k < power.size(); ++k)
            sum += power[k];
        bandLevel[b] = powerDb (sum / juce::jmax (1, spectra));
    }

    // Deviation from the target, with the upper mids (800 Hz-1.25 kHz) as the
    // reference: far enough from the low mids that mud can't lift it too.
    float reference = 0.0f;
    int refCount = 0;
    for (size_t b = 0; b < bandHz.size(); ++b)
        if (present[b] && bandHz[b] >= 800.0f && bandHz[b] <= 1250.0f)
        {
            reference += bandLevel[b] - targetDb (bandHz[b]);
            ++refCount;
        }
    reference /= (float) juce::jmax (1, refCount);
    for (size_t b = 0; b < bandHz.size(); ++b)
    {
        relative[b] = present[b] ? bandLevel[b] - reference : -100.0f;
        deviation[b] = present[b] ? relative[b] - targetDb (bandHz[b]) : 0.0f;
    }

    r.mudDb = rangeDeviation (relative, present, 200.0f, 500.0f);
    r.boomDb = rangeDeviation (relative, present, 63.0f, 125.0f);
    r.harshDb = rangeDeviation (relative, present, 2500.0f, 5000.0f);
    r.dullDb = -rangeDeviation (relative, present, 8000.0f, 16000.0f);
    {
        std::vector<float> wide;
        float peak = -100.0f;
        for (size_t b = 0; b < bandHz.size(); ++b)
        {
            if (! present[b])
                continue;
            if (bandHz[b] >= 1000.0f && bandHz[b] <= 8000.0f)
                wide.push_back (deviation[b]);
            if (bandHz[b] >= 2000.0f && bandHz[b] <= 6300.0f)
                peak = juce::jmax (peak, deviation[b]);
        }
        r.resonanceDb = juce::jmax (0.0f, peak - percentile (wide, 0.5f));
    }
    const float presence = rangeDeviation (relative, present, 2000.0f, 4000.0f);

    // 3. Sibilance: 10 ms frames, top band (5-10 kHz) against the vowel band.
    {
        Biquad vowelHp, vowelLp, sibHp, sibLp;
        vowelHp.c = Biquad::Coeffs::highPass (sr, 200.0, 0.707);
        vowelLp.c = Biquad::Coeffs::lowPass (sr, 2500.0, 0.707);
        sibHp.c = Biquad::Coeffs::highPass (sr, 5000.0, 0.707);
        sibLp.c = Biquad::Coeffs::lowPass (sr, juce::jmin (10000.0, sr * 0.45), 0.707);

        const int sibFrame = juce::jmax (1, juce::roundToInt (0.01 * sr));
        std::vector<float> ratios;
        double vowelSum = 0.0, sibSum = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const float v = vowelLp.process (vowelHp.process (x[i]));
            const float s = sibLp.process (sibHp.process (x[i]));
            vowelSum += (double) v * v;
            sibSum += (double) s * s;
            if ((i + 1) % sibFrame == 0)
            {
                const float sibDb = powerDb (sibSum / sibFrame);
                if (sibDb > -50.0f && sibDb > loudest - 45.0f)
                    ratios.push_back (sibDb - powerDb (vowelSum / sibFrame));
                vowelSum = sibSum = 0.0;
            }
        }
        r.sibilanceDb = ratios.empty() ? -30.0f : percentile (ratios, 0.95f);
    }

    // 4. Pitch: median of confidently voiced frames.
    {
        PitchTracker tracker;
        tracker.prepare (sr, 2048);
        std::vector<float> pitches;
        for (int pos = 0; pos + 2048 <= n; pos += 4096)
            if (isActive (pos + 1024))
            {
                const auto p = tracker.analyse (x + pos);
                if (p.clarity > 0.8f && p.frequency >= 70.0f && p.frequency <= 600.0f)
                    pitches.push_back (p.frequency);
            }
        r.pitchHz = percentile (pitches, 0.5f);
    }

    // 5. Suggestions.
    r.inputGainDb = roundTo (juce::jlimit (-12.0f, 12.0f, -18.0f - r.levelDb), 0.5f);

    // Low end, shaped before the preamp. A gentle shelf just under the sung
    // notes (60% of the median note) clears rumble and boom, as a high-pass
    // would, without its steep phase turn; a muddy voice gets the shelf moved
    // up into the mud instead, and a thin one keeps its weight.
    const bool thin = r.mudDb < -3.0f;
    r.toneLowHz = roundTo (juce::jlimit (60.0f, 160.0f, r.pitchHz > 0.0f ? 0.6f * r.pitchHz : 100.0f), 5.0f);
    r.toneLowGainDb = r.boomDb > 3.0f ? -roundTo (juce::jlimit (3.0f, 6.0f, 2.0f + 0.5f * r.boomDb), 0.5f) : -3.0f;
    if (r.mudDb > 1.5f)
    {
        r.toneLowHz = 300.0f;
        r.toneLowGainDb = -roundTo (juce::jlimit (1.5f, 5.0f, 1.0f + 0.7f * r.mudDb), 0.5f);
    }
    else if (thin)
    {
        r.toneLowGainDb = -1.0f;
    }

    // The console's low shelf only ever adds weight to a thin voice.
    r.lowFreq = 2;
    r.lowGainDb = 0.0f;
    if (thin)
    {
        r.lowFreq = 3;   // 110 Hz
        r.lowGainDb = roundTo (juce::jmin (2.0f, -r.mudDb * 0.4f), 0.5f);
    }

    r.midFreq = 4;   // 3.2 kHz
    r.midGainDb = 0.0f;
    if (r.harshDb > 2.0f)
        r.midGainDb = -roundTo (juce::jlimit (0.5f, 3.0f, r.harshDb * 0.5f), 0.5f);
    else if (presence < -3.0f)
        r.midGainDb = 1.5f;

    if (r.dullDb > 2.0f)
        r.highGainDb = roundTo (juce::jlimit (1.0f, 4.0f, r.dullDb * 0.5f), 0.5f);
    else if (r.dullDb < -3.0f)
        r.highGainDb = -roundTo (juce::jlimit (0.5f, 3.0f, -r.dullDb * 0.4f), 0.5f);

    r.preampDrive = thin ? 0.4f : 0.3f;

    const float harshScore = juce::jmax (0.0f, r.harshDb) + 0.5f * juce::jmax (0.0f, r.resonanceDb - 2.0f);
    r.deharshDepth = roundTo (juce::jlimit (3.0f, 10.0f, 4.0f + harshScore * 0.8f), 0.1f);
    r.deharshDetail = r.resonanceDb > 4.0f ? 0.65f : 0.5f;

    // Compression starts on the upper third of the phrases (after the input gain).
    const float loudPhrases = percentile (activeDb, 0.7f) + r.inputGainDb + 3.0f;
    r.compThresholdDb = roundTo (juce::jlimit (-36.0f, -6.0f, loudPhrases - 2.0f), 0.5f);
    r.compRatio = r.dynamicsDb > 18.0f ? 3.0f : (r.dynamicsDb > 12.0f ? 2.5f : 2.0f);
    r.compCotton = juce::jlimit (0.2f, 0.8f, 0.5f + (thin || r.dullDb > 2.0f ? 0.15f : 0.0f) - (r.mudDb > 1.5f ? 0.15f : 0.0f));

    // The opto levels the line under the tube: more for a singer with big dynamics.
    r.optoReduction = r.dynamicsDb > 18.0f ? 0.45f : (r.dynamicsDb > 12.0f ? 0.4f : 0.3f);

    r.airAmount = roundTo (juce::jlimit (0.1f, 0.65f, 0.3f + r.dullDb * 0.08f), 0.05f);
    r.airDeEss = roundTo (juce::jlimit (0.2f, 0.9f, 0.35f + (r.sibilanceDb + 10.0f) * 0.03f), 0.05f);

    // 6. A short, human summary.
    juce::StringArray heard, did;
    if (r.harshDb > 2.0f || r.resonanceDb > 4.0f) heard.add ("some edge around 3-5 kHz");
    if (r.mudDb > 1.5f)                           heard.add ("a little mud");
    if (r.boomDb > 3.0f)                          heard.add ("low-end boom");
    if (thin)                                     heard.add ("a thin low-mid");
    if (r.dullDb > 2.0f)                          heard.add ("a closed-in top");
    if (r.dullDb < -3.0f)                         heard.add ("a very bright top");
    if (r.sibilanceDb > 0.0f)                     heard.add ("sharp esses");
    heard.add (r.dynamicsDb > 18.0f ? "big dynamics" : (r.dynamicsDb > 12.0f ? "lively dynamics" : "steady dynamics"));

    did.add ("De-harsh " + juce::String (r.deharshDepth, 1));
    if (r.toneLowGainDb < 0.0f)
        did.add ("low shelf " + juce::String (r.toneLowGainDb, 1) + " dB under " + juce::String (juce::roundToInt (r.toneLowHz)) + " Hz");
    if (r.lowGainDb > 0.0f) did.add ("low lift +" + juce::String (r.lowGainDb, 1) + " dB");
    if (! juce::exactlyEqual (r.highGainDb, 0.0f)) did.add ("12k " + juce::String (r.highGainDb, 1) + " dB");
    did.add ("Dua Wox tube " + juce::String (r.compRatio, 1) + ":1 at " + juce::String (r.compThresholdDb, 1) + " dB, opto "
             + juce::String (juce::roundToInt (r.optoReduction * 100.0f)) + "%");
    did.add ("de-ess " + juce::String (juce::roundToInt (r.airDeEss * 100.0f)) + "%");

    r.summary = "Heard " + heard.joinIntoString (", ") + ". Set " + did.joinIntoString (", ") + ".";
    r.valid = true;
    return r;
}

} // namespace linen
