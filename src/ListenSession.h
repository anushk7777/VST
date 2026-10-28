#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/AutoAssist.h"

/** Drives the Listen button: captures the incoming vocal without touching
    the audio thread's timing (a lock-free FIFO), and once it has heard
    enough actual singing, analyses it on the message thread and hands the
    result back. */
class ListenSession final : private juce::Timer
{
public:
    enum class State { idle, listening, done, failed };

    static constexpr double targetSeconds = 6.0;     // of actual singing
    static constexpr double timeoutSeconds = 45.0;   // wall-clock

    explicit ListenSession (std::function<void (const linen::AssistResult&)> onResult);
    ~ListenSession() override;

    /** Message thread. */
    void start (double sampleRate);
    void cancel();

    /** Audio thread: offer the block's input (any number of channels). Real-time safe. */
    void push (const juce::AudioBuffer<float>& input, int numChannels);

    State getState() const noexcept { return state.load(); }
    float getProgress() const noexcept { return progress.load(); }   // 0..1
    juce::String getMessage() const { return message; }              // message thread

private:
    void timerCallback() override;
    void finish();

    std::function<void (const linen::AssistResult&)> onResult;

    juce::AbstractFifo fifo { 1 << 18 };
    std::vector<float> ring = std::vector<float> ((size_t) (1 << 18));
    std::atomic<bool> capturing { false };

    std::vector<float> captured;
    double sampleRate = 48000.0;
    double startedAt = 0.0;
    int frameFill = 0;
    double framePower = 0.0, activeSeconds = 0.0;

    std::atomic<State> state { State::idle };
    std::atomic<float> progress { 0.0f };
    juce::String message;
};
