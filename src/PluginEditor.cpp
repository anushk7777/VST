#include "PluginEditor.h"
#include "ui/Palette.h"

namespace
{
    namespace id = linen::params::id;

    constexpr int margin = 32;
    constexpr int displayTop = 112, displayHeight = 196;

    // Cards: Preamp | Silk comp on top, De-harsh | Air | Output below.
    constexpr int rowATop = 320, rowAHeight = 168, rowAKnobY = 368;
    constexpr int rowBTop = 500, rowBHeight = 170, rowBKnobY = 548;
    constexpr int leftX = 22, leftRight = 582;
    constexpr int compX = 594, airX = 594, airRight = 776, outputX = 788, rightEdge = 1018;
    constexpr int eqColumnWidth = 100;

    void drawHairline (juce::Graphics& g, float x0, float x1, float y, juce::Colour colour)
    {
        // Fades out at both ends, like a printed rule.
        juce::ColourGradient grad (colour.withAlpha (0.0f), x0, y, colour.withAlpha (0.0f), x1, y, false);
        grad.addColour (0.12, colour);
        grad.addColour (0.88, colour);
        g.setGradientFill (grad);
        g.fillRect (juce::Rectangle<float> (x0, y, x1 - x0, 1.0f));
    }

    juce::RangedAudioParameter& parameter (LinenAudioProcessor& p, const char* paramId)
    {
        auto* result = p.state.getParameter (paramId);
        jassert (result != nullptr);
        return *result;
    }

    void place (linen::LinenKnob& knob, int left, int top, int width)
    {
        knob.setBounds (left, top, width, (int) knob.getKnobDiameter() + 34);
    }

    juce::Font titleFont() { return linen::fonts::serifMedium (24.0f, 0.01f); }
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
    g.fillAll (linen::palette::surface);
}

void LinenAudioProcessorEditor::resized()
{
    canvas.setBounds (0, 0, designWidth, designHeight);
    canvas.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) designWidth));
}

