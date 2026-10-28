// LinenRender: runs the Linen engine offline on a WAV file.
//
//   LinenRender <in.wav> <out.wav> [options]
//
//   --input dB  --output dB  --mix 0..1  --block N
//   --silk 0..1  --pain 0..100
//   --drive 0..1  --hpf 0..4  --low-freq 0..4  --low-gain dB  --mid-freq 0..6  --mid-gain dB  --high-gain dB
//   --depth 0..15  --detail 0..1  --attack ms  --release ms  --max-cut dB
//   --focus-low Hz  --focus-high Hz  --hard 0|1  --glare 0|1  --body 0|1  --delta 0|1
//
// The output is trimmed by the engine latency so it lines up with the input.

#include <juce_audio_formats/juce_audio_formats.h>

#include "dsp/LinenEngine.h"

namespace
{
    int fail (const juce::String& message)
    {
        std::cerr << message << std::endl;
        return 1;
    }
}

int main (int argc, char** argv)
{
    juce::StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add (argv[i]);

    if (args.size() < 2)
        return fail ("usage: LinenRender <in.wav> <out.wav> [options] (see tools/render.cpp)");

    const juce::File inFile = juce::File::getCurrentWorkingDirectory().getChildFile (args[0]);
    const juce::File outFile = juce::File::getCurrentWorkingDirectory().getChildFile (args[1]);

    linen::EngineParams params;
    int blockSize = 512;

    for (int i = 2; i + 1 < args.size(); i += 2)
    {
        const auto flag = args[i];
        const float value = args[i + 1].getFloatValue();

        auto& d = params.deharsh;

        if (flag == "--input")           params.inputGainDb = value;
        else if (flag == "--output")     params.outputGainDb = value;
        else if (flag == "--mix")        params.mix = value;
        else if (flag == "--block")      blockSize = juce::jmax (1, (int) value);
        else if (flag == "--silk")       params.silk = value;
        else if (flag == "--pain")       params.pain = value / 100.0f;
        else if (flag == "--drive")      params.preampDrive = value;
        else if (flag == "--hpf")        params.eq.highPass = (int) value;
        else if (flag == "--low-freq")   params.eq.lowFreq = (int) value;
        else if (flag == "--low-gain")   params.eq.lowGainDb = value;
        else if (flag == "--mid-freq")   params.eq.midFreq = (int) value;
        else if (flag == "--mid-gain")   params.eq.midGainDb = value;
        else if (flag == "--high-gain")  params.eq.highGainDb = value;
        else if (flag == "--depth")      d.depth = value;
        else if (flag == "--detail")     d.detail = value;
        else if (flag == "--attack")     d.attackMs = value;
        else if (flag == "--release")    d.releaseMs = value;
        else if (flag == "--max-cut")    d.maxCutDb = value;
        else if (flag == "--focus-low")  d.focusLowHz = value;
        else if (flag == "--focus-high") d.focusHighHz = value;
        else if (flag == "--hard")       d.hard = value > 0.5f;
        else if (flag == "--glare")      d.glareFocus = value > 0.5f;
        else if (flag == "--body")       d.bodyProtect = value > 0.5f;
        else if (flag == "--delta")      d.delta = value > 0.5f;
        else return fail ("unknown option " + flag);
    }

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (inFile));
    if (reader == nullptr)
        return fail ("cannot read " + inFile.getFullPathName());

    const int channels = (int) reader->numChannels;
    const int length = (int) reader->lengthInSamples;
    const double sampleRate = reader->sampleRate;

    linen::Engine engine;
    engine.setParams (params);
    engine.prepare (sampleRate, blockSize, channels);
    const int latency = engine.getLatencySamples();

    // Pad with silence so the latency tail comes out, then trim it off the front.
    juce::AudioBuffer<float> audio (channels, length + latency);
    audio.clear();
    reader->read (&audio, 0, length, 0, true, true);

    const auto started = juce::Time::getMillisecondCounterHiRes();
    for (int pos = 0; pos < audio.getNumSamples(); pos += blockSize)
    {
        const int n = juce::jmin (blockSize, audio.getNumSamples() - pos);
        juce::AudioBuffer<float> block (audio.getArrayOfWritePointers(), channels, pos, n);
        engine.process (block);
    }
    const double seconds = (juce::Time::getMillisecondCounterHiRes() - started) / 1000.0;

    outFile.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = outFile.createOutputStream();
    if (stream == nullptr)
        return fail ("cannot write " + outFile.getFullPathName());

    juce::WavAudioFormat wav;
    const auto options = juce::AudioFormatWriterOptions{}.withSampleRate (sampleRate)
                                                         .withNumChannels (channels)
                                                         .withBitsPerSample (32)
                                                         .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
    auto writer = wav.createWriterFor (stream, options);
    if (writer == nullptr)
        return fail ("cannot create WAV writer");

    writer->writeFromAudioSampleBuffer (audio, latency, length);

    const double audioSeconds = length / sampleRate;
    std::cout << "rendered " << audioSeconds << " s, " << channels << " ch, latency " << latency
              << " samples, " << (audioSeconds / juce::jmax (1.0e-9, seconds)) << "x realtime" << std::endl;
    return 0;
}
