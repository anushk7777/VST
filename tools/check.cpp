// LinenCheck: loads the built Linen.vst3 the way a DAW does, and checks it.
//
//   LinenCheck <path/to/Linen.vst3> [--plot null.png] [--wav vocal.wav]
//
// 1. Null (phase) test. A fresh Linen (the Clean preset) plays test audio
//    (noise, a sung vowel, a full-scale sweep and silence, or the given WAV)
//    at 44.1, 48 and 96 kHz, in stereo and mono, in irregular block sizes.
//    The input, delayed by the latency the plugin reports, is subtracted from
//    the output: Clean must give the input back bit for bit (residual -inf).
//    The same holds with every section switched off and every knob cranked.
// 2. Distortion by preset: THD (harmonics 2-9) of a 1 kHz and a 220 Hz note
//    at -6 dBFS peak and -18 dBFS RMS. Presets without Pain must stay under
//    -45 dB on the -6 dBFS notes.
//
// Exits with 0 when everything passes.

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <BinaryData.h>

namespace
{
    constexpr int maxBlock = 512;
    constexpr double budgetDb = -45.0;

    struct Failures
    {
        int count = 0;
        void add (const juce::String& what) { ++count; std::cout << "   FAIL: " << what << std::endl; }
    };

    //==========================================================================
    // Test audio

    juce::AudioBuffer<float> programme (double sr, int channels)
    {
        const int noiseLength = (int) (0.5 * sr), vowelLength = (int) (1.5 * sr), sweepLength = (int) sr, silenceLength = (int) (0.25 * sr);
        juce::AudioBuffer<float> b (channels, noiseLength + vowelLength + sweepLength + silenceLength);
        b.clear();
        juce::Random rng (1);
        int pos = 0;

        // White noise.
        for (int i = 0; i < noiseLength; ++i, ++pos)
            for (int ch = 0; ch < channels; ++ch)
                b.setSample (ch, pos, 0.5f * (rng.nextFloat() * 2.0f - 1.0f));

        // A sung vowel: harmonics of a 220 Hz note with vibrato, formants and a
        // singer's ring, swelling up to about -6 dBFS.
        double phase = 0.0;
        for (int i = 0; i < vowelLength; ++i, ++pos)
        {
            const double t = i / sr;
            const double f0 = 220.0 * (1.0 + 0.012 * std::sin (juce::MathConstants<double>::twoPi * 5.5 * t));
            phase += juce::MathConstants<double>::twoPi * f0 / sr;
            double v = 0.0;
            for (int n = 1; n * f0 < juce::jmin (16000.0, sr * 0.45); ++n)
            {
                const double hz = n * f0;
                const double formants = 1.0 / (1.0 + std::pow ((hz - 700.0) / 450.0, 2.0)) + 0.5 / (1.0 + std::pow ((hz - 1200.0) / 400.0, 2.0))
                                      + 0.6 / (1.0 + std::pow ((hz - 3200.0) / 300.0, 2.0));
                v += (n == 1 ? 0.5 : 0.3 / n) * (0.15 + formants) * std::sin (phase * n);
            }
            const double swell = std::sin (juce::MathConstants<double>::pi * t / 1.5);
            for (int ch = 0; ch < channels; ++ch)
                b.setSample (ch, pos, (float) (0.32 * swell * v) + 0.002f * (rng.nextFloat() - 0.5f));
        }

        // A logarithmic sweep, 20 Hz to 20 kHz, at full scale.
        const double f1 = juce::jmin (20000.0, sr * 0.45), k = std::log (f1 / 20.0), duration = sweepLength / sr;
        for (int i = 0; i < sweepLength; ++i, ++pos)
        {
            const double t = i / sr;
            const double sweepPhase = juce::MathConstants<double>::twoPi * 20.0 * duration / k * (std::exp (k * t / duration) - 1.0);
            for (int ch = 0; ch < channels; ++ch)
                b.setSample (ch, pos, (float) std::sin (sweepPhase + ch * 0.5));
        }

        return b;   // ...and the silence at the end
    }

    juce::AudioBuffer<float> sine (double sr, double hz, double peakDb, int samples)
    {
        juce::AudioBuffer<float> b (2, samples);
        const double amp = juce::Decibels::decibelsToGain (peakDb);
        for (int i = 0; i < samples; ++i)
        {
            const auto v = (float) (amp * std::sin (juce::MathConstants<double>::twoPi * hz * i / sr));
            b.setSample (0, i, v);
            b.setSample (1, i, v);
        }
        return b;
    }

