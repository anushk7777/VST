#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "ListenSession.h"
#include "Parameters.h"
#include "dsp/LinenEngine.h"

class LinenAudioProcessor final : public juce::AudioProcessor
{
public:
    LinenAudioProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState state;

    /** Loads a factory preset (always, even if it is the current one). */
    void loadPreset (int index);

    /** De-harsh analysis for the spectrum display (read on the message thread). */
    linen::DeharshDisplay& getDeharshDisplay() noexcept { return engine.getDeharshDisplay(); }

    /** Live meters for the GUI. */
    float getCompReductionDb() const noexcept { return engine.getCompReductionDb(); }
    float getTubeReductionDb() const noexcept { return engine.getTubeReductionDb(); }
    float getOptoReductionDb() const noexcept { return engine.getOptoReductionDb(); }
    float getSibilance() const noexcept { return engine.getSibilance(); }
    float getPainStress() const noexcept { return engine.getPainStress(); }
    float getInputLevel() const noexcept { return engine.getInputLevel(); }

    /** The Listen button: hear a few seconds of the vocal, then set Linen up for it. */
    ListenSession& getListenSession() noexcept { return listen; }
    void startListening() { listen.start (getSampleRate()); }

private:
    void applyAssist (const linen::AssistResult&);

    linen::params::Handles handles;
    int currentProgram = 0;
    linen::Engine engine;
    ListenSession listen { [this] (const linen::AssistResult& r) { applyAssist (r); } };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LinenAudioProcessor)
};
