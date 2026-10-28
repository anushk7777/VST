#pragma once

#include <juce_dsp/juce_dsp.h>

#include "Air.h"
#include "Eq1073.h"
#include "Preamp.h"
#include "ResonanceSuppressor.h"
#include "SilkCompressor.h"

namespace linen
{

/** Plain values the engine runs with. The plugin fills this from its
    parameters every block; tests and the render tool fill it directly. */
struct EngineParams
{
    float inputGainDb  = 0.0f;
    float outputGainDb = 0.0f;
    float mix          = 1.0f;  // 0 = dry only, 1 = processed only

    float preampDrive = 0.0f;       // 0..1; 0 = preamp bypassed
    EqParams eq;

    DeharshParams deharsh;
    bool deharshSidechain = false;  // detect from the sidechain input instead of the vocal

    CompParams comp;
    AirParams air;

    float silk = 0.5f;  // the Silk macro; 0.5 = settings as they are
};

/** The whole Linen signal chain, independent of the plugin wrapper.

    Input gain → Preamp → EQ → De-harsh → Silk comp → Air → Output gain → Mix

    The dry signal for Mix is taken before the input gain and delayed by the
    chain's latency, so dry and wet stay sample-aligned at any Mix setting. */
class Engine
{
public:
    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset();

    void setParams (const EngineParams& newParams);

    /** Processes the buffer in place, at any length. Channels beyond the
        count given to prepare() are left untouched. sidechain, if given,
        must be at least as long as buffer. */
    void process (juce::AudioBuffer<float>& buffer, const juce::AudioBuffer<float>* sidechain = nullptr);

    /** Total latency of the wet path in samples. Constant after prepare(). */
    int getLatencySamples() const noexcept { return latencySamples; }

    DeharshDisplay& getDeharshDisplay() noexcept { return deharsh.getDisplay(); }
    float getCompReductionDb() const noexcept { return comp.getGainReductionDb(); }
    float getSibilance() const noexcept { return air.getSibilance(); }

    /** Adds a pure delay to the wet path. Only for tests: it stands in for
        a latent module so the dry-path compensation can be checked. */
    void setExtraWetLatencyForTesting (int samples);

private:
    void processChunk (juce::AudioBuffer<float>& buffer, const juce::AudioBuffer<float>* sidechain, int offset);
    void updateLatency();

    double sampleRate = 44100.0;
    int maxBlock = 0;
    int numChans = 0;

    EngineParams params;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> inputGain { 1.0f }, outputGain { 1.0f };
    juce::SmoothedValue<float> mix { 1.0f };

    Preamp preamp;
    Eq1073 eq;
    ResonanceSuppressor deharsh;
    std::vector<float> detector;
    SilkCompressor comp;
    Air air;

    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> dryDelay;
    juce::AudioBuffer<float> dryBuffer;

    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> testWetDelay;
    int testWetLatency = 0;

    int latencySamples = 0;
};

} // namespace linen