    //==========================================================================
    // Hosting

    std::unique_ptr<juce::AudioPluginInstance> load (const juce::PluginDescription& description, double sr, int channels)
    {
        juce::VST3PluginFormat format;
        juce::String error;
        auto instance = format.createInstanceFromDescription (description, sr, maxBlock, error);
        if (instance == nullptr)
        {
            std::cout << "cannot load the plugin: " << error << std::endl;
            return nullptr;
        }

        // Main bus mono or stereo; the sidechain, if there is one, off.
        auto layout = instance->getBusesLayout();
        const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
        for (int i = 0; i < layout.inputBuses.size(); ++i)
            layout.inputBuses.getReference (i) = i == 0 ? set : juce::AudioChannelSet::disabled();
        for (int i = 0; i < layout.outputBuses.size(); ++i)
            layout.outputBuses.getReference (i) = i == 0 ? set : juce::AudioChannelSet::disabled();
        if (! instance->setBusesLayout (layout))
        {
            std::cout << "the plugin refused a " << set.getDescription() << " layout" << std::endl;
            return nullptr;
        }

        instance->setRateAndBufferSizeDetails (sr, maxBlock);
        instance->prepareToPlay (sr, maxBlock);
        return instance;
    }

    /** Plays audio through the plugin in irregular blocks, as a host might. */
    juce::AudioBuffer<float> play (juce::AudioPluginInstance& plugin, const juce::AudioBuffer<float>& in)
    {
        juce::AudioBuffer<float> out (in);
        juce::MidiBuffer midi;
        const int sizes[] { maxBlock, 1, maxBlock / 2, 17, maxBlock - 3, 64 };
        int pos = 0, which = 0;
        while (pos < out.getNumSamples())
        {
            const int n = juce::jmin (sizes[which++ % 6], out.getNumSamples() - pos);
            juce::AudioBuffer<float> block (out.getArrayOfWritePointers(), out.getNumChannels(), pos, n);
            plugin.processBlock (block, midi);
            pos += n;
        }
        return out;
    }

    juce::AudioProcessorParameter* parameterNamed (juce::AudioPluginInstance& plugin, const juce::String& name)
    {
        for (auto* p : plugin.getParameters())
            if (p->getName (100) == name)
                return p;
        return nullptr;
    }

    struct Null
    {
        int identical = 0, compared = 0;
        float worst = 0.0f;

        bool exact() const { return compared > 0 && identical == compared; }

        juce::String describe() const
        {
            const auto residual = exact() ? juce::String ("-inf") : juce::String (juce::Decibels::gainToDecibels (worst, -200.0f), 1);
            return juce::String (identical) + " / " + juce::String (compared) + " samples identical, residual " + residual + " dBFS";
        }
    };

    /** Output minus the input delayed by the latency, from sample `from` of the input on. */
    Null compare (const juce::AudioBuffer<float>& in, const juce::AudioBuffer<float>& out, int latency, int from = 0)
    {
        Null n;
        for (int ch = 0; ch < in.getNumChannels(); ++ch)
            for (int i = from; i + latency < out.getNumSamples(); ++i)
            {
                const float a = in.getSample (ch, i), b = out.getSample (ch, i + latency);
                ++n.compared;
                if (juce::exactlyEqual (a, b))
                    ++n.identical;
                n.worst = juce::jmax (n.worst, std::abs (b - a));
            }
        return n;
    }

    //==========================================================================
    // Measuring

    /** Level (dBFS) of one tone, from a Blackman-Harris windowed DFT at exactly hz. */
    double toneDb (const float* x, int length, double sr, double hz)
    {
        double re = 0.0, im = 0.0, wsum = 0.0;
        for (int i = 0; i < length; ++i)
        {
            const double a = juce::MathConstants<double>::twoPi * i / (length - 1);
            const double w = 0.35875 - 0.48829 * std::cos (a) + 0.14128 * std::cos (2 * a) - 0.01168 * std::cos (3 * a);
            const double phase = juce::MathConstants<double>::twoPi * hz * i / sr;
            re += x[i] * w * std::cos (phase);
            im -= x[i] * w * std::sin (phase);
            wsum += w;
        }
        return 20.0 * std::log10 (2.0 * std::sqrt (re * re + im * im) / wsum + 1.0e-20);
    }

