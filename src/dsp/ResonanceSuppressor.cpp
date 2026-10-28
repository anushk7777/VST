#include "ResonanceSuppressor.h"

namespace linen
{

namespace
{
    template <typename T>
    T lerp (T a, T b, T t) { return a + (b - a) * t; }

    /** Smooth 0..1 fade: 1 inside the band, raised-cosine over fadeOctaves outside it. */
    float bandMask (float hz, float lowHz, float highHz, float fadeOctaves)
    {
        auto fade = [fadeOctaves] (float octavesOutside)
        {
            if (octavesOutside <= 0.0f)
                return 1.0f;
            if (octavesOutside >= fadeOctaves)
                return 0.0f;
            return 0.5f * (1.0f + std::cos (juce::MathConstants<float>::pi * octavesOutside / fadeOctaves));
        };

        if (hz <= 0.0f)
            return 0.0f;

        return fade (std::log2 (lowHz / hz)) * fade (std::log2 (hz / highHz));
    }

    /** Running max over windows [lo(k), hi(k)] whose ends never move backwards. */
    template <typename Bounds>
    void slidingMax (const float* src, float* dst, int n, std::vector<int>& queue, Bounds bounds)
    {
        // Monotone deque of indices (largest value at the head).
        int head = 0, tail = 0, next = 0;

        for (int k = 0; k < n; ++k)
        {
            const auto [lo, hi] = bounds (k);

            for (; next <= hi; ++next)
            {
                while (tail > head && src[queue[(size_t) tail - 1]] <= src[next])
                    --tail;
                queue[(size_t) tail++] = next;
            }

            while (queue[(size_t) head] < lo)
                ++head;

            dst[k] = src[queue[(size_t) head]];
        }
    }
}

void ResonanceSuppressor::prepare (double newSampleRate, int newNumChannels)
{
    sampleRate = newSampleRate;
    numChannels = newNumChannels;

    // Keep the analysis about 43 ms long at any sample rate.
    fftOrder = sampleRate <= 50000.0 ? 11 : (sampleRate <= 100000.0 ? 12 : 13);
    fftSize = 1 << fftOrder;
    hopSize = fftSize / 4;
    numBins = fftSize / 2 + 1;
    binHz = (float) (sampleRate / fftSize);

    fft = std::make_unique<juce::dsp::FFT> (fftOrder);

    // sqrt-Hann on analysis and on synthesis: their product is a Hann window,
    // which overlap-adds to exactly 2.0 at 75% overlap.
    window.resize ((size_t) fftSize);
    for (int j = 0; j < fftSize; ++j)
        window[(size_t) j] = std::sin (juce::MathConstants<float>::pi * (float) j / (float) fftSize);
    olaScale = 0.5f;

    inFifo.assign ((size_t) numChannels, std::vector<float> ((size_t) fftSize, 0.0f));
    outFifo.assign ((size_t) numChannels, std::vector<float> ((size_t) fftSize, 0.0f));
    detFifo.assign ((size_t) fftSize, 0.0f);

    timeData.assign ((size_t) fftSize, {});
    freqData.assign ((size_t) fftSize, {});
    spectra.assign ((size_t) numChannels, std::vector<std::complex<float>> ((size_t) numBins));
    detSpectrum.assign ((size_t) numBins, {});
    detFrame.assign ((size_t) fftSize, 0.0f);

    for (auto* v : { &levelDb, &envelope, &scratchA, &scratchB, &excessAverage, &cutTarget,
                     &cutSmoothed, &cutFinal, &gains, &binFreq, &binOctave, &glareWeight, &focusMask })
        v->assign ((size_t) numBins, 0.0f);

    prefix.assign ((size_t) numBins + 1, 0.0f);
    queue.assign ((size_t) numBins + 1, 0);

    for (int k = 0; k < numBins; ++k)
    {
        const float hz = (float) k * binHz;
        binFreq[(size_t) k] = hz;
        binOctave[(size_t) k] = hz > 0.0f ? std::log2 (hz) : -100.0f;

        // Ear-canal resonance: hearing is most sensitive (and glare most
        // painful) around 3–4 kHz, so excess there counts up to 1.5×.
        const float d = hz > 0.0f ? std::log2 (hz / 3500.0f) / 0.6f : 10.0f;
        glareWeight[(size_t) k] = 1.0f + 0.5f * std::exp (-0.5f * d * d);
    }

    pitchTracker.prepare (sampleRate, fftSize);

    focusMaskLow = focusMaskHigh = -1.0f;  // forces a rebuild on the first frame
    reset();
}

void ResonanceSuppressor::reset()
{
    for (auto& f : inFifo)  std::fill (f.begin(), f.end(), 0.0f);
    for (auto& f : outFifo) std::fill (f.begin(), f.end(), 0.0f);
    std::fill (detFifo.begin(), detFifo.end(), 0.0f);
    std::fill (excessAverage.begin(), excessAverage.end(), 0.0f);
    std::fill (cutSmoothed.begin(), cutSmoothed.end(), 0.0f);
    std::fill (cutFinal.begin(), cutFinal.end(), 0.0f);
    std::fill (gains.begin(), gains.end(), 1.0f);

    fifoPos = 0;
    hopCounter = 0;
    frameCounter = 0;
    lastPitch = {};
    protectedPitch = 0.0f;
    protectAmount = 0.0f;

    for (int p = 0; p < DeharshDisplay::numPoints; ++p)
    {
        display.levelDb[(size_t) p].store (-120.0f);
        display.cutDb[(size_t) p].store (0.0f);
    }
    display.pitchHz.store (0.0f);
}

void ResonanceSuppressor::process (juce::AudioBuffer<float>& main, const float* detector, bool detectorIsMainSum)
{
    detectorIsMainMix = detectorIsMainSum;
    const int mask = fftSize - 1;
    const int channels = juce::jmin (numChannels, main.getNumChannels());

    for (int i = 0; i < main.getNumSamples(); ++i)
    {
        for (int ch = 0; ch < channels; ++ch)
        {
            auto* data = main.getWritePointer (ch);
            inFifo[(size_t) ch][(size_t) fifoPos] = data[i];
            data[i] = outFifo[(size_t) ch][(size_t) fifoPos];
            outFifo[(size_t) ch][(size_t) fifoPos] = 0.0f;
        }

        detFifo[(size_t) fifoPos] = detector[i];
        fifoPos = (fifoPos + 1) & mask;

        // fifoPos now points at the oldest sample of the last fftSize inputs.
        if (++hopCounter == hopSize)
        {
            hopCounter = 0;
            processFrame();
        }
    }
}

void ResonanceSuppressor::processFrame()
{
    forwardChannels();
    analyseDetector();
    computeCuts();
    inverseChannels();
    publishDisplay();
    ++frameCounter;
}

void ResonanceSuppressor::forwardChannels()
{
    // Two real channels per complex FFT: z = a + ib, then split by symmetry.
    const int mask = fftSize - 1;

    for (int a = 0; a < numChannels; a += 2)
    {
        const int b = a + 1 < numChannels ? a + 1 : -1;
        const auto& inA = inFifo[(size_t) a];

        for (int j = 0; j < fftSize; ++j)
        {
            const int idx = (fifoPos + j) & mask;
            const float w = window[(size_t) j];
            timeData[(size_t) j] = { inA[(size_t) idx] * w, b >= 0 ? inFifo[(size_t) b][(size_t) idx] * w : 0.0f };
        }

        fft->perform (timeData.data(), freqData.data(), false);

        auto& specA = spectra[(size_t) a];
        for (int k = 0; k < numBins; ++k)
        {
            const auto zk = freqData[(size_t) k];
            const auto zn = std::conj (freqData[(size_t) ((fftSize - k) & mask)]);
            specA[(size_t) k] = 0.5f * (zk + zn);
            if (b >= 0)
                spectra[(size_t) b][(size_t) k] = std::complex<float> (0.0f, -0.5f) * (zk - zn);
        }
    }
}

void ResonanceSuppressor::inverseChannels()
{
    const int mask = fftSize - 1;

    for (int a = 0; a < numChannels; a += 2)
    {
        const int b = a + 1 < numChannels ? a + 1 : -1;
        const auto& specA = spectra[(size_t) a];
        const std::complex<float> i1 (0.0f, 1.0f);

        for (int k = 0; k < numBins; ++k)
        {
            const float g = params.delta ? 1.0f - gains[(size_t) k] : gains[(size_t) k];
            const auto xa = specA[(size_t) k] * g;
            const auto xb = b >= 0 ? spectra[(size_t) b][(size_t) k] * g : std::complex<float>();

            // Rebuild the full spectrum of a + ib from the two half spectra.
            freqData[(size_t) k] = xa + i1 * xb;
            if (k > 0 && k < fftSize / 2)
                freqData[(size_t) (fftSize - k)] = std::conj (xa) + i1 * std::conj (xb);
        }

        fft->perform (freqData.data(), timeData.data(), true);

        auto& outA = outFifo[(size_t) a];
        for (int j = 0; j < fftSize; ++j)
        {
            const int idx = (fifoPos + j) & mask;
            const float w = window[(size_t) j] * olaScale;
            outA[(size_t) idx] += timeData[(size_t) j].real() * w;
            if (b >= 0)
                outFifo[(size_t) b][(size_t) idx] += timeData[(size_t) j].imag() * w;
        }
    }
}

void ResonanceSuppressor::analyseDetector()
{
    const int mask = fftSize - 1;

    for (int j = 0; j < fftSize; ++j)
        detFrame[(size_t) j] = detFifo[(size_t) ((fifoPos + j) & mask)];

    // Fundamental, for body protection and for spacing the envelope. A voice's
    // pitch moves slowly enough that every other frame (~21 ms) is plenty.
    const float hopSeconds = (float) hopSize / (float) sampleRate;
    if ((frameCounter & 1) == 0)
        lastPitch = pitchTracker.analyse (detFrame.data());

    const bool voiced = lastPitch.clarity > 0.7f && lastPitch.frequency >= 80.0f && lastPitch.frequency <= 500.0f;

    if (voiced)
        protectedPitch = protectedPitch > 0.0f ? protectedPitch * std::sqrt (lastPitch.frequency / protectedPitch)
                                               : lastPitch.frequency;

    const float protectCoeff = std::exp (-hopSeconds / (voiced ? 0.01f : 0.15f));
    protectAmount = (voiced ? 1.0f : 0.0f) + (protectAmount - (voiced ? 1.0f : 0.0f)) * protectCoeff;
    if (protectAmount < 0.01f)
        protectedPitch = 0.0f;

    // Detector spectrum. When it is the average of the processed channels the
    // FFT is linear, so it is just the average of their spectra: no extra FFT.
    if (detectorIsMainMix)
    {
        const float norm = 1.0f / (float) juce::jmax (1, numChannels);
        for (int k = 0; k < numBins; ++k)
        {
            std::complex<float> sum;
            for (int ch = 0; ch < numChannels; ++ch)
                sum += spectra[(size_t) ch][(size_t) k];
            detSpectrum[(size_t) k] = sum * norm;
        }
    }
    else
    {
        for (int j = 0; j < fftSize; ++j)
            timeData[(size_t) j] = { detFrame[(size_t) j] * window[(size_t) j], 0.0f };
        fft->perform (timeData.data(), freqData.data(), false);
        std::copy (freqData.begin(), freqData.begin() + numBins, detSpectrum.begin());
    }

    // Level per bin, calibrated so a full-scale sine reads 0 dBFS.
    const float calibration = 20.0f * std::log10 (juce::MathConstants<float>::pi / (float) fftSize);
    for (int k = 0; k < numBins; ++k)
        levelDb[(size_t) k] = 10.0f * std::log10 (std::norm (detSpectrum[(size_t) k]) + 1.0e-20f) + calibration;
}

void ResonanceSuppressor::computeCuts()
{
    const float detail = juce::jlimit (0.0f, 1.0f, params.detail);
    const float hopSeconds = (float) hopSize / (float) sampleRate;
    const float depthScale = juce::jmax (0.0f, params.depth) / 10.0f;
    const float maxCut = juce::jmax (0.0f, params.maxCutDb);

    // 1. Peak-following envelope: wide enough to bridge the gaps between
    //    harmonics, so the harmonics themselves sit on the envelope. At low
    //    frequencies the window narrows (±25%) so single harmonics stay
    //    resolved and a boomy one can still be caught.
    const float bridgeHz = protectedPitch > 0.0f ? juce::jmax (120.0f, 0.75f * protectedPitch) : 250.0f;
    maxPeakBins (levelDb.data(), scratchA.data(), juce::jmax (1, juce::roundToInt (bridgeHz / binHz)));

    // 2. ...then smoothed over log-frequency. Lower Detail = wider window, so
    //    broader bumps count as resonances too. Two clipped passes keep a
    //    resonance from lifting the very envelope it is measured against.
    const float envelopeWidth = lerp (1.6f, 0.4f, detail);
    smoothLogFrequency (scratchA.data(), envelope.data(), envelopeWidth);
    for (int pass = 0; pass < 2; ++pass)
    {
        for (int k = 0; k < numBins; ++k)
            scratchB[(size_t) k] = juce::jmin (scratchA[(size_t) k], envelope[(size_t) k] + 1.0f);
        smoothLogFrequency (scratchB.data(), envelope.data(), envelopeWidth);
    }

    // 3–4. Excess above threshold → target cut.
    if (! juce::exactlyEqual (params.focusLowHz, focusMaskLow) || ! juce::exactlyEqual (params.focusHighHz, focusMaskHigh))
    {
        focusMaskLow = params.focusLowHz;
        focusMaskHigh = params.focusHighHz;
        for (int k = 0; k < numBins; ++k)
            focusMask[(size_t) k] = bandMask (binFreq[(size_t) k], focusMaskLow, focusMaskHigh, 0.5f);
    }

    const float threshold = 1.0f + 3.0f * detail;
    const float knee = params.hard ? 0.0f : 6.0f;
    const float averageCoeff = 1.0f - std::exp (-hopSeconds / 2.0f);

    for (int k = 0; k < numBins; ++k)
    {
        float excess = levelDb[(size_t) k] - envelope[(size_t) k];

        if (params.glareFocus && excess > 0.0f)
            excess *= glareWeight[(size_t) k];

        float bandThreshold = threshold;
        if (! params.hard)
        {
            auto& average = excessAverage[(size_t) k];
            average += (juce::jmax (0.0f, excess) - average) * averageCoeff;
            bandThreshold += 0.6f * average;
        }

        const float over = excess - bandThreshold;
        float shaped;
        if (knee <= 0.0f)
            shaped = juce::jmax (0.0f, over);
        else if (over <= -0.5f * knee)
            shaped = 0.0f;
        else if (over < 0.5f * knee)
            shaped = (over + 0.5f * knee) * (over + 0.5f * knee) / (2.0f * knee);
        else
            shaped = over;

        scratchB[(size_t) k] = juce::jmin (maxCut, depthScale * shaped) * focusMask[(size_t) k];
    }

    // 5. Spread each cut into a smooth bell: widen (max) then blur (mean).
    const float spread = lerp (1.0f / 3.0f, 1.0f / 10.0f, detail);
    maxLogFrequency (scratchB.data(), scratchA.data(), spread);
    smoothLogFrequency (scratchA.data(), cutTarget.data(), spread);

    // 6. Attack/release per bin.
    const float attack = std::exp (-hopSeconds / (juce::jmax (0.5f, params.attackMs) * 0.001f));
    const float release = std::exp (-hopSeconds / (juce::jmax (5.0f, params.releaseMs) * 0.001f));

    for (int k = 0; k < numBins; ++k)
    {
        const float target = cutTarget[(size_t) k];
        auto& current = cutSmoothed[(size_t) k];
        current = target + (current - target) * (target > current ? attack : release);
    }

    // 7. Body protection, applied last so neither smoothing stage can leak
    //    a neighbouring cut back onto the fundamental. Fully protected across
    //    the analysis window's main lobe (where f0's energy actually lands),
    //    then a 1/6-octave raised-cosine fade.
    const bool protect = params.bodyProtect && protectedPitch > 0.0f && protectAmount > 0.0f;
    const float flatOctaves = protect ? juce::jmax (1.0f / 6.0f, std::log2 (1.0f + 2.5f * binHz / protectedPitch)) : 0.0f;
    constexpr float fadeOctaves = 1.0f / 6.0f;
    const float pitchOctave = protect ? std::log2 (protectedPitch) : 0.0f;

    for (int k = 0; k < numBins; ++k)
    {
        float cut = cutSmoothed[(size_t) k];

        if (protect && binFreq[(size_t) k] > 0.0f)
        {
            const float d = std::abs (binOctave[(size_t) k] - pitchOctave) - flatOctaves;
            const float shield = d <= 0.0f ? 1.0f
                               : d >= fadeOctaves ? 0.0f
                               : 0.5f * (1.0f + std::cos (juce::MathConstants<float>::pi * d / fadeOctaves));
            cut *= 1.0f - protectAmount * shield;
        }

        cutFinal[(size_t) k] = cut;
        gains[(size_t) k] = juce::Decibels::decibelsToGain (-cut, -200.0f);
    }
}

void ResonanceSuppressor::publishDisplay()
{
    const float ratio = std::pow (DeharshDisplay::maxHz / DeharshDisplay::minHz, 0.5f / (float) (DeharshDisplay::numPoints - 1));

    for (int p = 0; p < DeharshDisplay::numPoints; ++p)
    {
        const float hz = DeharshDisplay::frequencyOf (p);
        const int lo = juce::jlimit (0, numBins - 1, (int) std::floor (hz / ratio / binHz));
        const int hi = juce::jlimit (lo, numBins - 1, (int) std::ceil (hz * ratio / binHz));

        float level = -120.0f, cut = 0.0f;
        for (int k = lo; k <= hi; ++k)
        {
            level = juce::jmax (level, levelDb[(size_t) k]);
            cut = juce::jmax (cut, cutFinal[(size_t) k]);
        }

        display.levelDb[(size_t) p].store (level, std::memory_order_relaxed);
        display.cutDb[(size_t) p].store (cut, std::memory_order_relaxed);
    }

    display.pitchHz.store (params.bodyProtect ? protectedPitch : 0.0f, std::memory_order_relaxed);
}

//==============================================================================
void ResonanceSuppressor::smoothLogFrequency (const float* src, float* dst, float widthOctaves)
{
    prefix[0] = 0.0f;
    for (int k = 0; k < numBins; ++k)
        prefix[(size_t) k + 1] = prefix[(size_t) k] + src[k];

    const float below = std::pow (2.0f, -0.5f * widthOctaves);
    const float above = std::pow (2.0f, 0.5f * widthOctaves);

    for (int k = 0; k < numBins; ++k)
    {
        const int lo = juce::jlimit (0, numBins - 1, (int) std::floor ((float) k * below));
        const int hi = juce::jlimit (lo, numBins - 1, (int) std::ceil ((float) k * above));
        dst[k] = (prefix[(size_t) hi + 1] - prefix[(size_t) lo]) / (float) (hi - lo + 1);
    }
}

void ResonanceSuppressor::maxLogFrequency (const float* src, float* dst, float widthOctaves)
{
    const float below = std::pow (2.0f, -0.5f * widthOctaves);
    const float above = std::pow (2.0f, 0.5f * widthOctaves);
    const int n = numBins;

    slidingMax (src, dst, n, queue, [=] (int k)
    {
        const int lo = juce::jlimit (0, n - 1, (int) std::floor ((float) k * below));
        const int hi = juce::jlimit (lo, n - 1, (int) std::ceil ((float) k * above));
        return std::pair { lo, juce::jmax (hi, k) };
    });
}

void ResonanceSuppressor::maxPeakBins (const float* src, float* dst, int maxHalfWidth)
{
    const int n = numBins;
    slidingMax (src, dst, n, queue, [=] (int k)
    {
        const int halfWidth = juce::jlimit (1, maxHalfWidth, juce::roundToInt (0.25f * (float) k));
        return std::pair { juce::jmax (0, k - halfWidth), juce::jmin (n - 1, k + halfWidth) };
    });
}

} // namespace linen
