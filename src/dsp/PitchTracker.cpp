#include "PitchTracker.h"

namespace linen
{

void PitchTracker::prepare (double newSampleRate, int newFrameSize, float minHz, float maxHz)
{
    sampleRate = newSampleRate;
    frameSize = newFrameSize;
    minLag = juce::jmax (2, (int) std::floor (sampleRate / maxHz));
    maxLag = juce::jmin (frameSize / 2, (int) std::ceil (sampleRate / minHz));

    // Zero-padded to twice the frame so the autocorrelation does not wrap.
    fft = std::make_unique<juce::dsp::FFT> (juce::roundToInt (std::log2 (frameSize)) + 1);
    work.assign ((size_t) frameSize * 4, 0.0f);
    nsdf.assign ((size_t) maxLag + 2, 0.0f);
}

PitchTracker::Result PitchTracker::analyse (const float* x)
{
    if (fft == nullptr)
        return {};

    double energy = 0.0;
    for (int i = 0; i < frameSize; ++i)
        energy += (double) x[i] * x[i];

    // Too quiet to be a voice (below about -60 dBFS RMS).
    if (energy / frameSize < 1.0e-6)
        return {};

    // r(τ) = Σ x[j]·x[j+τ], via |FFT|² of the zero-padded frame.
    std::fill (work.begin(), work.end(), 0.0f);
    std::copy (x, x + frameSize, work.begin());
    fft->performRealOnlyForwardTransform (work.data(), true);

    const int fftSize = fft->getSize();
    for (int k = 0; k <= fftSize / 2; ++k)
    {
        const float re = work[(size_t) (2 * k)], im = work[(size_t) (2 * k + 1)];
        work[(size_t) (2 * k)] = re * re + im * im;
        work[(size_t) (2 * k + 1)] = 0.0f;
    }
    fft->performRealOnlyInverseTransform (work.data());
    const float* r = work.data();

    // m(τ) = Σ x[j]² + x[j+τ]², updated incrementally; n(τ) = 2r(τ)/m(τ).
    double m = 2.0 * energy;
    for (int lag = 1; lag <= maxLag; ++lag)
    {
        m -= (double) x[lag - 1] * x[lag - 1] + (double) x[frameSize - lag] * x[frameSize - lag];
        nsdf[(size_t) lag] = m > 1.0e-12 ? (float) (2.0 * r[lag] / m) : 0.0f;
    }

    // Key maxima: the highest point of each positive lobe after the first dip.
    float best = 0.0f;
    int lobePeaks[64];
    int numPeaks = 0;
    int lag = 1;
    while (lag <= maxLag && nsdf[(size_t) lag] > 0.0f)
        ++lag;

    while (lag <= maxLag && numPeaks < 64)
    {
        while (lag <= maxLag && nsdf[(size_t) lag] <= 0.0f)
            ++lag;

        int peak = -1;
        while (lag <= maxLag && nsdf[(size_t) lag] > 0.0f)
        {
            if (lag >= minLag && (peak < 0 || nsdf[(size_t) lag] > nsdf[(size_t) peak]))
                peak = lag;
            ++lag;
        }

        if (peak > 0 && peak < maxLag)
        {
            lobePeaks[numPeaks++] = peak;
            best = juce::jmax (best, nsdf[(size_t) peak]);
        }
    }

    // The first lobe close to the best one is the period (later ones are multiples).
    for (int i = 0; i < numPeaks; ++i)
    {
        const int p = lobePeaks[i];
        const float y0 = nsdf[(size_t) p - 1], y1 = nsdf[(size_t) p], y2 = nsdf[(size_t) p + 1];

        if (y1 < 0.88f * best)
            continue;

        const float denom = y0 - 2.0f * y1 + y2;
        const float shift = std::abs (denom) > 1.0e-9f ? 0.5f * (y0 - y2) / denom : 0.0f;
        const float period = (float) p + juce::jlimit (-0.5f, 0.5f, shift);

        return { (float) (sampleRate / period), juce::jlimit (0.0f, 1.0f, y1) };
    }

    return {};
}

} // namespace linen