    /** THD (dB re the note) of a steady note, measured after 1.3 s of settling. */
    double measure (juce::AudioPluginInstance& plugin, double sr, double hz, double peakDb)
    {
        plugin.releaseResources();
        plugin.prepareToPlay (sr, maxBlock);   // start from rest, as a fresh insert would

        const int settle = (int) (1.3 * sr), length = 32768;
        const auto in = sine (sr, hz, peakDb, settle + length + plugin.getLatencySamples());
        const auto out = play (plugin, in);

        const float* y = out.getReadPointer (0, settle + plugin.getLatencySamples());
        const double fundamental = toneDb (y, length, sr, hz);
        double power = 0.0;
        for (int n = 2; n <= 9 && n * hz < sr * 0.45; ++n)
            power += std::pow (10.0, toneDb (y, length, sr, n * hz) / 10.0);

        return juce::jmax (-140.0, 10.0 * std::log10 (power + 1.0e-30) - fundamental);
    }

    //==========================================================================
    // The picture

    void plot (const juce::File& file, const juce::AudioBuffer<float>& in, const juce::AudioBuffer<float>& out, int latency,
               double sr, const Null& result)
    {
        constexpr int width = 1400, height = 860, left = 70, right = 40;
        juce::Image image (juce::Image::RGB, width, height, true);
        juce::Graphics g (image);

        const auto sans = juce::Typeface::createSystemTypefaceFor (LinenBinary::InterRegular_ttf, (size_t) LinenBinary::InterRegular_ttfSize);
        const auto serif = juce::Typeface::createSystemTypefaceFor (LinenBinary::CormorantGaramondMedium_ttf,
                                                                    (size_t) LinenBinary::CormorantGaramondMedium_ttfSize);
        const auto font = [] (const juce::Typeface::Ptr& face, float h) { return juce::Font (juce::FontOptions (face).withHeight (h)); };

        const juce::Colour paper (0xfff6f3ec), ink (0xff221e1c), champagne (0xffb08f55), cobalt (0xff1f4fff);
        g.fillAll (paper);

        g.setColour (ink);
        g.setFont (font (serif, 34.0f));
        g.drawText ("Linen, Clean preset: null (phase) test", left, 22, width - left - right, 44, juce::Justification::centredLeft);
        g.setColour (ink.withAlpha (0.6f));
        g.setFont (font (sans, 15.0f));
        g.drawText ("The real Linen.vst3, loaded like a DAW loads it, " + juce::String (sr / 1000.0, 1) + " kHz stereo. "
                    "Its output, lined up by the " + juce::String (latency) + "-sample latency it reports, minus your untouched audio.",
                    left, 66, width - left - right, 22, juce::Justification::centredLeft);

        const int samples = in.getNumSamples() - latency;
        const auto drawPanel = [&] (int top, int panelHeight, const juce::String& title, juce::Colour colour, auto&& valueAt, float scale)
        {
            const juce::Rectangle<int> area (left, top + 26, width - left - right, panelHeight - 26);
            g.setColour (ink.withAlpha (0.75f));
            g.setFont (font (sans, 15.0f));
            g.drawText (title, left, top, width - left - right, 22, juce::Justification::centredLeft);
            g.setColour (juce::Colour (0xffebe4d8));
            g.fillRoundedRectangle (area.toFloat(), 10.0f);
            g.setColour (ink.withAlpha (0.15f));
            g.drawHorizontalLine (area.getCentreY(), (float) area.getX(), (float) area.getRight());

            g.setColour (colour);
            for (int x = 0; x < area.getWidth(); ++x)
            {
                const int i0 = (int) ((int64_t) x * samples / area.getWidth()), i1 = (int) ((int64_t) (x + 1) * samples / area.getWidth());
                float lo = 0.0f, hi = 0.0f;
                for (int i = i0; i < juce::jmax (i0 + 1, i1); ++i)
                {
                    const float v = juce::jlimit (-1.0f, 1.0f, valueAt (i) * scale);
                    lo = juce::jmin (lo, v);
                    hi = juce::jmax (hi, v);
                }
                const float half = (float) area.getHeight() * 0.46f;
                g.drawVerticalLine (area.getX() + x, (float) area.getCentreY() - hi * half, (float) area.getCentreY() - lo * half + 1.0f);
            }
        };

        drawPanel (110, 220, "1. Your audio (input): noise, a sung vowel, a full-scale sweep, silence", ink.withAlpha (0.7f),
                   [&] (int i) { return in.getSample (0, i); }, 1.0f);
        drawPanel (350, 220, "2. Linen's output on Clean, lined up by its latency", champagne,
                   [&] (int i) { return out.getSample (0, i + latency); }, 1.0f);
        drawPanel (590, 220, "3. Output minus input, zoomed in 1,000,000x: any difference would show here", cobalt,
                   [&] (int i) { return out.getSample (0, i + latency) - in.getSample (0, i); }, 1.0e6f);

        g.setColour (result.exact() ? cobalt : juce::Colour (0xffd1493f));
        g.setFont (font (sans, 17.0f));
        g.drawText ((result.exact() ? juce::String ("Perfect null: ") : juce::String ("NOT a null: ")) + result.describe(),
                    left, 818, width - left - right, 26, juce::Justification::centredLeft);

        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat png;
        if (stream.openedOk() && png.writeImageToStream (image, stream))
            std::cout << "   wrote " << file.getFullPathName() << std::endl;
    }
}

