#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "ui/LinenLookAndFeel.h"
#include "ui/LinenWeave.h"
#include "ui/SpectrumDisplay.h"

/** Everything is laid out once on a fixed design canvas and scaled as a whole,
    so the window can be resized without any element reflowing. */
class LinenAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    static constexpr int designWidth = 980;
    static constexpr int designHeight = 620;

    explicit LinenAudioProcessorEditor (LinenAudioProcessor&);
    ~LinenAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    /** The fixed-size canvas holding every control. */
    class Canvas final : public juce::Component
    {
    public:
        explicit Canvas (LinenAudioProcessor&);

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
        using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

        void attach (linen::LinenKnob&, const char* paramId, const juce::String& tooltip);
        void attach (linen::LinenPill&, const char* paramId, const juce::String& tooltip);
        void paintHeader (juce::Graphics&);
        void updateDimming (double timestampSeconds);

        LinenAudioProcessor& processor;
        linen::LinenWeave weave;

        linen::SpectrumDisplay display;

        linen::LinenSegmented mode;
        linen::LinenPill glare { "Glare" }, body { "Body" }, delta { "Delta" }, sidechain { "Sidechain" };

        linen::LinenKnob depth { "Depth", 78.0f }, detail { "Detail", 58.0f }, attack { "Attack", 58.0f },
                         release { "Release", 58.0f }, maxCut { "Max cut", 58.0f };
        linen::LinenKnob input { "Input", 58.0f }, mix { "Mix", 58.0f }, output { "Output", 58.0f };

        std::vector<std::unique_ptr<SliderAttachment>> sliderAttachments;
        std::vector<std::unique_ptr<ButtonAttachment>> buttonAttachments;

        // Delta dims everything but De-harsh so the ear (and eye) stay on it.
        float dim = 1.0f;
        double lastTick = 0.0;
        juce::VBlankAttachment vblank;
    };

    linen::LinenLookAndFeel lookAndFeel;
    Canvas canvas;
    juce::TooltipWindow tooltips { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LinenAudioProcessorEditor)
};
