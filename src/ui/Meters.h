#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace linen
{

/** A quiet live meter, refreshed on VBlank. Bar: a thin well that fills from
    the top with terracotta (gain reduction hangs down, like the silk veil).
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

} // namespace linen
