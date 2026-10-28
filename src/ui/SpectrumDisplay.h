#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <melatonin_blur/melatonin_blur.h>

#include "dsp/ResonanceSuppressor.h"

namespace linen
{

/** The cream display well: the vocal's spectrum as a soft line, and De-harsh's
    gain reduction as a "silk veil" hanging from the top. Two handles set the
    focus range. Animated on the display's refresh (VBlank), never on a timer. */
class SpectrumDisplay final : public juce::Component,
                              public juce::SettableTooltipClient
{
public:
    SpectrumDisplay (DeharshDisplay& source, juce::RangedAudioParameter& focusLow, juce::RangedAudioParameter& focusHigh);

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    enum class Handle { none, low, high };

    void tick (double timestampSeconds);
    float xForHz (float hz) const;
    float hzForX (float x) const;
    float yForLevel (float db) const;
    float yForCut (float db) const;
    Handle handleAt (juce::Point<float> p) const;

    DeharshDisplay& source;
    juce::RangedAudioParameter& focusLowParam;
    juce::RangedAudioParameter& focusHighParam;
    juce::ParameterAttachment focusLowAttachment, focusHighAttachment;
    float focusLowHz = 120.0f, focusHighHz = 18000.0f;

    std::array<float, DeharshDisplay::numPoints> level {}, cut {};
    float pitchHz = 0.0f, pitchAlpha = 0.0f;
    double lastTick = 0.0;

    juce::Rectangle<float> plot;
    Handle hovered = Handle::none, dragging = Handle::none;

    melatonin::InnerShadow wellShadow {
        { juce::Colour (0xff221e1c).withAlpha (0.16f), 12, { 0, 4 } },
        { juce::Colours::white.withAlpha (0.55f), 2, { 0, -1 } }
    };

    juce::VBlankAttachment vblank;
};

} // namespace linen
