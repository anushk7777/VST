#pragma once

#include <juce_dsp/juce_dsp.h>

namespace linen
{

/** What the Listen button heard, and what it suggests. Values are in the
    plugin's parameter units (choice parameters as switch indices). */
struct AssistResult
{
    bool valid = false;
    juce::String summary;   // one or two plain sentences for the GUI

    // Measurements.
    float levelDb = -100.0f;        // RMS of the sung parts
    float dynamicsDb = 0.0f;        // spread between loud and soft phrases (P90 - P10)
    float pitchHz = 0.0f;           // median sung pitch, 0 if unknown
    float mudDb = 0.0f;             // 200-500 Hz vs a smooth vocal (+ = muddy)
    float boomDb = 0.0f;            // 63-125 Hz (+ = boomy)
    float harshDb = 0.0f;           // 2.5-5 kHz edge (+ = harsh)
    float resonanceDb = 0.0f;       // how peaky that edge is (narrow rings)
    float dullDb = 0.0f;            // 8-16 kHz (+ = dull, - = bright)
    float sibilanceDb = 0.0f;       // how far S's stick out above the vowels

    // Suggestions.
    float inputGainDb = 0.0f;
    float preampDrive = 0.25f;
    int highPass = 2;               // index: Off, 50, 80, 160, 300 Hz
    int lowFreq = 2;                // index: Off, 35, 60, 110, 220 Hz
    float lowGainDb = 0.0f;
    int midFreq = 4;                // index: Off, 360, 700, 1.6k, 3.2k, 4.8k, 7.2k
    float midGainDb = 0.0f;
    float highGainDb = 0.0f;
    float deharshDepth = 5.0f;
    float deharshDetail = 0.5f;
    float compThresholdDb = -18.0f;
    float compRatio = 2.0f;
    float compCotton = 0.5f;
    float airAmount = 0.3f;
    float airDeEss = 0.5f;
};

/** The "AI" behind the Listen button: measures a few seconds of a vocal
    against the tonal balance and dynamics of a smooth, finished pop vocal,
    and turns the differences into settings. Plain signal analysis, runs
    locally, no training data. Call off the audio thread. */
struct AutoAssist
{
    /** Minimum seconds of actual singing needed for a reliable read. */
    static constexpr double minimumSeconds = 2.0;

    static AssistResult analyse (const float* mono, int numSamples, double sampleRate);
};

} // namespace linen