int main (int argc, char** argv)
{
    const juce::ScopedJuceInitialiser_GUI messageThread;   // hosting needs one

    juce::StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add (argv[i]);

    if (args.isEmpty())
    {
        std::cout << "usage: LinenCheck <path/to/Linen.vst3> [--plot null.png] [--wav vocal.wav]" << std::endl;
        return 2;
    }

    const auto cwd = juce::File::getCurrentWorkingDirectory();
    const auto pluginFile = cwd.getChildFile (args[0]);
    juce::File plotFile, wavFile;
    for (int i = 1; i + 1 < args.size(); i += 2)
    {
        if (args[i] == "--plot") plotFile = cwd.getChildFile (args[i + 1]);
        if (args[i] == "--wav")  wavFile = cwd.getChildFile (args[i + 1]);
    }

    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> types;
    format.findAllTypesForFile (types, pluginFile.getFullPathName());
    if (types.isEmpty())
    {
        std::cout << "no VST3 plugin found at " << pluginFile.getFullPathName() << std::endl;
        return 2;
    }
    const auto& description = *types[0];
    std::cout << "Linen check: " << description.name << " " << description.version << " by " << description.manufacturerName
              << "\n   " << pluginFile.getFullPathName() << "\n" << std::endl;

    Failures failures;

    // Optional: the user's own vocal instead of the built-in programme.
    juce::AudioBuffer<float> wav;
    double wavRate = 0.0;
    if (wavFile.existsAsFile())
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        if (std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (wavFile)); reader != nullptr)
        {
            wav.setSize (2, (int) juce::jmin<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 60.0)));
            reader->read (&wav, 0, wav.getNumSamples(), 0, true, true);
            wavRate = reader->sampleRate;
        }
    }

    //==========================================================================
    std::cout << "1. Null test: Clean preset (a fresh Linen), output minus the latency-aligned input" << std::endl;
    for (const double sr : { 44100.0, 48000.0, 96000.0 })
        for (const int channels : { 2, 1 })
        {
            auto plugin = load (description, sr, channels);
            if (plugin == nullptr)
                return 2;

            const int latency = plugin->getLatencySamples();
            auto in = programme (sr, channels);
            in.setSize (channels, in.getNumSamples() + latency, true, true);   // room for the tail
            const auto out = play (*plugin, in);
            const auto result = compare (in, out, latency);

            std::cout << "   " << juce::String (sr / 1000.0, 1) << " kHz " << (channels == 2 ? "stereo" : "mono  ")
                      << "  latency " << latency << " samples   " << result.describe() << (result.exact() ? "   PASS" : "") << std::endl;
            if (! result.exact())
                failures.add ("Clean is not a perfect null");

            if (juce::exactlyEqual (sr, 48000.0) && channels == 2 && plotFile != juce::File())
                plot (plotFile, in, out, latency, sr, result);
        }

    if (wav.getNumSamples() > 0)
    {
        auto plugin = load (description, wavRate, 2);
        const int latency = plugin->getLatencySamples();
        wav.setSize (2, wav.getNumSamples() + latency, true, true);
        const auto result = compare (wav, play (*plugin, wav), latency);
        std::cout << "   your WAV (" << wavFile.getFileName() << ")   " << result.describe() << (result.exact() ? "   PASS" : "") << std::endl;
        if (! result.exact())
            failures.add ("Clean is not a perfect null on the WAV");
    }

    // Every section switched off, with every knob cranked: still untouched.
    {
        auto plugin = load (description, 48000.0, 2);
        const std::pair<const char*, float> settings[] {
            { "Tone Low Shelf Gain", 0.0f }, { "Tone High Shelf Gain", 1.0f }, { "Preamp Drive", 1.0f }, { "EQ Low Gain", 1.0f },
            { "EQ Mid Gain", 0.0f }, { "EQ High Gain (12 kHz)", 1.0f }, { "De-harsh Depth", 1.0f }, { "Comp Ratio", 1.0f },
            { "Comp Threshold", 0.0f }, { "Comp Cotton", 1.0f }, { "Air", 1.0f }, { "Air De-ess", 1.0f }, { "Silk", 1.0f },
            { "Tone On", 0.0f }, { "Preamp On", 0.0f }, { "De-harsh On", 0.0f }, { "Comp On", 0.0f }, { "Air On", 0.0f } };
        bool found = true;
        for (const auto& [name, value] : settings)
        {
            if (auto* p = parameterNamed (*plugin, name))
                p->setValue (value);
            else
                found = false;
        }
        if (! found)
            failures.add ("some parameters were not found by name");

        // A second of silence first, so the switches' 20 ms fades are long done.
        const int latency = plugin->getLatencySamples();
        const int warmUp = 48000;
        const auto audio = programme (48000.0, 2);
        juce::AudioBuffer<float> in (2, warmUp + audio.getNumSamples() + latency);
        in.clear();
        for (int ch = 0; ch < 2; ++ch)
            in.copyFrom (ch, warmUp, audio, ch, 0, audio.getNumSamples());
        const auto result = compare (in, play (*plugin, in), latency, warmUp);
        std::cout << "   every section off, every knob cranked   " << result.describe() << (result.exact() ? "   PASS" : "") << std::endl;
        if (! result.exact())
            failures.add ("switched-off sections are not a perfect null");
    }

    //==========================================================================
    std::cout << "\n2. Distortion by preset: THD (harmonics 2-9) re the note, on 1 kHz and 220 Hz notes\n"
              << "   " << juce::String ("preset").paddedRight (' ', 22) << "1k -6 dBFS pk   1k -18 dBFS rms   220 -6 dBFS pk   220 -18 dBFS rms" << std::endl;
    {
        auto plugin = load (description, 48000.0, 2);
        auto* painParameter = parameterNamed (*plugin, "Pain");

        for (int program = 0; program < plugin->getNumPrograms(); ++program)
        {
            plugin->setCurrentProgram (program);
            juce::MidiBuffer midi;
            juce::AudioBuffer<float> nudge (2, 64);   // delivers the preset to the processor
            nudge.clear();
            plugin->processBlock (nudge, midi);

            const bool meantToCrack = painParameter != nullptr && painParameter->getValue() > 0.0f;
            const auto name = plugin->getProgramName (program);

            const auto loud1k = measure (*plugin, 48000.0, 1000.0, -6.0);
            const auto soft1k = measure (*plugin, 48000.0, 1000.0, -15.0);
            const auto loud220 = measure (*plugin, 48000.0, 220.0, -6.0);
            const auto soft220 = measure (*plugin, 48000.0, 220.0, -15.0);

            // Below -120 dB is the float rounding of the test tone itself: nothing added.
            const auto cell = [] (double thd) { return (thd <= -120.0 ? juce::String ("none") : juce::String (thd, 1) + " dB").paddedLeft (' ', 13); };
            std::cout << "   " << (name + (meantToCrack ? " (Pain)" : "")).paddedRight (' ', 22) << cell (loud1k) << "   " << cell (soft1k)
                      << "    " << cell (loud220) << "     " << cell (soft220) << std::endl;

            if (! meantToCrack && juce::jmax (loud1k, loud220) > budgetDb)
                failures.add (name + " distorts more than " + juce::String (budgetDb, 0) + " dB on a -6 dBFS note");
        }
        std::cout << "   (Presets with Pain are meant to crack and strain, so they are shown but not held to the budget.)" << std::endl;
    }

    std::cout << "\n" << (failures.count == 0 ? "ALL CHECKS PASSED" : "CHECKS FAILED: " + std::to_string (failures.count)) << std::endl;
    return failures.count == 0 ? 0 : 1;
}
