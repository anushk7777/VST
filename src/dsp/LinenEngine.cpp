#include "LinenEngine.h"
#include "SilkMacro.h"

namespace linen
{

namespace
{
    constexpr double smoothingSeconds = 0.025;

    int maxDelayFor (int latency) { return juce::jmax (1, latency + 1); }

    /** Pain reaches into its neighbours: more preamp drive, a De-harsh that
        clamps the fizz above 5.5 kHz but leaves the throat band room to ache,
        and (past 50%) a darker, dustier Air. */
    EngineParams withPainCoupling (EngineParams p)
    {
        const float pain = juce::jlimit (0.0f, 1.0f, p.pain);
        const float t = juce::jlimit (0.0f, 1.0f, (pain - 0.5f) / 0.5f);

        p.preampDrive = juce::jlimit (0.0f, 1.0f, p.preampDrive + 0.25f * pain);
        p.deharsh.painTilt = pain;
        p.air.scorch = t * t * (3.0f - 2.0f * t);
        return p;
    }
}

void Engine::prepare (double newSampleRate, int maxBlockSize, int numChannels)
{
    sampleRate = newSampleRate;
    maxBlock = maxBlockSize;
    numChans = numChannels;

    const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlock, (juce::uint32) numChans };

    tone.prepare (sampleRate, numChans);
    preamp.prepare (sampleRate, maxBlock, numChans);
    eq.prepare (sampleRate, numChans);
    painEngine.prepare (sampleRate, maxBlock, numChans);
    deharsh.prepare (sampleRate, numChans);
    comp.prepare (sampleRate, maxBlock, numChans);
    air.prepare (sampleRate, maxBlock, numChans);
    detector.assign ((size_t) maxBlock, 0.0f);

    // Each switch's bypass is as late as its section, so switching a section
    // out never moves anything in time.
    toneSwitch.prepare (sampleRate, maxBlock, numChans, 0);
    preampSwitch.prepare (sampleRate, maxBlock, numChans, preamp.getLatencySamples());
    deharshSwitch.prepare (sampleRate, maxBlock, numChans, deharsh.getLatencySamples());
    compSwitch.prepare (sampleRate, maxBlock, numChans, comp.getLatencySamples());
    airSwitch.prepare (sampleRate, maxBlock, numChans, 0);

    dryDelay.prepare (spec);
    testWetDelay.prepare (spec);
    dryBuffer.setSize (numChans, maxBlock, false, false, true);

    inputGain.reset (sampleRate, smoothingSeconds);
    outputGain.reset (sampleRate, smoothingSeconds);
    mix.reset (sampleRate, smoothingSeconds);
    nullAmount.reset (sampleRate, smoothingSeconds);

    updateLatency();
    reset();
}

void Engine::reset()
{
    tone.reset();
    preamp.reset();
    eq.reset();
    painEngine.reset();
    deharsh.reset();
    comp.reset();
    air.reset();
    dryDelay.reset();
    testWetDelay.reset();

    for (auto* s : { &toneSwitch, &preampSwitch, &deharshSwitch, &compSwitch, &airSwitch })
        s->reset();

    inputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (params.inputGainDb));
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (params.outputGainDb));
    mix.setCurrentAndTargetValue (params.deharsh.delta ? 1.0f : params.mix);
    nullAmount.setCurrentAndTargetValue (params.nullTest ? 1.0f : 0.0f);
}

void Engine::setParams (const EngineParams& newParams)
{
    params = withPainCoupling (SilkMacro::apply (newParams, newParams.silk));

    // Delta plays what De-harsh removes; with De-harsh switched off there is nothing to play.
    if (! params.sections.deharsh)
        params.deharsh.delta = false;

    tone.setParams (params.tone);
    painEngine.setPain (params.pain);
    preamp.setDrive (params.preampDrive);
    eq.setParams (params.eq);
    deharsh.setParams (params.deharsh);
    comp.setParams (params.comp);
    air.setParams (params.air);

    toneSwitch.setOn (params.sections.tone);
    preampSwitch.setOn (params.sections.preamp);
    deharshSwitch.setOn (params.sections.deharsh);
    compSwitch.setOn (params.sections.comp);
    airSwitch.setOn (params.sections.air);

    inputGain.setTargetValue (juce::Decibels::decibelsToGain (params.inputGainDb));
    outputGain.setTargetValue (juce::Decibels::decibelsToGain (params.outputGainDb));

    // Delta is for listening to what De-harsh removes, so never blend dry into it.
    mix.setTargetValue (params.deharsh.delta ? 1.0f : juce::jlimit (0.0f, 1.0f, params.mix));
    nullAmount.setTargetValue (params.nullTest ? 1.0f : 0.0f);
}

void Engine::setExtraWetLatencyForTesting (int samples)
{
    testWetLatency = juce::jmax (0, samples);
    updateLatency();
}

