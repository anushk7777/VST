// linen-render: runs the Linen engine offline on a WAV file.
//
//   linen-render <in.wav> <out.wav> [--input dB] [--output dB] [--mix 0..1] [--block N]
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
        return fail ("usage: linen-render <in.wav> <out.wav> [--input dB] [--output dB] [--mix 0..1] [--block N]");

    const juce::File inFile = juce::File::getCurrentWorkingDirectory().getChildFile (args[0]);
    const juce::File outFile = juce::File::getCurrentWorkingDirectory().getChildFile (args[1]);

    linen::EngineParams params;
    int blockSize = 512;

    for (int i = 2; i + 1 < args.size(); i += 2)
    {
        const auto flag = args[i];
        const float value = args[i + 1].getFloatValue();

        if (flag == "--input")       params.inputGainDb = value;
        else if (flag == "--output") params.outputGainDb = value;
        else if (flag == "--mix")    params.mix = value;
        else if (flag == "--block")  blockSize = juce::jmax (1, (int) value);
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

    for (int pos = 0; pos < audio.getNumSamples(); pos += blockSize)
    {
        const int n = juce::jmin (blockSize, audio.getNumSamples() - pos);
        juce::AudioBuffer<float> block (audio.getArrayOfWritePointers(), channels, pos, n);
        engine.process (block);
    }

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

    std::cout << "rendered " << length << " samples, " << channels << " ch, latency " << latency << std::endl;
    return 0;
}
