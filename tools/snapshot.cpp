// LinenSnapshot: renders the plugin window to a PNG while a synthetic (or
// given) vocal plays through the real processor, so the GUI can be checked
// without a DAW. Needs a display (use Xvfb on a headless Linux box).
//
//   LinenSnapshot <out.png> [in.wav] [--scale 2] [--width 1200] [--seconds 3] [--listen] [--preset N] [--param id=value ...]

#include <juce_audio_formats/juce_audio_formats.h>

#include "PluginEditor.h"
#include "PluginProcessor.h"

namespace
{
    /** A bright sung vowel: harmonics of 220 Hz, a nasal resonance near 3.2 kHz,
        a little breath noise and a slow vibrato. */
    juce::AudioBuffer<float> syntheticVocal (double sampleRate, int samples)
    {
        juce::AudioBuffer<float> buffer (2, samples);
        buffer.clear();
        juce::Random rng (5);
        double phase = 0.0;

        for (int i = 0; i < samples; ++i)
        {
            const double t = i / sampleRate;
            const double f0 = 220.0 * (1.0 + 0.012 * std::sin (juce::MathConstants<double>::twoPi * 5.5 * t));
            phase += juce::MathConstants<double>::twoPi * f0 / sampleRate;

            double s = 0.0;
            for (int n = 1; n * f0 < 12000.0; ++n)
            {
                const double hz = n * f0;
                const double formant = 1.0 / (1.0 + std::pow ((hz - 700.0) / 500.0, 2.0))
                                     + 0.5 / (1.0 + std::pow ((hz - 1200.0) / 400.0, 2.0))
                                     + 0.9 / (1.0 + std::pow ((hz - 3200.0) / 180.0, 2.0));
                s += (n == 1 ? 0.5 : 0.25 / n) * (0.2 + formant) * std::sin (phase * n);
            }

            const float v = (float) (0.4 * s) + 0.004f * (rng.nextFloat() - 0.5f);
            buffer.setSample (0, i, v);
            buffer.setSample (1, i, v);
        }
        return buffer;
    }
}

/** Runs as a normal JUCE app (a bare console process trips over X11 window
    properties), plays the audio through the processor on a timer at real-time
    pace, then writes the snapshot and quits. */
class SnapshotApp final : public juce::JUCEApplication,
                          private juce::Timer
{
public:
    const juce::String getApplicationName() override { return "LinenSnapshot"; }
    const juce::String getApplicationVersion() override { return "1.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise (const juce::String&) override
    {
        auto args = getCommandLineParameterArray();
        if (args.isEmpty())
        {
            std::cerr << "usage: LinenSnapshot <out.png> [in.wav] [--scale 2] [--param id=value ...]" << std::endl;
            setApplicationReturnValue (1);
            quit();
            return;
        }

        outFile = juce::File::getCurrentWorkingDirectory().getChildFile (args[0]);
        juce::File inFile;

        for (int i = 1; i < args.size(); ++i)
        {
            if (args[i] == "--scale" && i + 1 < args.size())
                scale = args[++i].getFloatValue();
            else if (args[i] == "--width" && i + 1 < args.size())
                width = args[++i].getIntValue();
            else if (args[i] == "--seconds" && i + 1 < args.size())
                seconds = args[++i].getDoubleValue();
            else if (args[i] == "--listen")
                listen = true;
            else if (args[i] == "--preset" && i + 1 < args.size())
                processor.loadPreset (args[++i].getIntValue());
            else if (args[i] == "--param" && i + 1 < args.size())
            {
                const auto setting = args[++i];
                if (auto* p = processor.state.getParameter (setting.upToFirstOccurrenceOf ("=", false, false)))
                    p->setValueNotifyingHost (p->convertTo0to1 (setting.fromFirstOccurrenceOf ("=", false, false).getFloatValue()));
                else
                    std::cerr << "unknown parameter in " << setting << std::endl;
            }
            else
                inFile = juce::File::getCurrentWorkingDirectory().getChildFile (args[i]);
        }

        if (inFile.existsAsFile())
        {
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (inFile));
            if (reader != nullptr)
            {
                audio.setSize (2, (int) std::min (reader->lengthInSamples, (juce::int64) (sampleRate * seconds)));
                reader->read (&audio, 0, audio.getNumSamples(), 0, true, true);
            }
        }

        if (audio.getNumSamples() == 0)
            audio = syntheticVocal (sampleRate, (int) (sampleRate * seconds));

        processor.setPlayConfigDetails (2, 2, sampleRate, blockSize);
        processor.prepareToPlay (sampleRate, blockSize);

        window = std::make_unique<juce::DocumentWindow> ("Linen", juce::Colours::black, 0);
        window->setUsingNativeTitleBar (true);
        auto* editor = processor.createEditor();
        if (width > 0)
            editor->setSize (width, width * LinenAudioProcessorEditor::designHeight / LinenAudioProcessorEditor::designWidth);
        window->setContentOwned (editor, true);
        window->setTopLeftPosition (40, 40);
        window->setVisible (true);

        if (listen)
            processor.startListening();

        startTimer ((int) (1000.0 * blockSize / sampleRate));
    }

    void shutdown() override
    {
        window = nullptr;
    }

    void systemRequestedQuit() override { quit(); }

private:
    void timerCallback() override
    {
        if (position + blockSize <= audio.getNumSamples())
        {
            juce::MidiBuffer midi;
            juce::AudioBuffer<float> block (2, blockSize);
            for (int ch = 0; ch < 2; ++ch)
                block.copyFrom (ch, 0, audio, ch, position, blockSize);
            processor.processBlock (block, midi);
            position += blockSize;
            return;
        }

        stopTimer();

        auto* editor = window->getContentComponent();
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, scale);
        outFile.deleteFile();
        juce::FileOutputStream stream (outFile);
        juce::PNGImageFormat png;
        const bool ok = stream.openedOk() && png.writeImageToStream (image, stream);
        std::cout << (ok ? "wrote " : "cannot write ") << outFile.getFullPathName() << std::endl;
        setApplicationReturnValue (ok ? 0 : 1);
        quit();
    }

    static constexpr double sampleRate = 48000.0;
    static constexpr int blockSize = 512;

    LinenAudioProcessor processor;
    juce::AudioBuffer<float> audio;
    std::unique_ptr<juce::DocumentWindow> window;
    juce::File outFile;
    float scale = 2.0f;
    int width = 0;   // 0: the editor's own default size
    double seconds = 3.0;
    bool listen = false;
    int position = 0;
};

START_JUCE_APPLICATION (SnapshotApp)