void Engine::updateLatency()
{
    latencySamples = preamp.getLatencySamples() + painEngine.getLatencySamples() + deharsh.getLatencySamples()
                   + comp.getLatencySamples() + testWetLatency;

    dryDelay.setMaximumDelayInSamples (maxDelayFor (latencySamples));
    dryDelay.setDelay ((float) latencySamples);

    testWetDelay.setMaximumDelayInSamples (maxDelayFor (testWetLatency));
    testWetDelay.setDelay ((float) testWetLatency);
}

void Engine::process (juce::AudioBuffer<float>& buffer, const juce::AudioBuffer<float>* sidechain)
{
    // Some hosts occasionally send more than the announced block size,
    // so work through anything larger in prepared-size chunks.
    for (int pos = 0; pos < buffer.getNumSamples(); pos += maxBlock)
    {
        const int n = juce::jmin (maxBlock, buffer.getNumSamples() - pos);
        juce::AudioBuffer<float> chunk (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), pos, n);
        processChunk (chunk, sidechain, pos);
    }
}

void Engine::processChunk (juce::AudioBuffer<float>& buffer, const juce::AudioBuffer<float>* sidechain, int offset)
{
    const int numSamples = buffer.getNumSamples();
    const int channels = juce::jmin (buffer.getNumChannels(), numChans);

    if (channels > 0)
        inputLevel.store (buffer.getRMSLevel (0, 0, numSamples), std::memory_order_relaxed);

    // Dry path: untouched input, delayed to line up with the wet path.
    for (int ch = 0; ch < channels; ++ch)
    {
        const auto* in = buffer.getReadPointer (ch);
        auto* dry = dryBuffer.getWritePointer (ch);

        for (int i = 0; i < numSamples; ++i)
        {
            dryDelay.pushSample (ch, in[i]);
            dry[i] = dryDelay.popSample (ch);
        }
    }

    // Input gain.
    for (int i = 0; i < numSamples; ++i)
    {
        const float g = inputGain.getNextValue();
        for (int ch = 0; ch < channels; ++ch)
            buffer.getWritePointer (ch)[i] *= g;
    }

    juce::AudioBuffer<float> wet (buffer.getArrayOfWritePointers(), channels, numSamples);

    // Shape the voice before the preamp sees it.
    toneSwitch.capture (wet);
    tone.process (wet);
    toneSwitch.apply (wet);

    // Colour: preamp, then the console EQ.
    preampSwitch.capture (wet);
    preamp.process (wet);
    eq.process (wet);
    preampSwitch.apply (wet);

    painEngine.process (wet);

    // De-harsh, detecting from the vocal itself or from the sidechain.
    deharshSwitch.capture (wet);
    {
        const bool useSidechain = params.deharshSidechain && sidechain != nullptr && sidechain->getNumChannels() > 0;
        const auto& source = useSidechain ? *sidechain : buffer;
        const int sourceOffset = useSidechain ? offset : 0;
        const int sourceChannels = useSidechain ? sidechain->getNumChannels() : channels;
        const float norm = 1.0f / (float) juce::jmax (1, sourceChannels);

        std::fill (detector.begin(), detector.begin() + numSamples, 0.0f);
        for (int ch = 0; ch < sourceChannels; ++ch)
        {
            const auto* src = source.getReadPointer (ch, sourceOffset);
            for (int i = 0; i < numSamples; ++i)
                detector[(size_t) i] += src[i] * norm;
        }

        deharsh.process (wet, detector.data(), ! useSidechain);
    }
    deharshSwitch.apply (wet);

    // Glue, then air on top of the levelled voice.
    compSwitch.capture (wet);
    comp.process (wet);
    compSwitch.apply (wet);

    airSwitch.capture (wet);
    air.process (wet);
    airSwitch.apply (wet);

    if (testWetLatency > 0)
    {
        for (int ch = 0; ch < channels; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            for (int i = 0; i < numSamples; ++i)
            {
                testWetDelay.pushSample (ch, data[i]);
                data[i] = testWetDelay.popSample (ch);
            }
        }
    }

    // Output gain, then a linear (phase-coherent) dry/wet blend. The phase
    // test then takes the untouched input back out, sample-aligned.
    for (int i = 0; i < numSamples; ++i)
    {
        const float g = outputGain.getNextValue();
        const float m = mix.getNextValue();
        const float nulling = nullAmount.getNextValue();

        for (int ch = 0; ch < channels; ++ch)
        {
            // Written so that a wet path equal to the dry one gives the dry
            // signal back exactly, at any Mix.
            auto* data = buffer.getWritePointer (ch);
            const float dry = dryBuffer.getSample (ch, i);
            const float processed = data[i] * g;
            const float out = m >= 1.0f ? processed : dry + (processed - dry) * m;
            data[i] = nulling > 0.0f ? out - dry * nulling : out;
        }
    }
}

} // namespace linen
