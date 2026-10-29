#pragma once

#include <juce_dsp/juce_dsp.h>

#include "Biquad.h"

namespace linen
{

struct AirParams
{
    float amount = 0.0f;   // 0..1: High Air, up to +10 dB on the quiet highs above ~9 kHz
    float mid = 0.0f;      // 0..1: Mid Air, up to +10 dB on the quiet highs above ~3 kHz
    float deEss = 0.0f;    // 0..1: up to -6 dB on S and T sounds
    float scorch = 0.0f;   // 0..1 (from Pain): damp the air, darker and dustier, like old tape
    float loom = 0.5f;     // 0..1: more High Air where De-harsh cut the top (Silk loom)
};

/** Air in the manner of the classic "Dolby A trick": only the encoder's
    high bands, added back to the voice.

    Each band (Mid: a 3 kHz high-pass, High: a 9 kHz high-pass) feeds a side
    signal whose gain is high while the band is quiet next to the voice and
    falls away as it gets loud (its level is held near a ceiling set by the
    voice's own level), and the side is added to the dry voice. So a dull
    top and the quiet detail up there (breath, the edges of words) open by
    up to 10 dB, while a top that is already bright, or an S, is barely
    lifted. Relative to the voice, so it works the same at any level. Nothing is clipped or squared, so it adds
    no grain. A sibilance detector closes the air on S and T sounds, and
    De-ess turns that into a gentle cut. At 0 it is an exact pass-through. */
class Air
{
public:
    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset();
    void setParams (const AirParams& p) { params = p; }
    void process (juce::AudioBuffer<float>& buffer);

    /** Silk loom: how deep De-harsh is cutting above 2 kHz right now (dB). */
    void setHarshnessCut (float cutDb) noexcept { harshCutDb = cutDb; }

    /** How strongly the current sound reads as sibilance, 0..1 (for the GUI). */
    float getSibilance() const noexcept { return sibilanceMeter.load (std::memory_order_relaxed); }

    /** The lift (dB) a steady band gets when it sits this far under the
        voice, for a band set to liftDb at its quietest (the static curve). */
    static float liftDbAt (float liftDb, float bandUnderVoiceDb);

private:
    void updateControl();

    double sampleRate = 48000.0;
    int numChannels = 0;
    AirParams params;
    float harshCutDb = 0.0f;

    // Sibilance detector (mono).
    Biquad vowelHp, vowelLp, sibHp, sibLp;
    float vowelPower = 0.0f, sibPower = 0.0f, powerCoeff = 0.0f;
    float sibilance = 0.0f;
    float sibAttack = 0.0f, sibRelease = 0.0f;
    std::atomic<float> sibilanceMeter { 0.0f };

    // The two air bands: detectors (mono, linked) and per-channel band filters.
    struct Band
    {
        Biquad detector;
        std::vector<Biquad> filters;
        float envelope = 0.0f;  // band power
        float gain = 0.0f;      // side gain right now, gliding
        float k = 0.0f;         // side gain while quiet: 10^(lift/20) - 1
    };
    Band midBand, highBand;
    float envAttack = 0.0f, envRelease = 0.0f, voiceRelease = 0.0f, gainGlide = 0.0f;
    float voiceEnvelope = 0.0f;   // the whole voice's power, the air's reference
    std::vector<float> midGain, highGain;   // per-sample side gains

    // De-ess cut and scorch: a high shelf, redesigned every 16 samples.
    std::vector<Biquad> shelves;
    float shelfDb = 0.0f;
};

} // namespace linen
