#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"

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

    // The sidechain feeds De-harsh's detector when the host routes one in.
    // (emplace keeps it a non-owning view; assigning would copy the audio.)
    std::optional<juce::AudioBuffer<float>> sidechain;
    if (getBusCount (true) > 1 && getBus (true, 1)->isEnabled() && getChannelCountOfBus (true, 1) > 0)
        sidechain.emplace (getBusBuffer (buffer, true, 1));

    listen.push (main, getMainBusNumInputChannels());

    engine.setParams (handles.read());
    engine.process (main, sidechain ? &*sidechain : nullptr);
}

void LinenAudioProcessor::applyAssist (const linen::AssistResult& r)
{
    namespace id = linen::params::id;

    const std::pair<const char*, float> values[] {
        { id::inputGain, r.inputGainDb },     { id::preampDrive, r.preampDrive },
        { id::eqHighPass, (float) r.highPass }, { id::eqLowFreq, (float) r.lowFreq }, { id::eqLowGain, r.lowGainDb },
        { id::eqMidFreq, (float) r.midFreq },  { id::eqMidGain, r.midGainDb },      { id::eqHighGain, r.highGainDb },
        { id::deharshDepth, r.deharshDepth },  { id::deharshDetail, r.deharshDetail },
        { id::compThreshold, r.compThresholdDb }, { id::compRatio, r.compRatio }, { id::compCotton, r.compCotton },
        { id::airAmount, r.airAmount },        { id::airDeEss, r.airDeEss },
    };

    for (const auto& [paramId, value] : values)
        if (auto* p = state.getParameter (paramId))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (value));
            p->endChangeGesture();
        }
}

int LinenAudioProcessor::getNumPrograms()
{
    return (int) linen::presets::factory.size();
}

void LinenAudioProcessor::setCurrentProgram (int index)
{
    // Hosts call this with the current index while restoring a session;
    // re-applying it there would wipe the restored settings.
    if (index != currentProgram)
        loadPreset (index);
}

void LinenAudioProcessor::loadPreset (int index)
{
    if (! juce::isPositiveAndBelow (index, getNumPrograms()))
        return;

    currentProgram = index;
    state.state.setProperty ("preset", index, nullptr);
    linen::presets::apply (state, linen::presets::factory[(size_t) index]);
    updateHostDisplay (ChangeDetails().withProgramChanged (true));
}

const juce::String LinenAudioProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, getNumPrograms()) ? linen::presets::factory[(size_t) index].name : "";
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
        {
            state.replaceState (juce::ValueTree::fromXml (*xml));
            currentProgram = juce::jlimit (0, getNumPrograms() - 1, (int) state.state.getProperty ("preset", 0));
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LinenAudioProcessor();
}
