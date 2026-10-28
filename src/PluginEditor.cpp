#include "PluginEditor.h"
#include "ui/Palette.h"

namespace
{
    namespace id = linen::params::id;

    constexpr int margin = 32;
    constexpr int displayTop = 112, displayHeight = 204;

    // Two rows of sections: Preamp | Silk comp, then De-harsh | Air | Output.
    constexpr int rowATitleY = 332, rowAKnobY = 370;
    constexpr int rowBTitleY = 508, rowBKnobY = 548;
    constexpr int rightColumnX = 600;   // Silk comp, Air
    constexpr int outputX = 790;

    constexpr int eqColumnWidth = 100;
    constexpr int smallColumn = 76;

    void drawHairline (juce::Graphics& g, float x0, float x1, float y, juce::Colour colour)
    {
        // Fades out at both ends, like a printed rule.
        juce::ColourGradient grad (colour.withAlpha (0.0f), x0, y, colour.withAlpha (0.0f), x1, y, false);
        grad.addColour (0.12, colour);
        grad.addColour (0.88, colour);
        g.setGradientFill (grad);
        g.fillRect (juce::Rectangle<float> (x0, y, x1 - x0, 1.0f));
    }

    void drawVerticalRule (juce::Graphics& g, float x, float y0, float y1, juce::Colour colour)
    {
        juce::ColourGradient grad (colour.withAlpha (0.0f), x, y0, colour.withAlpha (0.0f), x, y1, false);
        grad.addColour (0.2, colour);
        grad.addColour (0.8, colour);
        g.setGradientFill (grad);
        g.fillRect (juce::Rectangle<float> (x, y0, 1.0f, y1 - y0));
    }

    juce::RangedAudioParameter& param (LinenAudioProcessor& p, const char* paramId)
    {
        auto* parameter = p.state.getParameter (paramId);
        jassert (parameter != nullptr);
        return *parameter;
    }

