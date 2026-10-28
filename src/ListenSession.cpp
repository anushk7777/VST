#include "ListenSession.h"

ListenSession::ListenSession (std::function<void (const linen::AssistResult&)> callback)
    : onResult (std::move (callback))
{
}

ListenSession::~ListenSession()
{
    capturing = false;
    stopTimer();
}

void ListenSession::start (double newSampleRate)
{
    capturing = false;   // the audio thread stops writing before the FIFO is reset
    fifo.reset();

    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    captured.clear();
    captured.reserve ((size_t) (sampleRate * 32.0));
    frameFill = 0;
    framePower = activeSeconds = 0.0;
    startedAt = juce::Time::getMillisecondCounterHiRes() * 0.001;
    progress = 0.0f;
    message = "Listening... play the vocal.";
    state = State::listening;

    capturing = true;
    startTimerHz (20);
}

void ListenSession::cancel()
{
    capturing = false;
    stopTimer();
    progress = 0.0f;
    message.clear();
    state = State::idle;
}

void ListenSession::push (const juce::AudioBuffer<float>& input, int numChannels)
{
    if (! capturing.load (std::memory_order_relaxed))
        return;

    const int n = input.getNumSamples();
    const int chans = juce::jmin (numChannels, input.getNumChannels());
    if (n == 0 || chans == 0)
        return;

    int start1, size1, start2, size2;
    fifo.prepareToWrite (n, start1, size1, start2, size2);
    const float norm = 1.0f / (float) chans;

    const auto write = [&] (int ringStart, int count, int offset)
    {
        for (int i = 0; i < count; ++i)
        {
            float x = 0.0f;
            for (int ch = 0; ch < chans; ++ch)
                x += input.getSample (ch, offset + i);
            ring[(size_t) (ringStart + i)] = x * norm;
        }
    };

    write (start1, size1, 0);
    write (start2, size2, size1);
    fifo.finishedWrite (size1 + size2);   // if it was full, the rest is simply dropped
}

void ListenSession::timerCallback()
{
    int start1, size1, start2, size2;
    fifo.prepareToRead (fifo.getNumReady(), start1, size1, start2, size2);

    const auto take = [this] (int from, int count)
    {
        const int frame = juce::roundToInt (0.05 * sampleRate);
        for (int i = 0; i < count; ++i)
        {
            const float x = ring[(size_t) (from + i)];
            captured.push_back (x);

            // Count only real singing (50 ms frames above -50 dBFS) toward the goal.
            framePower += (double) x * x;
            if (++frameFill == frame)
            {
                if (framePower / frame > 1.0e-5)
                    activeSeconds += frame / sampleRate;
                frameFill = 0;
                framePower = 0.0;
            }
        }
    };

    take (start1, size1);
    take (start2, size2);
    fifo.finishedRead (size1 + size2);

    progress = (float) juce::jlimit (0.0, 1.0, activeSeconds / targetSeconds);

    const double elapsed = juce::Time::getMillisecondCounterHiRes() * 0.001 - startedAt;
    if (activeSeconds >= targetSeconds || elapsed >= timeoutSeconds || captured.size() >= (size_t) (sampleRate * 30.0))
        finish();
}

void ListenSession::finish()
{
    capturing = false;
    stopTimer();

    if (captured.empty())
    {
        message = "No audio arrived. Press play in your DAW, then Listen again.";
        state = State::failed;
        return;
    }

    const auto result = linen::AutoAssist::analyse (captured.data(), (int) captured.size(), sampleRate);
    message = result.summary;

    if (result.valid)
    {
        onResult (result);
        progress = 1.0f;
        state = State::done;
    }
    else
    {
        state = State::failed;
    }
}
