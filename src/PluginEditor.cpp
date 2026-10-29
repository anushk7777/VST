#include "PluginEditor.h"
#include "ui/Palette.h"

namespace
{
    namespace id = linen::params::id;

    constexpr int margin = 32;
    constexpr int displayTop = 112, displayHeight = 170;

    // Faceplates: Tone | Preamp | Dua Wox on top, De-harsh | Air | Output below.
    constexpr int rowATop = 296, rowABottom = 534;
    constexpr int rowBTop = 546, rowBBottom = 758;
    constexpr int leftX = 22, toneRight = 238, preampX = 250, leftRight = 600;
    constexpr int compX = 612, airX = 612, airRight = 800, outputX = 812, rightEdge = 1178;

    // Inside Dua Wox: TUBE slot | VU | OPTO slot.
    constexpr int tubeX = compX + 12, vuX = compX + 206, optoX = compX + 394;
    constexpr int slotTop = rowATop + 44;

    // Settings the EQ curve on the display is drawn from.
    const char* const eqParameterIds[] { id::toneOn, id::toneLowFreq, id::toneLowGain, id::toneHighFreq, id::toneHighGain,
                                         id::preampOn, id::eqLowFreq, id::eqLowGain, id::eqMidFreq, id::eqMidGain, id::eqHighGain };

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

    /** A knob in a column: centred on the column, its ring's top at top. */
    void place (linen::LinenKnob& knob, int left, int top, int width)
    {
        const int w = juce::jmax (width, knob.getFootprint());
        knob.setBounds (left + (width - w) / 2, top, w, knob.getPreferredHeight());
    }

    juce::Font titleFont() { return linen::fonts::display (17.0f, 0.03f); }
    juce::Font wordmarkFont() { return linen::fonts::display (34.0f, 0.42f); }

    juce::String powerTip (const juce::String& section)
    {
        return section + " in/out. Out is an exact bypass: your settings stay, but nothing they do is heard.";
    }

    /** A small machined screw head, its slot at its own angle. */
    void drawScrew (juce::Graphics& g, juce::Point<float> c, float slotAngle, float alpha)
    {
        using namespace linen::palette;
        const auto head = juce::Rectangle<float> (7.0f, 7.0f).withCentre (c);
        g.setColour (espresso.withAlpha (0.18f * alpha));
        g.fillEllipse (head.translated (0.5f, 1.0f).expanded (0.6f));
        g.setGradientFill (juce::ColourGradient (chromeLight.withMultipliedAlpha (alpha), head.getX(), head.getY(),
                                                 chromeDark.withMultipliedAlpha (alpha), head.getRight(), head.getBottom(), false));
        g.fillEllipse (head);
        g.setColour (chromeDark.withAlpha (0.8f * alpha));
        g.drawEllipse (head, 0.5f);
        const auto d = juce::Point<float> (std::cos (slotAngle), std::sin (slotAngle)) * 2.6f;
        g.setColour (espresso.withAlpha (0.7f * alpha));
        g.drawLine ({ c - d, c + d }, 1.1f);
        g.setColour (juce::Colours::white.withAlpha (0.5f * alpha));
        g.drawLine ({ c - d + juce::Point<float> (0.3f, 0.6f), c + d + juce::Point<float> (0.3f, 0.6f) }, 0.4f);
    }
}