    void place (linen::LinenKnob& knob, int left, int top, int width)
    {
        knob.setBounds (left, top, width, (int) knob.getKnobDiameter() + 34);
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
      compMeter (linen::LinenMeter::Style::bar, [&p] { return p.getCompReductionDb(); }, 12.0f),
      mode (param (p, id::deharshMode), { "Soft", "Hard" }),
      sibilanceLight (linen::LinenMeter::Style::dot, [&p] { return p.getSibilance(); }, 1.0f),
      vblank (this, [this] (double t) { updateDimming (t); })
{
    addAndMakeVisible (display);

    // Preamp + EQ
    attach (drive, id::preampDrive, "Drive: push the preamp for transformer weight and class-A warmth");
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

    // Silk compressor
    attach (threshold, id::compThreshold, "Threshold: where the compressor starts to hold the voice");
    attach (ratio, id::compRatio, "Ratio: 1.5-2.5 is invisible, 4+ is firm");
    attach (compAttack, id::compAttack, "Attack: slower lets consonants through for a natural edge");
    attach (compRelease, id::compRelease, "Release (fast part): it slows down by itself on long phrases, like an opto");
    attach (compMix, id::compMix, "Blend: parallel compression, mixed with the uncompressed voice");
    attach (makeup, id::compMakeup, "Auto gain: make up for the average gain reduction");
    compMeter.setTooltip ("Gain reduction (0-12 dB)");
    addAndMakeVisible (compMeter);

    // De-harsh
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

    // Air
    attach (air, id::airAmount, "Air: open, breathy top end that closes on S and T sounds");
    attach (deEss, id::airDeEss, "De-ess: turn the air into a gentle cut on S and T sounds");
    sibilanceLight.setTooltip ("Lights up on S and T sounds");
    addAndMakeVisible (sibilanceLight);

    // Output
    attach (input, id::inputGain, "Input gain");
    attach (mix, id::mix, "Mix: blend the processed vocal with the untouched one (latency-matched)");
    attach (output, id::outputGain, "Output gain");

    for (auto* k : { &lowGain, &midGain, &highGain, &input, &output })
        k->setBipolar (true);
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
    display.setBounds (margin, displayTop, getWidth() - margin * 2, displayHeight);

    // Row A, left: Preamp — Drive, then EQ columns (selector under each gain).
    place (drive, margin, rowAKnobY, 100);
    const int eqX = drive.getRight() + 8;
    const int stepperY = rowAKnobY + 52 + 40;
    const int inset = 12;
    highPass.setBounds (eqX + inset, rowAKnobY + 34, eqColumnWidth - inset * 2, 20);
    place (lowGain, eqX + eqColumnWidth, rowAKnobY, eqColumnWidth);
    lowFreq.setBounds (eqX + eqColumnWidth + inset, stepperY, eqColumnWidth - inset * 2, 20);
    place (midGain, eqX + eqColumnWidth * 2, rowAKnobY, eqColumnWidth);
    midFreq.setBounds (eqX + eqColumnWidth * 2 + inset, stepperY, eqColumnWidth - inset * 2, 20);
    place (highGain, eqX + eqColumnWidth * 3, rowAKnobY, eqColumnWidth);

    // Row A, right: Silk comp.
    makeup.setBounds (rightColumnX + 136, rowATitleY + 3, makeup.getIdealWidth(), 22);
    int x = rightColumnX;
    for (auto* knob : { &threshold, &ratio, &compAttack, &compRelease, &compMix })
    {
        place (*knob, x, rowAKnobY + 4, smallColumn);
        x += smallColumn;
    }
    compMeter.setBounds (x + 8, rowAKnobY + 6, 7, 78);

    // Row B, left: De-harsh — switches beside the title, then knobs.
    x = margin + 112;
    const int pillY = rowBTitleY + 3, pillH = 22;
    mode.setBounds (x, pillY, mode.getIdealWidth(), pillH);
    x = mode.getRight() + 12;
    for (auto* pill : { &glare, &body, &delta, &sidechain })
    {
        pill->setBounds (x, pillY, pill->getIdealWidth(), pillH);
        x = pill->getRight() + 6;
    }

    place (depth, margin, rowBKnobY, 100);
    x = depth.getRight() + 8;
    for (auto* knob : { &detail, &attack, &release, &maxCut })
    {
        place (*knob, x, rowBKnobY + 4, 96);
        x += 96;
    }

    // Row B, middle: Air.
    sibilanceLight.setBounds (rightColumnX + 52, rowBTitleY + 5, 20, 20);
    place (air, rightColumnX, rowBKnobY + 4, smallColumn + 8);
    place (deEss, rightColumnX + smallColumn + 8, rowBKnobY + 4, smallColumn + 8);

    // Row B, right: Output.
    x = outputX;
    for (auto* knob : { &input, &mix, &output })
    {
        place (*knob, x, rowBKnobY + 4, smallColumn);
        x += smallColumn;
    }
}

std::vector<juce::Component*> LinenAudioProcessorEditor::Canvas::dimmable()
{
    return { &drive, &lowGain, &midGain, &highGain, &highPass, &lowFreq, &midFreq,
             &threshold, &ratio, &compAttack, &compRelease, &compMix, &makeup, &compMeter,
             &air, &deEss, &sibilanceLight, &input, &mix, &output };
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

    for (auto* c : dimmable())
        c->setAlpha (dim);

    repaint (0, rowATitleY - 4, getWidth(), 40);
    repaint (rightColumnX - 4, rowBTitleY - 4, getWidth() - rightColumnX, 40);
    repaint (margin + 100, rowAKnobY + 52, rightColumnX - margin - 100, 40);
}

void LinenAudioProcessorEditor::Canvas::paintSectionTitle (juce::Graphics& g, const juce::String& title, int x, int y, float alpha)
{
    g.setColour (linen::palette::espresso.withMultipliedAlpha (alpha));
    g.setFont (linen::fonts::serifMedium (24.0f, 0.01f));
    g.drawText (title, juce::Rectangle<int> (x, y, 220, 28), juce::Justification::centredLeft);
}

void LinenAudioProcessorEditor::Canvas::paint (juce::Graphics& g)
{
    using namespace linen::palette;

    weave.draw (g, getLocalBounds());
    paintHeader (g);

    paintSectionTitle (g, "Preamp", margin, rowATitleY, dim);
    paintSectionTitle (g, "Silk comp", rightColumnX, rowATitleY, dim);
    paintSectionTitle (g, "De-harsh", margin, rowBTitleY, 1.0f);
    paintSectionTitle (g, "Air", rightColumnX, rowBTitleY, dim);
    paintSectionTitle (g, "Output", outputX + 4, rowBTitleY, dim);

    // The high-pass column has no gain knob; caption it where the others' captions sit.
    {
        const int eqX = margin + 100 + 8;
        g.setColour (ink (0.62f * dim));
        g.setFont (linen::fonts::sansMedium (11.0f, 0.16f));
        g.drawText ("HIGH-PASS", juce::Rectangle<int> (eqX, rowAKnobY + 64, eqColumnWidth, 16), juce::Justification::centred);
    }

    // Quiet rules between the sections.
    drawVerticalRule (g, (float) rightColumnX - 20.0f, (float) rowATitleY, (float) rowAKnobY + 120.0f, champagne.withAlpha (0.45f));
    drawVerticalRule (g, (float) rightColumnX - 20.0f, (float) rowBTitleY, (float) rowBKnobY + 120.0f, champagne.withAlpha (0.45f));
    drawVerticalRule (g, (float) outputX - 14.0f, (float) rowBTitleY, (float) rowBKnobY + 120.0f, champagne.withAlpha (0.45f));
    drawHairline (g, (float) margin, (float) (getWidth() - margin), (float) rowBTitleY - 16.0f, champagne.withAlpha (0.35f));

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
    g.drawText (juce::String (juce::CharPointer_UTF8 ("PREAMP  \xc2\xb7  DE-HARSH  \xc2\xb7  SILK COMP  \xc2\xb7  AIR")),
                juce::Rectangle<int> (getWidth() - margin - 460, 40, 460, 20), juce::Justification::centredRight);

    drawHairline (g, (float) margin, (float) (getWidth() - margin), 100.0f, champagne.withAlpha (0.55f));
}
