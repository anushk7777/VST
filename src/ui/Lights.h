#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ListenSession.h"

namespace linen
{

/** A jewel lamp, like the pilot lights on vintage hardware: dark glass when
    its module is idle, glowing (with a soft bloom) as it works. Animated on
    VBlank; quick to light, slow to fade. */
class LinenLamp final : public juce::Component,
                        public juce::SettableTooltipClient
{
public:
    LinenLamp (std::function<float()> brightness, juce::Colour colour);

    void paint (juce::Graphics&) override;

    /** Draws a lamp of the given diameter at centre (shared with other controls). */
    static void drawJewel (juce::Graphics&, juce::Point<float> centre, float diameter, float level, juce::Colour colour);

private:
    std::function<float()> source;
    juce::Colour colour;
    float level = 0.0f;
    double lastTick = 0.0;
    juce::VBlankAttachment vblank;
};

/** The AI Listen button: a pill with a lamp ring that pulses cobalt while
    listening, fills with progress, and glows when it has set Linen up. */
class ListenButton final : public juce::Component,
                           public juce::SettableTooltipClient
{
public:
    ListenButton (ListenSession& session, std::function<void()> start);

    /** Called once each time a session finishes (done or failed), with its message. */
    std::function<void (bool succeeded, const juce::String& message)> onFinished;

    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { hovering = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hovering = false; repaint(); }
    void mouseUp (const juce::MouseEvent&) override;

private:
    void tick (double timestampSeconds);

    ListenSession& session;
    std::function<void()> start;
    ListenSession::State lastState = ListenSession::State::idle;
    bool hovering = false;
    double now = 0.0, finishedAt = -100.0;
    juce::VBlankAttachment vblank;
};

} // namespace linen