//==============================================================================
LinenAudioProcessorEditor::LinenAudioProcessorEditor (LinenAudioProcessor& p)
    : AudioProcessorEditor (p),
      canvas (p)
{
    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (canvas);

    // Opens at 90% of the design size, which fits a laptop screen; drag the corner to grow it.
    setResizable (true, true);
    setResizeLimits (designWidth * 3 / 4, designHeight * 3 / 4, designWidth * 2, designHeight * 2);
    getConstrainer()->setFixedAspectRatio ((double) designWidth / (double) designHeight);
    setSize (designWidth * 9 / 10, designHeight * 9 / 10);
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
    g.setFont (linen::fonts::displayMedium (12.0f, 0.04f));
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
      lowFreq (parameter (p, id::eqLowFreq)),
      midFreq (parameter (p, id::eqMidFreq)),
      vuMeter ([this, &p] { return isOn (id::compOn) ? p.getCompReductionDb() : 0.0f; },
               [this, &p] { return isOn (id::compOn) ? p.getTubeReductionDb() : 0.0f; },
               [this, &p] { return isOn (id::compOn) ? p.getOptoReductionDb() : 0.0f; }),
      mode (parameter (p, id::deharshMode), { "Soft", "Hard" }),
      deharshKey (parameter (p, id::deharshKey)),
      deharshScale (parameter (p, id::deharshScale), { "Maj", "Min" }),
      heroKey (parameter (p, id::deharshKey)),
      heroScale (parameter (p, id::deharshScale), { "Major", "Minor" }),
      toneLamp ([this]
                {
                    if (! isOn (id::toneOn))
                        return 0.0f;
                    const float shaping = (std::abs (param (id::toneLowGain)) + std::abs (param (id::toneHighGain))) / 6.0f;
                    return juce::jmin (1.0f, shaping * (0.3f + 0.7f * presence()));
                }, linen::palette::cobalt),
      preampLamp ([this]
                  {
                      return isOn (id::preampOn) ? juce::jmin (1.0f, 1.3f * param (id::preampDrive) * presence() + 0.15f * presence()) : 0.0f;
                  }, linen::palette::cobalt),
      compLamp ([this, &p] { return isOn (id::compOn) ? p.getCompReductionDb() / 6.0f : 0.0f; }, linen::palette::cobalt),
      deharshLamp ([this, &p]
                   {
                       if (! isOn (id::deharshOn))
                           return 0.0f;
                       float deepest = 0.0f;
                       for (auto& c : p.getDeharshDisplay().cutDb)
                           deepest = juce::jmax (deepest, c.load (std::memory_order_relaxed));
                       return deepest / 6.0f;
                   }, linen::palette::cobalt),
      airLamp ([this, &p]
               {
                   return isOn (id::airOn) ? juce::jmax (param (id::airAmount) * presence() * 1.4f, p.getSibilance()) : 0.0f;
               }, linen::palette::cobalt),
      outputLamp ([this] { return presence(); }, linen::palette::cobalt),
      tubeLamp ([this, &p] { return isOn (id::compOn) && isOn (id::adcTubeOn) ? p.getTubeReductionDb() / 4.0f : 0.0f; }, linen::palette::cobalt),
      optoLamp ([this, &p] { return isOn (id::compOn) && isOn (id::adcOptoOn) ? p.getOptoReductionDb() / 4.0f : 0.0f; }, linen::palette::cobalt),
      brandLamp ([this] { return 0.3f + 0.5f * presence(); }, linen::palette::cobalt),
      painWatch (parameter (p, id::pain), [this] (float v)
                 {
                     scorch = v;
                     repaint();
                 }),
      vblank (this, [this] (double t) { updateDimming (t); })
{
    using namespace linen::palette;

    auto& [toneCard, preampCard, compCard, deharshCard, airCard, outputCard] = cards;
    toneCard.title = "Tone";          toneCard.powerId = id::toneOn;
    preampCard.title = "Preamp";      preampCard.powerId = id::preampOn;
    preampCard.descriptor = juce::String (juce::CharPointer_UTF8 ("WARMTH  \xc2\xb7  WEIGHT"));
    compCard.title = "Dua Wox";       compCard.powerId = id::compOn;
    deharshCard.title = "De-harsh";   deharshCard.powerId = id::deharshOn;  deharshCard.dims = false;
    airCard.title = "Air";            airCard.powerId = id::airOn;          airCard.descriptor = "BREATH";
    outputCard.title = "Output";

    toneCard.members = { &toneLow, &toneHigh, &toneLowFreq, &toneHighFreq, &toneLamp };
    preampCard.members = { &drive, &lowGain, &midGain, &highGain, &lowFreq, &midFreq, &preampLamp };
    compCard.members = { &threshold, &ratio, &compAttack, &compRelease, &optoReduction, &warmth, &compMix, &makeup,
                         &tubeSwitch, &optoSwitch, &vuMeter, &compLamp, &tubeLamp, &optoLamp };
    deharshCard.members = { &mode, &glare, &body, &delta, &sidechain, &depth, &detail, &attack, &release, &maxCut,
                            &deharshKey, &deharshScale, &deharshTune, &deharshLamp };
    airCard.members = { &air, &midAir, &deEss, &airLamp };
    outputCard.members = { &input, &mix, &output, &nullTest, &outputLamp };

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

    attach (silk, id::silk, "Silk: one knob for the whole chain. Up adds drive, De-harsh, compression, Warmth and Air "
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

    // Power switches.
    attach (tonePower, id::toneOn, powerTip ("Tone"));
    attach (preampPower, id::preampOn, powerTip ("Preamp"));
    attach (compPower, id::compOn, powerTip ("The Dua Wox compressor"));
    attach (deharshPower, id::deharshOn, powerTip ("De-harsh"));
    attach (airPower, id::airOn, powerTip ("Air"));

    // Tone: shelves that shape the voice before it reaches the preamp.
    attach (toneLow, id::toneLowGain, "Low shelf: take weight out of the lows (or add some) before the preamp. "
                                      "Half the gain at its frequency, all of it a few octaves below");
    attach (toneHigh, id::toneHighGain, "High shelf: calm or open the top before the preamp. "
                                        "Half the gain at its frequency, all of it a few octaves above");
    attach (toneLowFreq, id::toneLowFreq, "Low shelf frequency: drag up or down, or scroll. Shift for fine steps");
    attach (toneHighFreq, id::toneHighFreq, "High shelf frequency: drag up or down, or scroll. Shift for fine steps");
    toneLow.setBipolar (true);
    toneHigh.setBipolar (true);

    // Preamp + EQ.
    attach (drive, id::preampDrive, "Drive: from a whisper of class-A warmth to thick transformer weight. "
                                    "0 = off (exact bypass); about 30% is the house sound");
    attach (lowGain, id::eqLowGain, "Low shelf gain");
    attach (midGain, id::eqMidGain, "Mid gain: a broad, musical bell that narrows as you push it");
    attach (highGain, id::eqHighGain, "High shelf at 12 kHz: air and sheen");
    for (auto [stepper, tip] : { std::pair { &lowFreq, "Low shelf frequency" },
                                 std::pair { &midFreq, "Mid frequency" } })
    {
        stepper->setTooltip (tip);
        addAndMakeVisible (*stepper);
    }

    // The Dua Wox compressor.
    attach (tubeSwitch, id::adcTubeOn, "TUBE in/out: the catch. A variable-mu tube cell with an auto release: "
                                       "single peaks let go quickly, long loud lines slowly. Out is exact");
    attach (threshold, id::compThreshold, "Threshold: where the TUBE starts to lean on the voice, through a wide 12 dB knee");
    attach (ratio, id::compRatio, "Ratio: how firmly the TUBE leans, 1:1 (off) to about 2.3:1, firming a little on the "
                                  "loudest notes. Never a hard squash");
    attach (compAttack, id::compAttack, "Attack: slower lets consonants through for a natural edge");
    attach (compRelease, id::compRelease, "Release: the TUBE's fast recovery. A slower one takes over after long loud lines");
    attach (optoSwitch, id::adcOptoOn, "OPTO in/out: the leveller. A light-cell model: about 10 ms attack, half lets go "
                                       "in 60-100 ms, the rest over seconds, slower after long phrases. Out is exact");
    attach (optoReduction, id::adcOptoReduction, "Peak reduction: how hard the OPTO levels the line. 0 = off. Its knee is "
                                                 "natural and it never passes 3:1");
    attach (warmth, id::compCotton, "Warmth: Dua Wox's tube line stage (mostly 2nd harmonic, at hardware levels), "
                                    "plus a touch of low-mid bloom and a softer top while it holds the voice");
    attach (compMix, id::compMix, "Blend: parallel compression, mixed with the uncompressed voice");
    attach (makeup, id::compMakeup, "Auto gain: give back most of what Dua Wox takes, so it stays at about the same level");
    vuMeter.setTooltip ("Dua Wox gain reduction. The bars show how the TUBE and OPTO share it");
    addAndMakeVisible (vuMeter);

    // De-harsh.
    addAndMakeVisible (mode);
    mode.setTooltip ("Soft: transparent, adapts to the voice's natural tone. Hard: firmer, catches more.");
    attach (glare, id::deharshGlare, "Glare focus: lean on the 2.5-5.5 kHz region where harshness hurts most");
    attach (body, id::deharshBody, "Body protect: never cut the sung fundamental, so the voice keeps its weight");
    attach (delta, id::deharshDelta, "Delta: hear only what De-harsh is removing");
    attach (sidechain, id::deharshSidechain, "Key: listen to the sidechain input instead of the vocal");
    attach (depth, id::deharshDepth, "Depth: how much of each harsh resonance is removed. 0 = off (exact pass-through)");
    attach (detail, id::deharshDetail, "Detail: low = broad, gentle cuts; high = narrow, picky cuts");
    attach (attack, id::deharshAttack, "Attack: how fast cuts close in");
    attach (release, id::deharshRelease, "Release: how slowly cuts let go");
    attach (maxCut, id::deharshMaxCut, "Max cut: the deepest any single frequency can be cut");

    // Air and Output.
    attach (air, id::airAmount, "High Air: opens the top above 9 kHz, lifting it the more it sits quiet under the voice "
                                "(up to +10 dB), and an already bright top or an S hardly at all");
    attach (midAir, id::airMid, "Mid Air: the same for the upper mids above 3 kHz: presence without harshness");
    attach (deEss, id::airDeEss, "De-ess: turn the air into a gentle cut on S and T sounds");

    // Key, in the De-harsh card.
    for (auto* stepper : { &deharshKey, &heroKey })
    {
        stepper->setTooltip ("Key: the song's key. Out-of-key harmonics and rings are tamed, in-key ones lifted a little (needs Tune)");
        addAndMakeVisible (*stepper);
    }
    for (auto* scale : { &deharshScale, &heroScale })
    {
        scale->setTooltip ("The key's scale: major or (natural) minor");
        addAndMakeVisible (*scale);
    }
    attach (deharshTune, id::deharshTune, "Tune: how strongly the Key shapes the voice. 0 = off");

    // The simple view.
    attach (hero, id::linen, "LINEN: one knob for the whole sound. 0 = your settings untouched; turning up brings in "
                             "the preamp, De-harsh, Dua Wox, Air and the Key together, held at the same loudness");
    attach (heroTune, id::deharshTune, "Tune: how strongly the Key shapes the voice. 0 = off");
    attach (heroOutput, id::outputGain, "Output gain");
    heroOutput.setBipolar (true);
    hero.setGlow ([this]
    {
        const float amount = param (id::linen);
        return linen::LinenKnob::Glow { amount * (0.35f + 0.65f * presence()), cobalt, 0.0f };
    });

    // Air in the simple view moves both bands: High, and Mid at 60% of it.
    heroAir.setRange (0.0, 1.0);
    heroAir.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + " %"; };
    heroAir.setTooltip ("Air: opens a dull top and the upper mids (High Air, and Mid Air at 60% of it)");
    heroAir.setDoubleClickReturnValue (true, 0.0);
    heroAirSync = std::make_unique<juce::ParameterAttachment> (parameter (p, id::airAmount),
                                                              [this] (float v) { heroAir.setValue (v, juce::dontSendNotification); });
    heroAirSync->sendInitialUpdate();
    heroAir.onDragStart = [this] { parameter (processor, id::airMid).beginChangeGesture(); heroAirSync->beginGesture(); };
    heroAir.onDragEnd = [this] { heroAirSync->endGesture(); parameter (processor, id::airMid).endChangeGesture(); };
    heroAir.onValueChange = [this]
    {
        const auto v = (float) heroAir.getValue();
        heroAirSync->setValueAsPartOfGesture (v);
        auto& mid = parameter (processor, id::airMid);
        mid.setValueNotifyingHost (mid.convertTo0to1 (0.6f * v));
    };
    addAndMakeVisible (heroAir);

    advancedSwitch.setTooltip ("Advanced: the full console, every section and knob");
    advancedSwitch.onClick = [this] { setAdvanced (advancedSwitch.getToggleState()); };
    addAndMakeVisible (advancedSwitch);
    attach (input, id::inputGain, "Input gain");
    attach (mix, id::mix, "Mix: blend the processed vocal with the untouched one (latency-matched)");
    attach (output, id::outputGain, "Output gain");
    attach (nullTest, id::nullTest, "Null (phase test): plays Linen's output minus your untouched vocal, sample-aligned. "
                                    "Silence means Linen is changing nothing; on any setting you hear exactly what it adds or takes away");

    for (auto* k : { &lowGain, &midGain, &highGain, &input, &output })
        k->setBipolar (true);

    // Lamps.
    toneLamp.setTooltip ("Lights as the Tone shelves shape the voice");
    preampLamp.setTooltip ("Lights as the preamp works");
    compLamp.setTooltip ("Lights with gain reduction");
    tubeLamp.setTooltip ("Lights as the TUBE leans in");
    optoLamp.setTooltip ("Lights as the OPTO levels");
    deharshLamp.setTooltip ("Lights as De-harsh cuts");
    airLamp.setTooltip ("Lights with air, flares on S and T sounds");
    outputLamp.setTooltip ("Signal");
    for (auto* lamp : { &toneLamp, &preampLamp, &compLamp, &deharshLamp, &airLamp, &outputLamp, &tubeLamp, &optoLamp, &brandLamp })
        addAndMakeVisible (*lamp);

    // The display draws the EQ shape; it is redrawn whenever an EQ setting moves.
    display.setEqResponse ([this] (float hz)
    {
        double db = 0.0;
        if (isOn (id::toneOn))
        {
            linen::ToneParams t;
            t.lowHz = param (id::toneLowFreq);
            t.lowGainDb = param (id::toneLowGain);
            t.highHz = param (id::toneHighFreq);
            t.highGainDb = param (id::toneHighGain);
            db += linen::ToneShelves::magnitudeDb (t, 48000.0, hz);
        }
        if (isOn (id::preampOn))
        {
            linen::EqParams e;
            e.lowFreq = juce::roundToInt (param (id::eqLowFreq));
            e.lowGainDb = param (id::eqLowGain);
            e.midFreq = juce::roundToInt (param (id::eqMidFreq));
            e.midGainDb = param (id::eqMidGain);
            e.highGainDb = param (id::eqHighGain);
            db += linen::Eq1073::magnitudeDb (e, 48000.0, hz);
        }
        return (float) db;
    });
    for (auto* eqId : eqParameterIds)
        processor.state.addParameterListener (eqId, this);

    updateDimming (0.0, true);
    setAdvanced (processor.state.state.getProperty ("advancedView", false));
}

void LinenAudioProcessorEditor::Canvas::setAdvanced (bool shouldShowConsole)
{
    advanced = shouldShowConsole;
    advancedSwitch.setToggleState (advanced, juce::dontSendNotification);
    processor.state.state.setProperty ("advancedView", advanced, nullptr);

    for (auto& card : cards)
        for (auto* c : card.members)
            c->setVisible (advanced);
    for (auto* c : std::initializer_list<juce::Component*> { &tonePower, &preampPower, &compPower, &deharshPower, &airPower })
        c->setVisible (advanced);
    for (auto* c : std::initializer_list<juce::Component*> { &hero, &heroKey, &heroScale, &heroTune, &heroAir, &heroOutput })
        c->setVisible (! advanced);
    repaint();
}

LinenAudioProcessorEditor::Canvas::~Canvas()
{
    for (auto* eqId : eqParameterIds)
        processor.state.removeParameterListener (eqId, this);
}

void LinenAudioProcessorEditor::Canvas::parameterChanged (const juce::String&, float)
{
    display.markEqChanged();   // may arrive on the audio thread: it only flags a redraw
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

void LinenAudioProcessorEditor::Canvas::attach (juce::Slider& slider, const char* paramId, const juce::String& tooltip)
{
    auto& p = parameter (processor, paramId);
    sliderAttachments.push_back (std::make_unique<SliderAttachment> (processor.state, paramId, slider));
    slider.setDoubleClickReturnValue (true, p.convertFrom0to1 (p.getDefaultValue()));
    slider.setTooltip (tooltip);
    addAndMakeVisible (slider);
}

void LinenAudioProcessorEditor::Canvas::attach (juce::Button& button, const char* paramId, const juce::String& tooltip)
{
    buttonAttachments.push_back (std::make_unique<ButtonAttachment> (processor.state, paramId, button));
    button.setTooltip (tooltip);
    addAndMakeVisible (button);
}

int LinenAudioProcessorEditor::Canvas::titleX (const Card& card)
{
    return card.bounds.getX() + (card.powerId != nullptr ? 50 : 24);
}

void LinenAudioProcessorEditor::Canvas::resized()
{
    using Rect = juce::Rectangle<int>;
    cards[0].bounds = Rect::leftTopRightBottom (leftX, rowATop, toneRight, rowABottom);
    cards[1].bounds = Rect::leftTopRightBottom (preampX, rowATop, leftRight, rowABottom);
    cards[2].bounds = Rect::leftTopRightBottom (compX, rowATop, rightEdge, rowABottom);
    cards[3].bounds = Rect::leftTopRightBottom (leftX, rowBTop, leftRight, rowBBottom);
    cards[4].bounds = Rect::leftTopRightBottom (airX, rowBTop, airRight, rowBBottom);
    cards[5].bounds = Rect::leftTopRightBottom (outputX, rowBTop, rightEdge, rowBBottom);

    // Each section's switch sits before its title, its lamp just after.
    linen::LinenLamp* lamps[] { &toneLamp, &preampLamp, &compLamp, &deharshLamp, &airLamp, &outputLamp };
    linen::LinenToggle* powers[] { &tonePower, &preampPower, &compPower, &deharshPower, &airPower, nullptr };
    for (size_t i = 0; i < cards.size(); ++i)
    {
        const auto& b = cards[i].bounds;
        if (powers[i] != nullptr)
            powers[i]->setBounds (b.getX() + 19, b.getY() + 4, 26, 38);

        const int titleWidth = (int) std::ceil (juce::GlyphArrangement::getStringWidth (titleFont(), cards[i].title));
        lamps[i]->setBounds (titleX (cards[i]) + titleWidth + 6, b.getY() + 13, 22, 22);
    }

    display.setBounds (margin, displayTop, getWidth() - margin * 2, displayHeight);

    // Header.
    presetBox.setBounds (getWidth() / 2 - 130, 26, 260, 28);
    advancedSwitch.setBounds (getWidth() / 2 + 150, 29, advancedSwitch.getIdealWidth(), 22);
    listenButton.setBounds (getWidth() / 2 - 76, 62, 152, 28);
    place (pain, rightEdge - 186, 6, 90);
    place (silk, rightEdge - 92, 6, 90);
    brandLamp.setBounds (margin + (int) std::ceil (juce::GlyphArrangement::getStringWidth (wordmarkFont(), "LINEN")) + 4, 36, 22, 22);

    // Tone: each shelf's gain, with its frequency underneath.
    const int toneKnobY = rowATop + 72;
    place (toneLow, leftX + 12, toneKnobY, 96);
    place (toneHigh, leftX + 108, toneKnobY, 96);
    toneLowFreq.setBounds (toneLow.getBounds().getCentreX() - 40, toneLow.getBottom() + 6, 80, 20);
    toneHighFreq.setBounds (toneHigh.getBounds().getCentreX() - 40, toneHigh.getBottom() + 6, 80, 20);

    // Preamp: Drive, then the console EQ columns (selector under each gain).
    place (drive, preampX + 6, rowATop + 64, 124);
    const int eqX = preampX + 132, eqColumn = 72, eqKnobY = rowATop + 72;
    place (lowGain, eqX, eqKnobY, eqColumn);
    place (midGain, eqX + eqColumn, eqKnobY, eqColumn);
    place (highGain, eqX + eqColumn * 2, eqKnobY, eqColumn);
    lowFreq.setBounds (eqX + 4, lowGain.getBottom() + 6, eqColumn - 8, 20);
    midFreq.setBounds (eqX + eqColumn + 4, midGain.getBottom() + 6, eqColumn - 8, 20);

    // Dua Wox: the TUBE's four controls | the VU, with Warmth and Blend under it | the OPTO's big knob.
    tubeSwitch.setBounds (tubeX + 1, slotTop - 6, 20, 30);
    tubeLamp.setBounds (tubeX + 70, slotTop - 1, 18, 18);
    place (threshold, tubeX, slotTop + 22, 94);
    place (ratio, tubeX + 94, slotTop + 22, 94);
    place (compAttack, tubeX, threshold.getBottom() - 6, 94);
    place (compRelease, tubeX + 94, ratio.getBottom() - 6, 94);

    vuMeter.setBounds (vuX, slotTop - 4, 180, 112);
    place (warmth, vuX, vuMeter.getBottom() + 2, 90);
    place (compMix, vuX + 90, vuMeter.getBottom() + 2, 90);

    optoSwitch.setBounds (optoX + 1, slotTop - 6, 20, 30);
    optoLamp.setBounds (optoX + 70, slotTop - 1, 18, 18);
    place (optoReduction, optoX, slotTop + 24, rightEdge - 12 - optoX);
    makeup.setBounds (optoX + (rightEdge - 12 - optoX - makeup.getIdealWidth()) / 2, rowABottom - 38, makeup.getIdealWidth(), 22);

    // De-harsh: switches across the card's header, then the knobs.
    int x = deharshLamp.getRight() + 8;
    const int pillY = rowBTop + 12, pillH = 22;
    mode.setBounds (x, pillY, mode.getIdealWidth(), pillH);
    x = mode.getRight() + 10;
    for (auto* pill : { &glare, &body, &delta, &sidechain })
    {
        pill->setBounds (x, pillY, pill->getIdealWidth(), pillH);
        x = pill->getRight() + 5;
    }

    const int rowBKnobY = rowBTop + 74;
    place (depth, leftX + 10, rowBTop + 62, 130);
    x = leftX + 142;
    for (auto* knob : { &detail, &attack, &release, &maxCut })
    {
        place (*knob, x, rowBKnobY + 6, 92);
        x += 92;
    }

    // Key: note and scale, then Tune, in the card's last column.
    deharshKey.setBounds (x + 2, rowBKnobY, 64, 20);
    deharshScale.setBounds (x + 2, rowBKnobY + 26, 64, 20);
    place (deharshTune, x + 2, rowBKnobY + 50, 64);

    // Air, and Output with the phase test at its top right.
    place (air, airX + 2, rowBKnobY + 6, 62);
    place (midAir, airX + 63, rowBKnobY + 6, 62);
    place (deEss, airX + 124, rowBKnobY + 6, 62);

    // The simple view: the LINEN knob in the middle, Key | Air | Output under it.
    simpleArea = juce::Rectangle<int>::leftTopRightBottom (leftX, rowATop, rightEdge, rowBBottom);
    place (hero, getWidth() / 2 - 140, rowATop + 30, 280);
    const int rowY = hero.getBottom() + 30;
    heroKey.setBounds (getWidth() / 2 - 330, rowY + 18, 96, 24);
    heroScale.setBounds (getWidth() / 2 - 330, rowY + 50, 96, 24);
    place (heroTune, getWidth() / 2 - 226, rowY + 6, 96);
    place (heroAir, getWidth() / 2 - 60, rowY, 120);
    place (heroOutput, getWidth() / 2 + 130, rowY + 6, 120);
    nullTest.setBounds (rightEdge - 20 - nullTest.getIdealWidth(), pillY, nullTest.getIdealWidth(), pillH);
    x = outputX + 18;
    for (auto* knob : { &input, &mix, &output })
    {
        place (*knob, x, rowBKnobY + 6, 110);
        x += 110;
    }
}

void LinenAudioProcessorEditor::Canvas::updateDimming (double timestampSeconds, bool instantly)
{
    const float dt = lastTick > 0.0 ? (float) juce::jlimit (0.0, 0.1, timestampSeconds - lastTick) : 0.0f;
    lastTick = timestampSeconds;
    clock = timestampSeconds;

    // Delta dims every card but De-harsh's; a section switched off fades back.
    const bool deharshIn = isOn (id::deharshOn);
    const bool listeningToDelta = delta.getToggleState() && deharshIn;
    delta.setEnabled (deharshIn);

    for (auto& card : cards)
    {
        const float target = (card.dims && listeningToDelta ? 0.35f : 1.0f)
                           * (card.powerId == nullptr || isOn (card.powerId) ? 1.0f : 0.4f);
        if (! instantly && std::abs (target - card.alpha) < 0.002f)
            continue;

        // ~180 ms ease-out.
        card.alpha += (target - card.alpha) * (instantly ? 1.0f : 1.0f - std::exp (-dt / 0.06f));
        if (std::abs (target - card.alpha) < 0.002f)
            card.alpha = target;

        for (auto* c : card.members)
            c->setAlpha (card.alpha);
        repaint (card.bounds.expanded (20));
    }

    // Inside Dua Wox, a slot switched out fades back too (its switch stays bright).
    const auto& adc = cards[2];
    const float tubeAlpha = adc.alpha * (isOn (id::adcTubeOn) ? 1.0f : 0.4f);
    const float optoAlpha = adc.alpha * (isOn (id::adcOptoOn) ? 1.0f : 0.4f);
    for (auto* c : std::initializer_list<juce::Component*> { &threshold, &ratio, &compAttack, &compRelease, &tubeLamp })
        if (std::abs (c->getAlpha() - tubeAlpha) > 0.002f)
            c->setAlpha (tubeAlpha);
    for (auto* c : std::initializer_list<juce::Component*> { &optoReduction, &optoLamp })
        if (std::abs (c->getAlpha() - optoAlpha) > 0.002f)
            c->setAlpha (optoAlpha);
}

void LinenAudioProcessorEditor::Canvas::paintCard (juce::Graphics& g, Card& card, int index)
{
    using namespace linen::palette;

    const auto r = card.bounds.toFloat();
    const float alpha = card.alpha;
    juce::Path shape;
    shape.addRoundedRectangle (r, 18.0f);

    card.shadow.render (g, shape);
    g.setColour (linen::palette::card.withAlpha (0.7f));
    g.fillPath (shape);

    // Top-lit edge and a hairline rim.
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.8f), 0.0f, r.getY(),
                                             juce::Colours::white.withAlpha (0.0f), 0.0f, r.getY() + 26.0f, false));
    g.strokePath (shape, juce::PathStrokeType (1.2f));
    g.setColour (rim.withAlpha (0.6f));
    g.drawRoundedRectangle (r.reduced (0.5f), 18.0f, 0.7f);

    // Screwed down at the corners, each slot at its own angle.
    const float inset = 10.0f;
    const juce::Point<float> corners[] { { r.getX() + inset, r.getY() + inset }, { r.getRight() - inset, r.getY() + inset },
                                         { r.getX() + inset, r.getBottom() - inset }, { r.getRight() - inset, r.getBottom() - inset } };
    for (int i = 0; i < 4; ++i)
        drawScrew (g, corners[i], 0.4f + 1.37f * (float) (index * 4 + i), 0.55f + 0.45f * alpha);

    g.setColour (espresso.withMultipliedAlpha (alpha));
    g.setFont (titleFont());
    g.drawText (card.title, juce::Rectangle<float> ((float) titleX (card), r.getY() + 8.0f, 220.0f, 28.0f), juce::Justification::centredLeft);

    if (card.descriptor.isNotEmpty())
    {
        g.setColour (ink (0.38f * alpha));
        g.setFont (linen::fonts::sans (8.5f, 0.24f));
        g.drawText (card.descriptor, juce::Rectangle<float> (r.getRight() - 246.0f, r.getY() + 14.0f, 220.0f, 16.0f),
                    juce::Justification::centredRight);
    }
}