//==============================================================================
LinenAudioProcessorEditor::Canvas::PresetBox::PresetBox (LinenAudioProcessor& p) : processor (p)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void LinenAudioProcessorEditor::Canvas::PresetBox::paint (juce::Graphics& g)
{
    using namespace linen::palette;
    const auto r = getLocalBounds().toFloat().reduced (0.5f);

    g.setColour (hovering ? ivory : ivory.withAlpha (0.7f));
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (hovering ? champagneDeep.withAlpha (0.8f) : rim);
    g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 0.8f);

    g.setColour (espresso);
    g.setFont (linen::fonts::serifItalic (17.0f));
    g.drawText (processor.getProgramName (processor.getCurrentProgram()), r, juce::Justification::centred, false);

    // A small chevron: this opens a list.
    const float cx = r.getRight() - 16.0f, cy = r.getCentreY();
    juce::Path chevron;
    chevron.startNewSubPath (cx - 3.5f, cy - 1.5f);
    chevron.lineTo (cx, cy + 2.0f);
    chevron.lineTo (cx + 3.5f, cy - 1.5f);
    g.setColour (hovering ? cobalt.withAlpha (0.9f) : champagneDeep.withAlpha (0.7f));
    g.strokePath (chevron, juce::PathStrokeType (1.1f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void LinenAudioProcessorEditor::Canvas::PresetBox::mouseUp (const juce::MouseEvent& e)
{
    if (! e.mouseWasClicked())
        return;

    juce::PopupMenu menu;
    for (int i = 0; i < processor.getNumPrograms(); ++i)
        menu.addItem (i + 1, processor.getProgramName (i), true, i == processor.getCurrentProgram());

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (getWidth()),
                        [safe = juce::Component::SafePointer<PresetBox> (this)] (int result)
                        {
                            if (safe != nullptr && result > 0)
                            {
                                safe->processor.loadPreset (result - 1);
                                safe->repaint();
                            }
                        });
}

//==============================================================================
LinenAudioProcessorEditor::Canvas::Canvas (LinenAudioProcessor& p)
    : processor (p),
      display (p.getDeharshDisplay(), parameter (p, id::deharshFocusLow), parameter (p, id::deharshFocusHigh)),
      presetBox (p),
      listenButton (p.getListenSession(), [&p] { p.startListening(); }),
      highPass (parameter (p, id::eqHighPass)),
      lowFreq (parameter (p, id::eqLowFreq)),
      midFreq (parameter (p, id::eqMidFreq)),
      compMeter (linen::LinenMeter::Style::bar, [&p] { return p.getCompReductionDb(); }, 12.0f),
      mode (parameter (p, id::deharshMode), { "Soft", "Hard" }),
      preampLamp ([this] { return juce::jmin (1.0f, 1.3f * param (id::preampDrive) * presence() + 0.15f * presence()); }, linen::palette::cobalt),
      compLamp ([&p] { return p.getCompReductionDb() / 6.0f; }, linen::palette::cobalt),
      deharshLamp ([&p]
                   {
                       float deepest = 0.0f;
                       for (auto& c : p.getDeharshDisplay().cutDb)
                           deepest = juce::jmax (deepest, c.load (std::memory_order_relaxed));
                       return deepest / 6.0f;
                   }, linen::palette::cobalt),
      airLamp ([this, &p] { return juce::jmax (param (id::airAmount) * presence() * 1.4f, p.getSibilance()); }, linen::palette::cobalt),
      outputLamp ([this] { return presence(); }, linen::palette::cobalt),
      brandLamp ([this] { return 0.3f + 0.5f * presence(); }, linen::palette::cobalt),
      painWatch (parameter (p, id::pain), [this] (float v)
                 {
                     scorch = v;
                     repaint();
                 }),
      vblank (this, [this] (double t) { updateDimming (t); })
{
    using namespace linen::palette;

    cards[0].title = "Preamp";    cards[0].descriptor = juce::String (juce::CharPointer_UTF8 ("WARMTH  \xc2\xb7  WEIGHT"));
    cards[1].title = "Silk comp";
    cards[2].title = "De-harsh";  cards[2].dims = false;
    cards[3].title = "Air";       cards[3].descriptor = "BREATH";
    cards[4].title = "Output";

    addAndMakeVisible (display);
    display.setContextSource ([this, &p]
    {
        linen::SpectrumDisplay::Context c;
        c.pain = param (id::pain);
        c.stress = p.getPainStress();
        c.listening = p.getListenSession().getState() == ListenSession::State::listening ? p.getListenSession().getProgress() : -1.0f;
        return c;
    });

    // Header.
    addAndMakeVisible (presetBox);
    presetBox.setTooltip ("Factory presets");
    addAndMakeVisible (listenButton);
    listenButton.onFinished = [this] (bool ok, const juce::String& message) { display.showMessage (message, ok); };

    attach (silk, id::silk, "Silk: one knob for the whole chain. Up adds drive, De-harsh, compression, Cotton and Air "
                            "together at an even level; down pulls back toward transparent. 50% = your settings as they are");
    attach (pain, id::pain, "Pain: raw ache that only bites when the singer pushes. Belts crack and tremble, quiet lines stay fragile, "
                            "and De-harsh keeps the fizz in check");
    silk.setBipolar (true);
    silk.setGlow ([this]
    {
        const float breathe = 0.85f + 0.15f * (float) std::sin (clock * 1.3);
        return linen::LinenKnob::Glow { (0.2f + 0.8f * presence()) * (0.35f + 0.65f * param (id::silk)) * breathe,
                                        champagneDeep, 0.0f };
    });
    pain.setGlow ([this, &p]
    {
        const float amount = param (id::pain), stress = p.getPainStress();
        return linen::LinenKnob::Glow { amount * (0.3f + 0.7f * stress), crimson, amount * (0.35f + 0.65f * stress) };
    });

    // Preamp + EQ.
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

    // Silk compressor.
    attach (cotton, id::compCotton, "Cotton: the signature texture. While it compresses it adds soft diode-bridge harmonics, "
                                    "low-mid warmth and a softer top, so loud lines turn round and warm while quiet ones stay open");
    attach (threshold, id::compThreshold, "Threshold: where the compressor starts to hold the voice");
    attach (ratio, id::compRatio, "Ratio: 1.5-2.5 is invisible, 4+ is firm");
    attach (compAttack, id::compAttack, "Attack: slower lets consonants through for a natural edge");
    attach (compRelease, id::compRelease, "Release: the fast half. The other half lets go slowly, slower still after long held phrases, like an optical cell");
    attach (compMix, id::compMix, "Blend: parallel compression, mixed with the uncompressed voice");
    attach (makeup, id::compMakeup, "Auto gain: make up for the average gain reduction");
    compMeter.setTooltip ("Gain reduction (0-12 dB)");
    addAndMakeVisible (compMeter);

    // De-harsh.
    addAndMakeVisible (mode);
    mode.setTooltip ("Soft: transparent, adapts to the voice's natural tone. Hard: firmer, catches more.");
    attach (glare, id::deharshGlare, "Glare focus: lean on the 2.5-5.5 kHz region where harshness hurts most");
    attach (body, id::deharshBody, "Body protect: never cut the sung fundamental, so the voice keeps its weight");
    attach (delta, id::deharshDelta, "Delta: hear only what De-harsh is removing");
    attach (sidechain, id::deharshSidechain, "Key: listen to the sidechain input instead of the vocal");
    attach (depth, id::deharshDepth, "Depth: how much of each harsh resonance is removed");
    attach (detail, id::deharshDetail, "Detail: low = broad, gentle cuts; high = narrow, picky cuts");
    attach (attack, id::deharshAttack, "Attack: how fast cuts close in");
    attach (release, id::deharshRelease, "Release: how slowly cuts let go");
    attach (maxCut, id::deharshMaxCut, "Max cut: the deepest any single frequency can be cut");

    // Air and Output.
    attach (air, id::airAmount, "Air: open, breathy top end that closes on S and T sounds");
    attach (deEss, id::airDeEss, "De-ess: turn the air into a gentle cut on S and T sounds");
    attach (input, id::inputGain, "Input gain");
    attach (mix, id::mix, "Mix: blend the processed vocal with the untouched one (latency-matched)");
    attach (output, id::outputGain, "Output gain");

    for (auto* k : { &lowGain, &midGain, &highGain, &input, &output })
        k->setBipolar (true);

    // Lamps.
    preampLamp.setTooltip ("Lights as the preamp works");
    compLamp.setTooltip ("Lights with gain reduction");
    deharshLamp.setTooltip ("Lights as De-harsh cuts");
    airLamp.setTooltip ("Lights with air, flares on S and T sounds");
    outputLamp.setTooltip ("Signal");
    for (auto* lamp : { &preampLamp, &compLamp, &deharshLamp, &airLamp, &outputLamp, &brandLamp })
        addAndMakeVisible (*lamp);
}

float LinenAudioProcessorEditor::Canvas::param (const char* paramId) const
{
    return processor.state.getRawParameterValue (paramId)->load();
}

float LinenAudioProcessorEditor::Canvas::presence() const
{
    const float db = juce::Decibels::gainToDecibels (processor.getInputLevel(), -100.0f);
    return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 40.0f);
}

