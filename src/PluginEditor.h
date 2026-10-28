#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "ui/LinenLookAndFeel.h"
#include "ui/LinenWeave.h"

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
        void paintHeader (juce::Graphics&);
        void paintDisplayWell (juce::Graphics&);

        using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;

        linen::LinenWeave weave;
        juce::Rectangle<int> displayArea;

        linen::LinenKnob input { "Input", 74.0f }, mix { "Mix", 74.0f }, output { "Output", 74.0f };
        Attachment inputAttachment, mixAttachment, outputAttachment;

        melatonin::InnerShadow displayInner {
            { juce::Colour (0xff221e1c).withAlpha (0.16f), 12, { 0, 4 } },
            { juce::Colours::white.withAlpha (0.55f), 2, { 0, -1 } }
        };
    };

    linen::LinenLookAndFeel lookAndFeel;
    Canvas canvas;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LinenAudioProcessorEditor)
};
