#pragma once

#include <juce_dsp/juce_dsp.h>

#include "Air.h"
#include "Eq1073.h"
#include "PainEngine.h"
#include "Preamp.h"
#include "ResonanceSuppressor.h"
#include "SectionBypass.h"
#include "SilkCompressor.h"
#include "ToneShelves.h"

namespace linen
{

/** Plain values the engine runs with. The plugin fills this from its
    parameters every block; tests and the render tool fill it directly. */
struct EngineParams
{
    float inputGainDb  = 0.0f;
    float outputGainDb = 0.0f;
    float mix          = 1.0f;  // 0 = dry only, 1 = processed only

    ToneParams tone;                // shelves before the preamp

    float preampDrive = 0.0f;       // 0..1; 0 = preamp bypassed
    EqParams eq;

    DeharshParams deharsh;
    bool deharshSidechain = false;  // detect from the sidechain input instead of the vocal

    CompParams comp;
    AirParams air;

    float silk = 0.5f;  // the Silk macro; 0.5 = settings as they are
    float pain = 0.0f;  // the Pain macro, 0..1; 0 = off

    /** Section power switches. An "off" section is an exact, latency-aligned
        bypass, whatever its knobs say. */
    struct Sections
    {
        bool tone = true, preamp = true, deharsh = true, comp = true, air = true;
    } sections;

    /** Phase test: play Linen's output minus the untouched input. Silence
        means the chain changes nothing. */
    bool nullTest = false;
};

/** The whole Linen signal chain, independent of the plugin wrapper.

    Input gain → Tone → Preamp → EQ → Pain → De-harsh → Silk comp → Air → Output gain → Mix

    The dry signal for Mix is taken before the input gain and delayed by the
    chain's latency, so dry and wet stay sample-aligned at any Mix setting.
    Every section has a power switch (a click-free, latency-aligned bypass),
    and every neutral setting is bit-exact: with all of them neutral the
    output is the input, delayed by the latency, to the last bit. */
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
    float getPainStress() const noexcept { return painEngine.getStressLevel(); }

    /** RMS of the incoming signal over the last block (linear), for the lamps. */
    float getInputLevel() const noexcept { return inputLevel.load (std::memory_order_relaxed); }

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
    juce::SmoothedValue<float> mix { 1.0f }, nullAmount { 0.0f };

    ToneShelves tone;
    Preamp preamp;
    Eq1073 eq;
    PainEngine painEngine;
    ResonanceSuppressor deharsh;
    std::vector<float> detector;
    SilkCompressor comp;
    Air air;

    SectionBypass toneSwitch, preampSwitch, deharshSwitch, compSwitch, airSwitch;

    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> dryDelay;
    juce::AudioBuffer<float> dryBuffer;

    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> testWetDelay;
    int testWetLatency = 0;

    int latencySamples = 0;
    std::atomic<float> inputLevel { 0.0f };
};

} // namespace linen