void LinenAudioProcessorEditor::Canvas::attach (linen::LinenKnob& knob, const char* paramId, const juce::String& tooltip)
{
    auto& p = parameter (processor, paramId);
    sliderAttachments.push_back (std::make_unique<SliderAttachment> (processor.state, paramId, knob));
    knob.setDoubleClickReturnValue (true, p.convertFrom0to1 (p.getDefaultValue()));
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
    cards[0].bounds = juce::Rectangle<int>::leftTopRightBottom (leftX, rowATop, leftRight, rowATop + rowAHeight);
    cards[1].bounds = juce::Rectangle<int>::leftTopRightBottom (compX, rowATop, rightEdge, rowATop + rowAHeight);
    cards[2].bounds = juce::Rectangle<int>::leftTopRightBottom (leftX, rowBTop, leftRight, rowBTop + rowBHeight);
    cards[3].bounds = juce::Rectangle<int>::leftTopRightBottom (airX, rowBTop, airRight, rowBTop + rowBHeight);
    cards[4].bounds = juce::Rectangle<int>::leftTopRightBottom (outputX, rowBTop, rightEdge, rowBTop + rowBHeight);

    // Each section's lamp sits just after its title.
    linen::LinenLamp* lamps[] { &preampLamp, &compLamp, &deharshLamp, &airLamp, &outputLamp };
    for (size_t i = 0; i < cards.size(); ++i)
    {
        const int titleWidth = (int) std::ceil (juce::GlyphArrangement::getStringWidth (titleFont(), cards[i].title));
        lamps[i]->setBounds (cards[i].bounds.getX() + 16 + titleWidth + 6, cards[i].bounds.getY() + 13, 22, 22);
    }

    display.setBounds (margin, displayTop, getWidth() - margin * 2, displayHeight);

    // Header.
    presetBox.setBounds (getWidth() / 2 - 120, 26, 240, 28);
    listenButton.setBounds (getWidth() / 2 - 72, 62, 144, 28);
    place (pain, rightEdge - 190, 6, 90);
    place (silk, rightEdge - 96, 6, 90);
    brandLamp.setBounds (margin + 196, 49, 22, 22);

    // Preamp: Drive, then EQ columns (selector under each gain).
    place (drive, leftX + 12, rowAKnobY, 100);
    const int eqX = leftX + 118;
    const int stepperY = rowAKnobY + 52 + 40;
    const int inset = 12;
    highPass.setBounds (eqX + inset, rowAKnobY + 34, eqColumnWidth - inset * 2, 20);
    place (lowGain, eqX + eqColumnWidth, rowAKnobY, eqColumnWidth);
    lowFreq.setBounds (eqX + eqColumnWidth + inset, stepperY, eqColumnWidth - inset * 2, 20);
    place (midGain, eqX + eqColumnWidth * 2, rowAKnobY, eqColumnWidth);
    midFreq.setBounds (eqX + eqColumnWidth * 2 + inset, stepperY, eqColumnWidth - inset * 2, 20);
    place (highGain, eqX + eqColumnWidth * 3, rowAKnobY, eqColumnWidth);

    // Silk comp: Cotton leads, then the finer controls and the GR meter.
    makeup.setBounds (rightEdge - 16 - makeup.getIdealWidth(), rowATop + 12, makeup.getIdealWidth(), 22);
    place (cotton, compX + 4, rowAKnobY - 4, 88);
    int x = cotton.getRight();
    for (auto* knob : { &threshold, &ratio, &compAttack, &compRelease, &compMix })
    {
        place (*knob, x, rowAKnobY + 4, 60);
        x += 60;
    }
    compMeter.setBounds (x + 4, rowAKnobY + 8, 7, 70);

    // De-harsh: switches across the card's header, then the knobs.
    x = deharshLamp.getRight() + 8;
    const int pillY = rowBTop + 12, pillH = 22;
    mode.setBounds (x, pillY, mode.getIdealWidth(), pillH);
    x = mode.getRight() + 10;
    for (auto* pill : { &glare, &body, &delta, &sidechain })
    {
        pill->setBounds (x, pillY, pill->getIdealWidth(), pillH);
        x = pill->getRight() + 5;
    }

    place (depth, leftX + 12, rowBKnobY, 100);
    x = leftX + 118;
    for (auto* knob : { &detail, &attack, &release, &maxCut })
    {
        place (*knob, x, rowBKnobY + 4, 100);
        x += 100;
    }

    // Air and Output.
    place (air, airX + 6, rowBKnobY + 4, 84);
    place (deEss, airX + 92, rowBKnobY + 4, 84);
    x = outputX + 6;
    for (auto* knob : { &input, &mix, &output })
    {
        place (*knob, x, rowBKnobY + 6, 72);
        x += 72;
    }
}

