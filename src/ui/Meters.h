#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace linen
{

/** A quiet live meter, refreshed on VBlank. Bar: a thin well that fills from
    the top with champagne to terracotta (gain reduction hangs down, like the silk veil).
    Dot: a small light that glows as the value rises (e.g. sibilance). */
class LinenMeter final : public juce::Component,
                         public juce::SettableTooltipClient
{
public:
    enum class Style { bar, dot };

    LinenMeter (Style style, std::function<float()> source, float fullScale);

    void paint (juce::Graphics&) override;

private:
    void tick (double timestampSeconds);

    Style style;
    std::function<float()> source;
    float fullScale;
    float shown = 0.0f;
    double lastTick = 0.0;
    juce::VBlankAttachment vblank;
};

/** The ADC's needle meter: a backlit cream face with the classic VU scale,
    the needle resting at 0 and swinging left as the compressor holds the
    voice, with VU ballistics (about 300 ms to settle). The backlight warms
    toward cobalt while it works, and two LED bars under the face show how the
    TUBE and OPTO share the reduction. */
class LinenVuMeter final : public juce::Component,
                           public juce::SettableTooltipClient
{
public:
    LinenVuMeter (std::function<float()> totalDb, std::function<float()> tubeDb, std::function<float()> optoDb);

    void paint (juce::Graphics&) override;

private:
    void tick (double timestampSeconds);

    std::function<float()> total, tube, opto;
    float needle = 0.0f, velocity = 0.0f;   // dB of gain reduction
    float tubeShown = 0.0f, optoShown = 0.0f;
    double lastTick = 0.0;
    juce::VBlankAttachment vblank;
};

} // namespace linen