void LinenAudioProcessorEditor::Canvas::paintSimple (juce::Graphics& g)
{
    using namespace linen::palette;

    // One faceplate, screwed down, with the LINEN knob's light spilling across it.
    simplePlate.bounds = simpleArea;
    paintCard (g, simplePlate, 6);

    const auto centre = hero.getBounds().toFloat().getCentre().withY ((float) hero.getY() + (float) hero.getFootprint() * 0.5f);
    const float amount = param (id::linen);
    if (amount > 0.0f)
    {
        juce::ColourGradient spill (electric.withAlpha (0.16f * amount), centre.x, centre.y,
                                    electric.withAlpha (0.0f), centre.x + 330.0f, centre.y, true);
        g.setGradientFill (spill);
        g.fillEllipse (juce::Rectangle<float> (660.0f, 440.0f).withCentre (centre));
    }

    // Engraved legends: the knob's ends, and the row's names.
    g.setColour (ink (0.45f));
    g.setFont (linen::fonts::sansMedium (10.0f, 0.3f));
    const auto legend = [&g] (const juce::String& text, juce::Rectangle<int> r)
    {
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.drawText (text, r.translated (0, 1), juce::Justification::centred);
        g.setColour (linen::palette::ink (0.5f));
        g.drawText (text, r, juce::Justification::centred);
    };
    legend ("UNTOUCHED", juce::Rectangle<int> (140, 16).withCentre ({ (int) centre.x - 205, (int) centre.y + 88 }));
    legend ("FINISHED", juce::Rectangle<int> (140, 16).withCentre ({ (int) centre.x + 205, (int) centre.y + 88 }));
    legend ("KEY", juce::Rectangle<int> (96, 16).withCentre ({ heroKey.getBounds().getCentreX(), heroKey.getY() - 12 }));

    g.setColour (ink (0.38f));
    g.setFont (linen::fonts::sans (11.0f, 0.06f));
    g.drawText ("ADVANCED, top right, opens the full console", juce::Rectangle<int> (simpleArea.getRight() - 344, simpleArea.getBottom() - 30, 320, 18),
                juce::Justification::centredRight);
}