std::vector<juce::Component*> LinenAudioProcessorEditor::Canvas::dimmable()
{
    return { &drive, &lowGain, &midGain, &highGain, &highPass, &lowFreq, &midFreq,
             &cotton, &threshold, &ratio, &compAttack, &compRelease, &compMix, &makeup, &compMeter,
             &air, &deEss, &input, &mix, &output, &preampLamp, &compLamp, &airLamp, &outputLamp };
}

void LinenAudioProcessorEditor::Canvas::updateDimming (double timestampSeconds)
{
    const float dt = lastTick > 0.0 ? (float) juce::jlimit (0.0, 0.1, timestampSeconds - lastTick) : 0.0f;
    lastTick = timestampSeconds;
    clock = timestampSeconds;

    const float target = delta.getToggleState() ? 0.35f : 1.0f;
    if (std::abs (target - dim) < 0.002f)
        return;

    // ~180 ms ease-out.
    dim += (target - dim) * (1.0f - std::exp (-dt / 0.06f));
    if (std::abs (target - dim) < 0.002f)
        dim = target;

    for (auto* c : dimmable())
        c->setAlpha (dim);

    for (auto& card : cards)
        if (card.dims)
            repaint (card.bounds.expanded (20));
}

void LinenAudioProcessorEditor::Canvas::paintCard (juce::Graphics& g, Card& card)
{
    using namespace linen::palette;

    const auto r = card.bounds.toFloat();
    const float alpha = card.dims ? dim : 1.0f;
    juce::Path shape;
    shape.addRoundedRectangle (r, 18.0f);

    card.shadow.render (g, shape);
    g.setColour (linen::palette::card.withAlpha (0.66f));
    g.fillPath (shape);

    // Top-lit edge and a hairline rim.
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.75f), 0.0f, r.getY(),
                                             juce::Colours::white.withAlpha (0.0f), 0.0f, r.getY() + 26.0f, false));
    g.strokePath (shape, juce::PathStrokeType (1.2f));
    g.setColour (rim.withAlpha (0.55f));
    g.drawRoundedRectangle (r.reduced (0.5f), 18.0f, 0.7f);

    g.setColour (espresso.withMultipliedAlpha (alpha));
    g.setFont (titleFont());
    g.drawText (card.title, juce::Rectangle<float> (r.getX() + 16.0f, r.getY() + 8.0f, 220.0f, 28.0f), juce::Justification::centredLeft);

    if (card.descriptor.isNotEmpty())
    {
        g.setColour (ink (0.38f * alpha));
        g.setFont (linen::fonts::sans (8.5f, 0.24f));
        g.drawText (card.descriptor, juce::Rectangle<float> (r.getRight() - 236.0f, r.getY() + 14.0f, 220.0f, 16.0f),
                    juce::Justification::centredRight);
    }
}

