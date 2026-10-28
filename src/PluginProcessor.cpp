#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    juce::AudioChannelSet mainLayoutOf (const juce::AudioProcessor::BusesLayout& layouts, bool isInput)
    {
        return isInput ? layouts.getMainInputChannelSet() : layouts.getMainOutputChannelSet();
    }
}

LinenAudioProcessor::LinenAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      state (*this, nullptr, "LINEN", linen::params::createLayout()),
      handles (state)
{
}

bool LinenAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = mainLayoutOf (layouts, false);

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    if (mainLayoutOf (layouts, true) != out)
        return false;

    // Sidechain (used by De-harsh later) may be off, mono or stereo.
    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
            return false;
    }

    return true;
}

void LinenAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.setParams (handles.read());
    engine.prepare (sampleRate, samplesPerBlock, getMainBusNumOutputChannels());
    setLatencySamples (engine.getLatencySamples());
}

void LinenAudioProcessor::releaseResources()
{
}

void LinenAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    auto main = getBusBuffer (buffer, false, 0);

    for (int ch = getMainBusNumInputChannels(); ch < main.getNumChannels(); ++ch)
        main.clear (ch, 0, main.getNumSamples());

    engine.setParams (handles.read());
    engine.process (main);
}

juce::AudioProcessorEditor* LinenAudioProcessor::createEditor()
{
    return new LinenAudioProcessorEditor (*this);
}

void LinenAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void LinenAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (state.state.getType()))
            state.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LinenAudioProcessor();
}
