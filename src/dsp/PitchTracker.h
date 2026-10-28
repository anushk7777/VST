#pragma once

#include <juce_dsp/juce_dsp.h>

namespace linen
{

/** Finds the fundamental of a (mostly) monophonic voice in one analysis frame,
    using the McLeod normalised-square-difference method. The autocorrelation
    comes from an FFT, so it is cheap enough to run every STFT hop. */
class PitchTracker
{
public:
    struct Result
    {
        float frequency = 0.0f;  // Hz, 0 if nothing found
        float clarity = 0.0f;    // 0..1, how periodic the frame is
    };

    /** frameSize must be a power of two. Allocates; call off the audio thread. */
    void prepare (double sampleRate, int frameSize, float minHz = 70.0f, float maxHz = 600.0f);

    /** Analyses frameSize samples (unwindowed). Real-time safe. */
    Result analyse (const float* samples);

private:
    double sampleRate = 48000.0;
    int frameSize = 0;
    int minLag = 0, maxLag = 0;

    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> work, nsdf;
};

} // namespace linen