void LinenAudioProcessorEditor::Canvas::paintSlotLabels (juce::Graphics& g)
{
    using namespace linen::palette;

    const float alpha = cards[2].alpha;
    const auto label = [&] (int x, const char* name, const char* role, bool on)
    {
        const float a = alpha * (on ? 1.0f : 0.45f);
        g.setColour (espresso.withMultipliedAlpha (a));
        g.setFont (linen::fonts::sansMedium (10.0f, 0.24f));
        g.drawText (name, juce::Rectangle<int> (x + 24, slotTop - 2, 46, 20), juce::Justification::centredLeft);
        g.setColour (ink (0.42f * a));
        g.setFont (linen::fonts::sans (11.0f, 0.06f));
        g.drawText (role, juce::Rectangle<int> (x + 92, slotTop - 3, 90, 20), juce::Justification::centredLeft);
    };
    label (tubeX, "TUBE", "the catch", isOn (id::adcTubeOn));
    label (optoX, "OPTO", "the leveller", isOn (id::adcOptoOn));

    // Fine engraved rules between the slots.
    for (const int x : { vuX - 6, optoX - 6 })
    {
        g.setColour (ink (0.1f * alpha));
        g.fillRect ((float) x, (float) slotTop, 1.0f, (float) (rowABottom - slotTop - 14));
        g.setColour (juce::Colours::white.withAlpha (0.6f * alpha));
        g.fillRect ((float) x + 1.0f, (float) slotTop, 1.0f, (float) (rowABottom - slotTop - 14));
    }
}

void LinenAudioProcessorEditor::Canvas::paint (juce::Graphics& g)
{
    using namespace linen::palette;

    backdrop.draw (g, getLocalBounds());
    linen::Backdrop::drawScorch (g, getLocalBounds().toFloat(), scorch);
    paintHeader (g);

    if (advanced)
    {
        for (size_t i = 0; i < cards.size(); ++i)
            paintCard (g, cards[i], (int) i);
        paintSlotLabels (g);
    }
    else
    {
        paintSimple (g);
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
    g.setFont (wordmarkFont());
    g.drawText ("LINEN", juce::Rectangle<int> (margin - 2, 22, 340, 54), juce::Justification::centredLeft);

    g.setColour (ink (0.58f));
    g.setFont (linen::fonts::sans (13.0f, 0.12f));
    g.drawText ("silk for the voice", juce::Rectangle<int> (margin, 72, 340, 22), juce::Justification::centredLeft);

    drawHairline (g, (float) margin, (float) (getWidth() - margin), 102.0f, champagne.withAlpha (0.55f));
}
