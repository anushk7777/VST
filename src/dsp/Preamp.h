#pragma once

#include <juce_dsp/juce_dsp.h>

#include "Biquad.h"

namespace linen
{

/** A 1073-style class-A microphone preamp, run at 4x oversampling.

      input transformer → class-A gain stage → output transformer

    Transformers: magnetic flux is the integral of the voltage, so the model
    integrates (leaky, about -6 dB/oct above ~90 Hz), saturates the flux, then
    differentiates back. Below saturation it is exactly transparent; when
    driven, low notes thicken first and the mids stay clean.

    Class-A stage: an asymmetric (biased) soft curve, mostly 2nd harmonic. The
    bias leans further as the vocal gets louder (2 ms / 90 ms follower,
    measured against a belted -6 dBFS peak), so belted notes bloom and quiet
    tails relax. Small-signal gain is always 1, so Drive changes colour, not
    loudness.

    Drive is calibrated for a normally recorded vocal, the way a console is
    gain-staged: how hard both stages are hit rises evenly in dB across the
    knob. At 30% a belted -6 dBFS note carries about -55 dB of 2nd harmonic
    (felt, not heard); at 100% about -30 dB (warm, never fuzz).

    Drive 0 is a bit-exact bypass (latency-matched). */
class Preamp
{
public:
    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset();

    void setDrive (float drive01) { drive.setTargetValue (juce::jlimit (0.0f, 1.0f, drive01)); }

    int getLatencySamples() const noexcept { return latency; }

    void process (juce::AudioBuffer<float>& buffer);

private:
    struct Transformer
    {
        float k = 0.0f, leak = 0.0f;
        float flux = 0.0f, lastSaturated = 0.0f;

        /** fluxGain: how hard the core is driven (the flux that reaches its knee). */
        float process (float x, float fluxGain) noexcept;
        void reset() noexcept { flux = lastSaturated = 0.0f; }
    };

    struct ChannelState
    {
        Transformer inputIron, outputIron;
        float envelope = 0.0f;
        Biquad outputLowPass, dcBlock;
    };

    double sampleRate = 48000.0, overSampleRate = 192000.0;
    int numChannels = 0;
    int latency = 0;

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    std::vector<ChannelState> channels;
    float envAttack = 0.0f, envRelease = 0.0f;

    juce::SmoothedValue<float> drive { 0.0f };
    std::vector<float> driveRamp, classARamp, ironRamp;

    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> dryDelay;
    juce::AudioBuffer<float> dry;
};

} // namespace linen
