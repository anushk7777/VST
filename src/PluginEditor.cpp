#include "PluginEditor.h"
#include "ui/Palette.h"

namespace
{
    constexpr int margin = 32;

    void drawHairline (juce::Graphics& g, float x0, float x1, float y, juce::Colour colour)
    {
        // Fades out at both ends, like a printed rule.
        juce::ColourGradient grad (colour.withAlpha (0.0f), x0, y, colour.withAlpha (0.0f), x1, y, false);
        grad.addColour (0.12, colour);
        grad.addColour (0.88, colour);
        g.setGradientFill (grad);
        g.fillRect (juce::Rectangle<float> (x0, y, x1 - x0, 1.0f));
    }
}

//==============================================================================
LinenAudioProcessorEditor::LinenAudioProcessorEditor (LinenAudioProcessor& p)
    : AudioProcessorEditor (p),
      canvas (p)
{
    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (canvas);

    setResizable (true, true);
    setResizeLimits (designWidth * 3 / 4, designHeight * 3 / 4, designWidth * 2, designHeight * 2);
    getConstrainer()->setFixedAspectRatio ((double) designWidth / (double) designHeight);
    setSize (designWidth, designHeight);
}

LinenAudioProcessorEditor::~LinenAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void LinenAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (linen::palette::alabaster);
}

void LinenAudioProcessorEditor::resized()
{
    canvas.setBounds (0, 0, designWidth, designHeight);
    canvas.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) designWidth));
}

//==============================================================================
LinenAudioProcessorEditor::Canvas::Canvas (LinenAudioProcessor& p)
    : inputAttachment (p.state, linen::params::id::inputGain, input),
      mixAttachment (p.state, linen::params::id::mix, mix),
      outputAttachment (p.state, linen::params::id::outputGain, output)
{
    const std::pair<linen::LinenKnob*, const char*> knobs[] {
        { &input, linen::params::id::inputGain },
        { &mix, linen::params::id::mix },
        { &output, linen::params::id::outputGain },
    };

    for (auto [knob, paramId] : knobs)
    {
        if (auto* param = p.state.getParameter (paramId))
            knob->setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));

        addAndMakeVisible (*knob);
    }

    input.setBipolar (true);
    output.setBipolar (true);
}

void LinenAudioProcessorEditor::Canvas::resized()
{
    displayArea = { margin, 112, designWidth - margin * 2, 300 };

    const int knobW = 110, knobH = 110;
    const int rowY = displayArea.getBottom() + 44;
    const int cx = getWidth() / 2;

    input.setBounds (margin + 20, rowY, knobW, knobH);
    mix.setBounds (cx - knobW / 2, rowY, knobW, knobH);
    output.setBounds (getWidth() - margin - 20 - knobW, rowY, knobW, knobH);
}

void LinenAudioProcessorEditor::Canvas::paint (juce::Graphics& g)
{
    weave.draw (g, getLocalBounds());
    paintHeader (g);
    paintDisplayWell (g);

    using namespace linen::palette;
    drawHairline (g, (float) margin, (float) (getWidth() - margin), (float) getHeight() - 38.0f, champagne.withAlpha (0.45f));

    g.setColour (ink (0.38f));
    g.setFont (linen::fonts::sans (9.5f, 0.22f));
    g.drawText ("LINEN AUDIO", juce::Rectangle<int> (margin, getHeight() - 30, 200, 20), juce::Justification::centredLeft);
    g.drawText ("v" JucePlugin_VersionString, juce::Rectangle<int> (getWidth() - margin - 200, getHeight() - 30, 200, 20),
                juce::Justification::centredRight);
}

void LinenAudioProcessorEditor::Canvas::paintHeader (juce::Graphics& g)
{
    using namespace linen::palette;

    g.setColour (espresso);
    g.setFont (linen::fonts::serif (50.0f, 0.34f));
    g.drawText ("LINEN", juce::Rectangle<int> (margin - 2, 22, 340, 54), juce::Justification::centredLeft);

    g.setColour (ink (0.58f));
    g.setFont (linen::fonts::serifItalic (19.0f, 0.01f));
    g.drawText ("silk for the voice", juce::Rectangle<int> (margin, 72, 340, 22), juce::Justification::centredLeft);

    g.setColour (ink (0.45f));
    g.setFont (linen::fonts::sans (9.5f, 0.24f));
    g.drawText (juce::String (juce::CharPointer_UTF8 ("VOCAL PREAMP  \xc2\xb7  DE-HARSH  \xc2\xb7  COMPRESSION  \xc2\xb7  AIR")),
                juce::Rectangle<int> (getWidth() - margin - 460, 40, 460, 20), juce::Justification::centredRight);

    drawHairline (g, (float) margin, (float) (getWidth() - margin), 100.0f, champagne.withAlpha (0.55f));
}

void LinenAudioProcessorEditor::Canvas::paintDisplayWell (juce::Graphics& g)
{
    using namespace linen::palette;

    const auto well = displayArea.toFloat();
    juce::Path wellPath;
    wellPath.addRoundedRectangle (well, 14.0f);

    g.setColour (cashmere);
    g.fillPath (wellPath);
    displayInner.render (g, wellPath);

    // Log-frequency grid, 20 Hz – 20 kHz.
    const auto plot = well.reduced (22.0f, 18.0f);
    const auto xFor = [&] (float hz)
    {
        return plot.getX() + plot.getWidth() * std::log (hz / 20.0f) / std::log (1000.0f);
    };

    g.setFont (linen::fonts::sans (9.0f, 0.12f));
    for (const float hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = xFor (hz);
        g.setColour (ink (0.06f));
        g.fillRect (juce::Rectangle<float> (x, plot.getY(), 1.0f, plot.getHeight() - 14.0f));
        g.setColour (ink (0.36f));
        const auto label = hz >= 1000.0f ? juce::String ((int) (hz / 1000.0f)) + "k" : juce::String ((int) hz);
        g.drawText (label, juce::Rectangle<float> (x - 20.0f, plot.getBottom() - 10.0f, 40.0f, 12.0f), juce::Justification::centred);
    }

    g.setColour (ink (0.40f));
    g.setFont (linen::fonts::serifItalic (17.0f));
    g.drawText ("the silk veil appears here once De-harsh is woven in",
                plot.withTrimmedBottom (14.0f), juce::Justification::centred);
}