void LinenAudioProcessorEditor::Canvas::paint (juce::Graphics& g)
{
    using namespace linen::palette;

    backdrop.draw (g, getLocalBounds());
    linen::Backdrop::drawScorch (g, getLocalBounds().toFloat(), scorch);
    paintHeader (g);

    for (auto& card : cards)
        paintCard (g, card);

    // The high-pass column has no gain knob; caption it where the others' captions sit.
    {
        g.setColour (ink (0.62f * dim));
        g.setFont (linen::fonts::sansMedium (11.0f, 0.16f));
        g.drawText ("HIGH-PASS", juce::Rectangle<int> (leftX + 118, rowAKnobY + 64, eqColumnWidth, 16), juce::Justification::centred);
    }

    drawHairline (g, (float) margin, (float) (getWidth() - margin), (float) getHeight() - 38.0f, champagne.withAlpha (0.45f));

    g.setColour (ink (0.38f));
    g.setFont (linen::fonts::sans (9.5f, 0.22f));
    g.drawText ("ANUSHK DUA", juce::Rectangle<int> (margin, getHeight() - 30, 200, 20), juce::Justification::centredLeft);
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

    drawHairline (g, (float) margin, (float) (getWidth() - margin), 102.0f, champagne.withAlpha (0.55f));
}
