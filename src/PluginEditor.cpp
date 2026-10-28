#include "PluginEditor.h"
#include "ui/Palette.h"

namespace
{
    namespace id = linen::params::id;

    constexpr int margin = 32;
    constexpr int displayTop = 112, displayHeight = 206;
    constexpr int topSectionY = 330;        // Preamp | Output titles
    constexpr int topRowY = 368;            // their controls
    constexpr int deharshSectionY = 500;    // De-harsh title and switches
    constexpr int deharshRowY = 540;        // its knobs
    constexpr int outputSectionX = 652;
    constexpr int eqColumnWidth = 104;

    void drawHairline (juce::Graphics& g, float x0, float x1, float y, juce::Colour colour)
    {
        // Fades out at both ends, like a printed rule.
        juce::ColourGradient grad (colour.withAlpha (0.0f), x0, y, colour.withAlpha (0.0f), x1, y, false);
        grad.addColour (0.12, colour);
        grad.addColour (0.88, colour);
        g.setGradientFill (grad);
        g.fillRect (juce::Rectangle<float> (x0, y, x1 - x0, 1.0f));
    }

    juce::RangedAudioParameter& param (LinenAudioProcessor& p, const char* paramId)
    {
        auto* parameter = p.state.getParameter (paramId);
        jassert (parameter != nullptr);
        return *parameter;
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
    : processor (p),
      display (p.getDeharshDisplay(), param (p, id::deharshFocusLow), param (p, id::deharshFocusHigh)),
      highPass (param (p, id::eqHighPass)),
      lowFreq (param (p, id::eqLowFreq)),
      midFreq (param (p, id::eqMidFreq)),
      mode (param (p, id::deharshMode), { "Soft", "Hard" }),
      vblank (this, [this] (double t) { updateDimming (t); })
{
    addAndMakeVisible (display);

    attach (drive, id::preampDrive, "Drive: push the 1073-style preamp for transformer weight and class-A warmth");
    attach (lowGain, id::eqLowGain, "Low shelf gain");
    attach (midGain, id::eqMidGain, "Mid gain: a broad, musical bell that narrows as you push it");
    attach (highGain, id::eqHighGain, "High shelf at 12 kHz: air and sheen");
    for (auto [stepper, tip] : { std::pair { &highPass, "High-pass filter (18 dB/oct)" },
                                 std::pair { &lowFreq, "Low shelf frequency" },
                                 std::pair { &midFreq, "Mid frequency" } })
    {
        stepper->setTooltip (tip);
        addAndMakeVisible (*stepper);
    }
    for (auto* k : { &lowGain, &midGain, &highGain })
        k->setBipolar (true);

    addAndMakeVisible (mode);
    mode.setTooltip ("Soft: transparent, adapts to the voice's natural tone. Hard: firmer, catches more.");

    attach (glare, id::deharshGlare, "Glare focus: lean on the 2.5-5.5 kHz region where harshness hurts most");
    attach (body, id::deharshBody, "Body protect: never cut the sung fundamental, so the voice keeps its weight");
    attach (delta, id::deharshDelta, "Delta: hear only what De-harsh is removing");
    attach (sidechain, id::deharshSidechain, "Sidechain: listen to the sidechain input instead of the vocal");

    attach (depth, id::deharshDepth, "Depth: how much of each harsh resonance is removed");
    attach (detail, id::deharshDetail, "Detail: low = broad, gentle cuts; high = narrow, picky cuts");
    attach (attack, id::deharshAttack, "Attack: how fast cuts close in");
    attach (release, id::deharshRelease, "Release: how slowly cuts let go");
    attach (maxCut, id::deharshMaxCut, "Max cut: the deepest any single frequency can be cut");

    attach (input, id::inputGain, "Input gain");
    attach (mix, id::mix, "Mix: blend the processed vocal with the untouched one (latency-matched)");
    attach (output, id::outputGain, "Output gain");

    input.setBipolar (true);
    output.setBipolar (true);
}

void LinenAudioProcessorEditor::Canvas::attach (linen::LinenKnob& knob, const char* paramId, const juce::String& tooltip)
{
    auto& parameter = param (processor, paramId);
    sliderAttachments.push_back (std::make_unique<SliderAttachment> (processor.state, paramId, knob));
    knob.setDoubleClickReturnValue (true, parameter.convertFrom0to1 (parameter.getDefaultValue()));
    knob.setTooltip (tooltip);
    addAndMakeVisible (knob);
}

void LinenAudioProcessorEditor::Canvas::attach (linen::LinenPill& pill, const char* paramId, const juce::String& tooltip)
{
    buttonAttachments.push_back (std::make_unique<ButtonAttachment> (processor.state, paramId, pill));
    pill.setTooltip (tooltip);
    addAndMakeVisible (pill);
}

void LinenAudioProcessorEditor::Canvas::resized()
{
    display.setBounds (margin, displayTop, designWidth - margin * 2, displayHeight);

    const auto place = [] (linen::LinenKnob& knob, int left, int top, int width)
    {
        knob.setBounds (left, top, width, (int) knob.getKnobDiameter() + 34);
    };

    // Preamp: Drive, then the EQ columns (selector under each gain knob).
    place (drive, margin, topRowY, 104);
    const int eqX = drive.getRight() + 16;
    const int stepperY = topRowY + 54 + 40;
    const int stepperInset = 14;
    highPass.setBounds (eqX + stepperInset, topRowY + 36, eqColumnWidth - stepperInset * 2, 20);
    place (lowGain, eqX + eqColumnWidth, topRowY, eqColumnWidth);
    lowFreq.setBounds (eqX + eqColumnWidth + stepperInset, stepperY, eqColumnWidth - stepperInset * 2, 20);
    place (midGain, eqX + eqColumnWidth * 2, topRowY, eqColumnWidth);
    midFreq.setBounds (eqX + eqColumnWidth * 2 + stepperInset, stepperY, eqColumnWidth - stepperInset * 2, 20);
    place (highGain, eqX + eqColumnWidth * 3, topRowY, eqColumnWidth);

    // Output.
    int x = outputSectionX;
    for (auto* knob : { &input, &mix, &output })
    {
        place (*knob, x, topRowY + 10, 84);
        x += 84 + 20;
    }

    // De-harsh: switches beside the title, then big Depth and the finer controls.
    x = margin + 112;
    const int pillY = deharshSectionY + 3, pillH = 22;
    mode.setBounds (x, pillY, mode.getIdealWidth(), pillH);
    x = mode.getRight() + 14;
    for (auto* pill : { &glare, &body, &delta, &sidechain })
    {
        pill->setBounds (x, pillY, pill->getIdealWidth(), pillH);
        x = pill->getRight() + 8;
    }

    place (depth, margin, deharshRowY, 104);
    x = depth.getRight() + 20;
    for (auto* knob : { &detail, &attack, &release, &maxCut })
    {
        place (*knob, x, deharshRowY + 10, 84);
        x += 84 + 20;
    }
}

void LinenAudioProcessorEditor::Canvas::updateDimming (double timestampSeconds)
{
    const float dt = lastTick > 0.0 ? (float) juce::jlimit (0.0, 0.1, timestampSeconds - lastTick) : 0.0f;
    lastTick = timestampSeconds;

    const float target = delta.getToggleState() ? 0.35f : 1.0f;
    if (std::abs (target - dim) < 0.002f)
        return;

    // ~180 ms ease-out.
    dim += (target - dim) * (1.0f - std::exp (-dt / 0.06f));
    if (std::abs (target - dim) < 0.002f)
        dim = target;

    for (auto* c : std::initializer_list<juce::Component*> { &input, &mix, &output, &drive, &lowGain, &midGain,
                                                               &highGain, &highPass, &lowFreq, &midFreq })
        c->setAlpha (dim);
    repaint (0, topSectionY - 4, getWidth(), 44);
    repaint (margin + 104, topRowY + 50, outputSectionX - margin - 104, 40);
}

void LinenAudioProcessorEditor::Canvas::paintSectionTitle (juce::Graphics& g, const juce::String& title, int x, int y, float alpha)
{
    g.setColour (linen::palette::espresso.withMultipliedAlpha (alpha));
    g.setFont (linen::fonts::serifMedium (24.0f, 0.01f));
    g.drawText (title, juce::Rectangle<int> (x, y, 200, 28), juce::Justification::centredLeft);
}

void LinenAudioProcessorEditor::Canvas::paint (juce::Graphics& g)
{
    using namespace linen::palette;

    weave.draw (g, getLocalBounds());
    paintHeader (g);

    paintSectionTitle (g, "Preamp", margin, topSectionY, dim);
    paintSectionTitle (g, "Output", outputSectionX + 6, topSectionY, dim);
    paintSectionTitle (g, "De-harsh", margin, deharshSectionY, 1.0f);

    // The high-pass column has no gain knob; caption it where the others' captions sit.
    {
        const int eqX = margin + 104 + 16;
        g.setColour (ink (0.62f * dim));
        g.setFont (linen::fonts::sansMedium (11.0f, 0.16f));
        g.drawText ("HIGH-PASS", juce::Rectangle<int> (eqX, topRowY + 66, eqColumnWidth, 16), juce::Justification::centred);
    }

    // Quiet rules between the sections.
    {
        const float x = (float) outputSectionX - 22.0f;
        juce::ColourGradient grad (champagne.withAlpha (0.0f), x, (float) topSectionY, champagne.withAlpha (0.0f), x, (float) topSectionY + 140.0f, false);
        grad.addColour (0.2, champagne.withAlpha (0.45f));
        grad.addColour (0.8, champagne.withAlpha (0.45f));
        g.setGradientFill (grad);
        g.fillRect (juce::Rectangle<float> (x, (float) topSectionY, 1.0f, 140.0f));
    }
    drawHairline (g, (float) margin, (float) (getWidth() - margin), (float) deharshSectionY - 14.0f, champagne.withAlpha (0.35f));

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
