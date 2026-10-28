#pragma once

#include <juce_dsp/juce_dsp.h>

#include "PitchTracker.h"

namespace linen
{

struct DeharshParams
{
    float depth = 5.0f;          // 0..15; 10 removes the whole excess above threshold
    float detail = 0.5f;         // 0..1; higher = narrower, pickier cuts
    float attackMs = 5.0f;
    float releaseMs = 120.0f;
    float maxCutDb = 12.0f;      // hard cap per frequency
    float focusLowHz = 120.0f;   // detection range
    float focusHighHz = 18000.0f;
    bool hard = false;           // false = Soft (adaptive threshold, soft knee)
    bool glareFocus = true;      // lean toward the 2.5–5.5 kHz ear-canal region
    bool bodyProtect = true;     // never cut the sung fundamental
    bool delta = false;          // output only what is being removed
};

/** Smoothed spectrum and gain reduction at log-spaced points, for the GUI.
    Written by the audio thread once per STFT frame; readers may see a frame
    that is partly old and partly new, which is harmless for drawing. */
struct DeharshDisplay
{
    static constexpr int numPoints = 64;
    static constexpr float minHz = 20.0f, maxHz = 20000.0f;

    static float frequencyOf (int point)
    {
        return minHz * std::pow (maxHz / minHz, (float) point / (float) (numPoints - 1));
    }

    std::array<std::atomic<float>, numPoints> levelDb {};   // input level, dBFS
    std::array<std::atomic<float>, numPoints> cutDb {};     // gain reduction, positive dB
    std::atomic<float> pitchHz { 0.0f };                    // protected fundamental, 0 if none
};

/** Dynamic resonance suppressor ("De-harsh").

    Per STFT frame (sqrt-Hann analysis and synthesis, 75% overlap):
      1. Detector level per bin, in dB.
      2. Spectral envelope: a peak-following envelope (so harmonics sit on it)
         smoothed over a log-frequency window set by Detail.
      3. Excess of each bin above envelope + threshold. Soft mode raises the
         threshold where a bin is persistently high (that is the voice's own
         tone, not a resonance) and uses a soft knee.
      4. Cut = Depth × excess, capped at Max Cut, shaped by focus range, glare
         weighting and body protection, then smoothed in time (attack/release)
         and across frequency (so cuts are smooth bells, not ringing notches).
      5. Applied to every channel's spectrum with the same gains (stereo-linked).

    Latency is one FFT length. */
class ResonanceSuppressor
{
public:
    void prepare (double sampleRate, int numChannels);
    void reset();
    void setParams (const DeharshParams& p) { params = p; }

    int getLatencySamples() const noexcept { return fftSize; }
    int getFftSize() const noexcept { return fftSize; }

    /** Processes main in place. detector holds the signal to analyse (mono,
        same length): the main input or an external sidechain. Pass
        detectorIsMainSum = true when detector is the plain average of main's
        channels; its spectrum is then taken from theirs (one FFT fewer). */
    void process (juce::AudioBuffer<float>& main, const float* detector, bool detectorIsMainSum);

    DeharshDisplay& getDisplay() noexcept { return display; }

private:
    void processFrame();
    void forwardChannels();
    void analyseDetector();
    void computeCuts();
    void inverseChannels();
    void publishDisplay();

    // Mean of src over [k·2^(-w/2), k·2^(w/2)] for every bin (w in octaves).
    void smoothLogFrequency (const float* src, float* dst, float widthOctaves);
    // Running max of src over [k·2^(-w/2), k·2^(w/2)].
    void maxLogFrequency (const float* src, float* dst, float widthOctaves);
    // Running max over ±min(maxHalfWidth, 25% of k) bins.
    void maxPeakBins (const float* src, float* dst, int maxHalfWidth);

    double sampleRate = 48000.0;
    int numChannels = 0;
    int fftOrder = 11, fftSize = 2048, hopSize = 512, numBins = 1025;
    float binHz = 23.4f;

    DeharshParams params;

    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window;     // sqrt-Hann (periodic)
    float olaScale = 0.5f;

    // Circular FIFOs, fftSize long, sharing one write position.
    std::vector<std::vector<float>> inFifo, outFifo;
    std::vector<float> detFifo;
    int fifoPos = 0, hopCounter = 0;
    juce::uint32 frameCounter = 0;
    bool detectorIsMainMix = true;

    // Per-frame scratch, all sized in prepare().
    std::vector<std::complex<float>> timeData, freqData, detSpectrum;
    std::vector<std::vector<std::complex<float>>> spectra;  // per channel, bins 0..N/2
    std::vector<float> detFrame, levelDb, envelope, scratchA, scratchB;
    std::vector<float> excessAverage, cutTarget, cutSmoothed, cutFinal, gains, binFreq, prefix;
    std::vector<float> binOctave, glareWeight, focusMask;
    float focusMaskLow = -1.0f, focusMaskHigh = -1.0f;
    std::vector<int> queue;

    PitchTracker pitchTracker;
    PitchTracker::Result lastPitch;
    float protectedPitch = 0.0f, protectAmount = 0.0f;

    DeharshDisplay display;
};

} // namespace linen
