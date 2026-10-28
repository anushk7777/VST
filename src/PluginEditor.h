#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "ui/LinenLookAndFeel.h"
#include "ui/LinenWeave.h"
#include "ui/Meters.h"
#include "ui/SpectrumDisplay.h"

/** Everything is laid out once on a fixed design canvas and scaled as a whole,
    so the window can be resized without any element reflowing. */
class LinenAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    static constexpr int designWidth = 1040;
    static constexpr int designHeight = 740;

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
        void paintSectionTitle (juce::Graphics&, const juce::String& title, int x, int y, float alpha);
        void updateDimming (double timestampSeconds);
        std::vector<juce::Component*> dimmable();

        LinenAudioProcessor& processor;
        linen::LinenWeave weave;

        linen::SpectrumDisplay display;

        // Preamp + 1073-style EQ
        linen::LinenKnob drive { "Drive", 74.0f };
        linen::LinenKnob lowGain { "Low", 52.0f }, midGain { "Mid", 52.0f }, highGain { "High 12k", 52.0f };
        linen::LinenStepper highPass, lowFreq, midFreq;

        // Silk compressor
        linen::LinenKnob threshold { "Threshold", 52.0f }, ratio { "Ratio", 52.0f }, compAttack { "Attack", 52.0f },
                         compRelease { "Release", 52.0f }, compMix { "Blend", 52.0f };
        linen::LinenPill makeup { "Auto gain" };
        linen::LinenMeter compMeter;

        // De-harsh
        linen::LinenSegmented mode;
        linen::LinenPill glare { "Glare" }, body { "Body" }, delta { "Delta" }, sidechain { "Sidechain" };
        linen::LinenKnob depth { "Depth", 74.0f }, detail { "Detail", 52.0f }, attack { "Attack", 52.0f },
                         release { "Release", 52.0f }, maxCut { "Max cut", 52.0f };

        // Air
        linen::LinenKnob air { "Air", 52.0f }, deEss { "De-ess", 52.0f };
        linen::LinenMeter sibilanceLight;

        // Output
        linen::LinenKnob input { "Input", 52.0f }, mix { "Mix", 52.0f }, output { "Output", 52.0f };

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
