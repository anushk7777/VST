#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "ui/Backdrop.h"
#include "ui/Lights.h"
#include "ui/LinenLookAndFeel.h"
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
        /** Shows the current preset; click for the list. */
        class PresetBox final : public juce::Component,
                                public juce::SettableTooltipClient
        {
        public:
            explicit PresetBox (LinenAudioProcessor&);
            void paint (juce::Graphics&) override;
            void mouseEnter (const juce::MouseEvent&) override { hovering = true; repaint(); }
            void mouseExit (const juce::MouseEvent&) override { hovering = false; repaint(); }
            void mouseUp (const juce::MouseEvent&) override;

        private:
            LinenAudioProcessor& processor;
            bool hovering = false;
        };

        /** A softly raised linen card behind one section. */
        struct Card
        {
            juce::String title, descriptor;
            juce::Rectangle<int> bounds;
            melatonin::DropShadow shadow { { linen::palette::espresso.withAlpha (0.09f), 18, { 0, 6 } },
                                           { linen::palette::espresso.withAlpha (0.05f), 3, { 0, 1 } } };
            bool dims = true;   // fades with Delta
        };

        using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
        using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

        void attach (linen::LinenKnob&, const char* paramId, const juce::String& tooltip);
        void attach (linen::LinenPill&, const char* paramId, const juce::String& tooltip);
        void paintHeader (juce::Graphics&);
        void paintCard (juce::Graphics&, Card&);
        void updateDimming (double timestampSeconds);
        std::vector<juce::Component*> dimmable();
        float param (const char* paramId) const;
        float presence() const;

        LinenAudioProcessor& processor;
        linen::Backdrop backdrop;
        std::array<Card, 5> cards;   // preamp, comp, de-harsh, air, output

        linen::SpectrumDisplay display;

        // Header: presets, Listen, and the two macros.
        PresetBox presetBox;
        linen::ListenButton listenButton;
        linen::LinenKnob pain { "Pain", 54.0f }, silk { "Silk", 54.0f };

        // Preamp + 1073-style EQ
        linen::LinenKnob drive { "Drive", 74.0f };
        linen::LinenKnob lowGain { "Low", 52.0f }, midGain { "Mid", 52.0f }, highGain { "High 12k", 52.0f };
        linen::LinenStepper highPass, lowFreq, midFreq;

        // Silk compressor
        linen::LinenKnob cotton { "Cotton", 62.0f };
        linen::LinenKnob threshold { "Thresh", 46.0f }, ratio { "Ratio", 46.0f }, compAttack { "Attack", 46.0f },
                         compRelease { "Release", 46.0f }, compMix { "Blend", 46.0f };
        linen::LinenPill makeup { "Auto gain" };
        linen::LinenMeter compMeter;

        // De-harsh
        linen::LinenSegmented mode;
        linen::LinenPill glare { "Glare" }, body { "Body" }, delta { "Delta" }, sidechain { "Key" };
        linen::LinenKnob depth { "Depth", 74.0f }, detail { "Detail", 52.0f }, attack { "Attack", 52.0f },
                         release { "Release", 52.0f }, maxCut { "Max cut", 52.0f };

        // Air
        linen::LinenKnob air { "Air", 52.0f }, deEss { "De-ess", 52.0f };

        // Output
        linen::LinenKnob input { "Input", 50.0f }, mix { "Mix", 50.0f }, output { "Output", 50.0f };

        // Jewel lamps, one per section, and the brand jewel.
        linen::LinenLamp preampLamp, compLamp, deharshLamp, airLamp, outputLamp, brandLamp;

        std::vector<std::unique_ptr<SliderAttachment>> sliderAttachments;
        std::vector<std::unique_ptr<ButtonAttachment>> buttonAttachments;

        // Pain scorches the linen's edge; repaint the canvas when it moves.
        float scorch = 0.0f;
        juce::ParameterAttachment painWatch;

        // Delta dims everything but De-harsh so the ear (and eye) stay on it.
        float dim = 1.0f;
        double lastTick = 0.0, clock = 0.0;
        juce::VBlankAttachment vblank;
    };

    linen::LinenLookAndFeel lookAndFeel;
    Canvas canvas;
    juce::TooltipWindow tooltips { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LinenAudioProcessorEditor)
};
