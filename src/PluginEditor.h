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
    static constexpr int designWidth = 1200;
    static constexpr int designHeight = 800;

    explicit LinenAudioProcessorEditor (LinenAudioProcessor&);
    ~LinenAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    /** The fixed-size canvas holding every control. */
    class Canvas final : public juce::Component,
                         private juce::AudioProcessorValueTreeState::Listener
    {
    public:
        explicit Canvas (LinenAudioProcessor&);
        ~Canvas() override;

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

        /** A raised linen faceplate behind one section, screwed down at its corners. */
        struct Card
        {
            juce::String title, descriptor;
            juce::Rectangle<int> bounds;
            melatonin::DropShadow shadow { { linen::palette::espresso.withAlpha (0.11f), 18, { 0, 7 } },
                                           { linen::palette::espresso.withAlpha (0.06f), 3, { 0, 1 } } };
            bool dims = true;                        // fades with Delta
            const char* powerId = nullptr;           // the section's power switch, if it has one
            std::vector<juce::Component*> members;   // fade with the card
            float alpha = 1.0f;
        };

        using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
        using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

        void attach (juce::Slider&, const char* paramId, const juce::String& tooltip);
        void attach (juce::Button&, const char* paramId, const juce::String& tooltip);
        void parameterChanged (const juce::String& parameterID, float newValue) override;
        void paintHeader (juce::Graphics&);
        void paintCard (juce::Graphics&, Card&, int index);
        void paintSlotLabels (juce::Graphics&);
        void updateDimming (double timestampSeconds, bool instantly = false);
        float param (const char* paramId) const;
        bool isOn (const char* powerId) const { return param (powerId) > 0.5f; }
        float presence() const;
        static int titleX (const Card&);

        LinenAudioProcessor& processor;
        linen::Backdrop backdrop;
        std::array<Card, 6> cards;   // tone, preamp, comp, de-harsh, air, output

        linen::SpectrumDisplay display;

        // Header: presets, Listen, and the two macros.
        PresetBox presetBox;
        linen::ListenButton listenButton;
        linen::LinenKnob pain { "Pain", 46.0f }, silk { "Silk", 46.0f };

        // Tone: shelves before the preamp
        linen::LinenKnob toneLow { "Low shelf", 46.0f }, toneHigh { "High shelf", 46.0f };
        linen::LinenValuePill toneLowFreq, toneHighFreq;

        // Preamp + 1073-style EQ
        linen::LinenKnob drive { "Drive", 64.0f };
        linen::LinenKnob lowGain { "Low", 42.0f }, midGain { "Mid", 42.0f }, highGain { "High 12k", 42.0f };
        linen::LinenStepper lowFreq, midFreq;

        // The Dua Wox compressor: TUBE (the catch) | VU | OPTO (the leveller), Warmth and Blend shared.
        linen::LinenKnob threshold { "Thresh", 40.0f }, ratio { "Ratio", 40.0f }, compAttack { "Attack", 40.0f },
                         compRelease { "Release", 40.0f };
        linen::LinenKnob optoReduction { "Peak red.", 64.0f };
        linen::LinenKnob warmth { "Warmth", 38.0f }, compMix { "Blend", 38.0f };
        linen::LinenPill makeup { "Auto gain" };
        linen::LinenToggle tubeSwitch, optoSwitch;
        linen::LinenVuMeter vuMeter;

        // De-harsh
        linen::LinenSegmented mode;
        linen::LinenPill glare { "Glare" }, body { "Body" }, delta { "Delta" }, sidechain { "Key" };
        linen::LinenKnob depth { "Depth", 68.0f }, detail { "Detail", 46.0f }, attack { "Attack", 46.0f },
                         release { "Release", 46.0f }, maxCut { "Max cut", 46.0f };

        // Air
        linen::LinenKnob air { "Air", 46.0f }, deEss { "De-ess", 46.0f };

        // Output, and the phase test
        linen::LinenKnob input { "Input", 46.0f }, mix { "Mix", 46.0f }, output { "Output", 46.0f };
        linen::LinenPill nullTest { "Null" };

        // A bat-handle switch for each section.
        linen::LinenToggle tonePower, preampPower, compPower, deharshPower, airPower;

        // Jewel lamps, one per section and one per Dua Wox slot, and the brand jewel.
        linen::LinenLamp toneLamp, preampLamp, compLamp, deharshLamp, airLamp, outputLamp, tubeLamp, optoLamp, brandLamp;

        std::vector<std::unique_ptr<SliderAttachment>> sliderAttachments;
        std::vector<std::unique_ptr<ButtonAttachment>> buttonAttachments;

        // Pain scorches the linen's edge; repaint the canvas when it moves.
        float scorch = 0.0f;
        juce::ParameterAttachment painWatch;

        // Delta dims everything but De-harsh so the ear (and eye) stay on it;
        // a section switched off fades back too.
        double lastTick = 0.0, clock = 0.0;
        juce::VBlankAttachment vblank;
    };

    linen::LinenLookAndFeel lookAndFeel;
    Canvas canvas;
    juce::TooltipWindow tooltips { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LinenAudioProcessorEditor)
};
