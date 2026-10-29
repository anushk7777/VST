#pragma once

#include <juce_dsp/juce_dsp.h>

#include "Biquad.h"

namespace linen
{

/** Settings of the Dua Wox compressor. */
struct CompParams
{
    // TUBE slot: the catch.
    bool tubeOn = true;
    float thresholdDb = 0.0f;    // -48..0: where the tube starts to lean in
    float ratio = 1.0f;          // 1..6: how firmly it leans (1 = off); see tubeRatioFor()
    float attackMs = 12.0f;      // 1..80
    float releaseMs = 60.0f;     // 20..400: the fast half of the auto release

    // OPTO slot: the leveller.
    bool optoOn = true;
    float optoReduction = 0.0f;  // 0..1: peak reduction (0 = off)

    // Shared.
    float warmth = 0.0f;         // 0..1: tube line stage, plus low-mid bloom and a softer top while compressing
    float weave = 0.5f;          // 0..1: how far Warmth's colour deepens the longer the voice is held down
    bool autoMakeup = true;
    float mix = 1.0f;            // parallel blend
};

/** The Dua Wox compressor: two soft cells in one feedback
    loop, built from how three classic circuits behave. Both cells listen to
    the finished output, so they lean on the voice rather than clamp it, and
    they share the work instead of stacking up: when the OPTO is already
    holding the line, the TUBE lets it.

    TUBE (the catch), after a variable-mu tube cell with a diode-bridge
    style recovery:
      - Gain follows a remote-cutoff tube's curve as the sidechain re-biases
        it: a 12 dB knee, and a ratio that firms up gently with level,
        never much past 3:1.
      - Auto release with two time constants: a fast one (the Release knob)
        and a slow one that only charges while the voice is held down for a
        while, so single peaks recover quickly and long loud lines slowly.
      - The tube's push-pull grid curve adds a little 3rd harmonic, and a
        slight bridge imbalance a little 2nd, both only as it reduces gain.

    OPTO (the leveller), after an electro-optical cell:
      - The sidechain lights an electroluminescent panel (fast rise, a few
        ms of persistence) shining on a photoconductive cell.
      - The cell's conductance rises in about 10 ms and falls in two parts,
        about half within 60-100 ms, then the rest over 0.5-5 s: trapped
        charge that lingers the longer the cell has been lit (its memory).
      - The cell is the shunt leg of a divider: a natural soft knee, at most 3:1.
      - The cell moves in dB of reduction, as its response speeds up in
        brighter light, so a phrase's loud onset is caught, not swallowed.

    WARMTH: a tube line stage (mostly 2nd harmonic), plus a touch of low-mid
    bloom and a softer top in proportion to the gain reduction.

    WEAVE: the longer the singer pushes (seconds of reduction), the further
    the line stage's bias leans, so its 2nd harmonic deepens a few dB, then
    eases back over seconds when the line relaxes.

    BREATH GATE: a breath or a consonant (energy mostly above 4.5 kHz) holds
    both cells where they are, so breaths are neither squashed nor pulled up.

    Gains are applied at the base rate; the harmonic colour is computed at 2x
    as a difference added to the latency-aligned signal. With no gain
    reduction and Warmth at 0 the output is the input, delayed, bit for bit. */
class AdcCompressor
{
public:
    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset();
    void setParams (const CompParams& p) { params = p; }
    void process (juce::AudioBuffer<float>& buffer);

    /** Constant after prepare(): the oversampled colour path's latency. */
    int getLatencySamples() const noexcept { return latency; }

    /** Gain reduction right now, in positive dB (for the meters). */
    float getGainReductionDb() const noexcept { return tubeMeter.load (std::memory_order_relaxed) + optoMeter.load (std::memory_order_relaxed); }
    float getTubeReductionDb() const noexcept { return tubeMeter.load (std::memory_order_relaxed); }
    float getOptoReductionDb() const noexcept { return optoMeter.load (std::memory_order_relaxed); }

    /** Steady-state gain reduction (positive dB) for a steady tone whose
        peak sits at inputDb, both cells together, before make-up. */
    static float staticReductionDb (const CompParams& p, float inputDb);

    /** The automatic make-up gain for these settings, in dB (0 with Auto gain off). */
    static float makeupDb (const CompParams& p);

    /** The ratio the TUBE really runs at, as its knee begins, for a Ratio
        knob setting (1..6); it firms up by about a third on the loudest
        notes. And the knob setting for a ratio (for typed-in values). */
    static float tubeRatioFor (float ratioKnob);
    static float ratioKnobFor (float tubeRatio);

private:
    void updateTexture (float reduction);

    double sampleRate = 48000.0;
    int numChannels = 0;
    int latency = 0;
    CompParams params;

    /** A 90-degree phase splitter: two chains of allpasses whose outputs,
        squared and summed, give a signal's instantaneous amplitude with no
        ripple. So the detector reacts as fast as the attack allows and has
        no memory of its own (an averaging detector remembers a phrase's
        unreduced onset and over-compresses after it). */
    struct PhaseSplitter
    {
        /** Returns the squared instantaneous amplitude of x. */
        double envelopeSquared (double x) noexcept;
        void reset() noexcept;

        struct Section
        {
            double c = 0.0, x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;
            double process (double x) noexcept
            {
                const double y = c * (x + y2) - x2;
                x2 = x1;
                x1 = x;
                y2 = y1;
                y1 = y;
                return y;
            }
        };

        std::array<Section, 4> a, b;
        double aDelayed = 0.0;
    };

    // Sidechain (mono, from the finished output): 100 Hz high-pass and a
    // gentle presence lift, as these cells hear more of the bright energy.
    Biquad sidechainHp, sidechainPresence;
    PhaseSplitter splitter;
    bool detecting = false;

    // Breath gate: how much of the sidechain sits above 4.5 kHz.
    Biquad breathHp;
    float breathPower = 0.0f, bodyPower = 0.0f, breathiness = 0.0f;
    float gatePowerCoeff = 0.0f, gateAttack = 0.0f, gateRelease = 0.0f;

    // Weave: how long the voice has been held down (0..1).
    float pushed = 0.0f, pushRise = 0.0f, pushFall = 0.0f;

    // TUBE state: gain reduction in dB, fast and slow halves of the recovery.
    float tubeFast = 0.0f, tubeSlow = 0.0f;
    float slowCharge = 0.0f, slowRelease = 0.0f;

    // OPTO state, in dB of reduction: panel light, the cell (fast part),
    // trapped charge (slow part); and how long it has been lit (its memory).
    float light = 0.0f, cell = 0.0f, trapped = 0.0f, exposure = 0.0f;
    float lightRise = 0.0f, lightFall = 0.0f, cellAttack = 0.0f, cellRelease = 0.0f;
    float trapCharge = 0.0f, exposureRise = 0.0f, exposureFall = 0.0f;

    juce::SmoothedValue<float> makeup, mix;
    std::vector<float> gainRamp, tubeColourRamp, reductionRamp;
    std::atomic<float> tubeMeter { 0.0f }, optoMeter { 0.0f };

    // Colour: harmonics at 2x, added as a difference so "no colour" is exact.
    // It keeps running a little after its sources stop, so its tail is never cut.
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    juce::AudioBuffer<float> compressed, colour, dry;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> alignDelay, dryDelay;
    std::vector<Biquad> colourDcBlock;
    int colourHold = 0, colourHoldSamples = 0;

    // Warmth's GR-coupled bloom and soft top, redesigned every 16 samples.
    std::vector<std::array<Biquad, 2>> texture;
    float textureAmount = 0.0f;
};

} // namespace linen
