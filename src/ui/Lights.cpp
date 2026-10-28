#include "Lights.h"
#include "LinenLookAndFeel.h"
#include "Palette.h"

namespace linen
{

//==============================================================================
LinenLamp::LinenLamp (std::function<float()> brightness, juce::Colour lampColour)
    : source (std::move (brightness)),
      colour (lampColour),
      vblank (this, [this] (double t)
      {
          const float dt = lastTick > 0.0 ? (float) juce::jlimit (0.0, 0.1, t - lastTick) : 1.0f / 60.0f;
          lastTick = t;
          const float target = juce::jlimit (0.0f, 1.0f, source());
          const float next = level + (target - level) * (1.0f - std::exp (-dt / (target > level ? 0.03f : 0.25f)));
          if (std::abs (next - level) > 0.003f)
          {
              level = next;
              repaint();
          }
      })
{
    setInterceptsMouseClicks (true, false);
}

void LinenLamp::drawJewel (juce::Graphics& g, juce::Point<float> centre, float diameter, float level, juce::Colour colour)
{
    using namespace palette;
    const auto jewel = juce::Rectangle<float> (diameter, diameter).withCentre (centre);

    // Bloom.
    if (level > 0.01f)
    {
        juce::ColourGradient bloom (colour.withAlpha (0.5f * level), centre.x, centre.y,
                                    colour.withAlpha (0.0f), centre.x + diameter * 1.4f, centre.y, true);
        g.setGradientFill (bloom);
        g.fillEllipse (jewel.expanded (diameter * 0.9f));
    }

    // Bezel: a thin champagne ring.
    g.setGradientFill (juce::ColourGradient (champagneLight, jewel.getX(), jewel.getY(), champagneDeep, jewel.getRight(), jewel.getBottom(), false));
    g.fillEllipse (jewel.expanded (1.6f));

    // Glass: dark when idle, lit from within when working.
    const auto dark = espresso.interpolatedWith (colour, 0.18f);
    const auto lit = colour.interpolatedWith (juce::Colours::white, 0.35f);
    g.setGradientFill (juce::ColourGradient (dark.interpolatedWith (lit, level).brighter (0.1f), centre.x, centre.y - diameter * 0.2f,
                                             dark.interpolatedWith (colour, level).darker (0.2f), centre.x, jewel.getBottom(), true));
    g.fillEllipse (jewel);

    // Facet highlight.
    g.setColour (juce::Colours::white.withAlpha (0.55f));
    g.fillEllipse (jewel.withSizeKeepingCentre (diameter * 0.38f, diameter * 0.26f).translated (-diameter * 0.12f, -diameter * 0.2f));
}

void LinenLamp::paint (juce::Graphics& g)
{
    drawJewel (g, getLocalBounds().toFloat().getCentre(), 9.0f, level, colour);
}

//==============================================================================
ListenButton::ListenButton (ListenSession& s, std::function<void()> startListening)
    : session (s),
      start (std::move (startListening)),
      vblank (this, [this] (double t) { tick (t); })
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setTooltip ("AI Listen: play your vocal and Linen sets itself up for it in a few seconds. Click again to stop.");
}

void ListenButton::tick (double timestampSeconds)
{
    now = timestampSeconds;
    const auto state = session.getState();

    if (state != lastState)
    {
        if ((state == ListenSession::State::done || state == ListenSession::State::failed) && onFinished != nullptr)
        {
            finishedAt = now;
            onFinished (state == ListenSession::State::done, session.getMessage());
        }
        lastState = state;
    }

    if (state == ListenSession::State::listening || now - finishedAt < 4.0)
        repaint();
}

void ListenButton::mouseUp (const juce::MouseEvent& e)
{
    if (! e.mouseWasClicked())
        return;

    if (session.getState() == ListenSession::State::listening)
        session.cancel();
    else
        start();
    repaint();
}

void ListenButton::paint (juce::Graphics& g)
{
    using namespace palette;

    const auto state = session.getState();
    const bool listening = state == ListenSession::State::listening;
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    const float radius = r.getHeight() * 0.5f;

    // Body: ivory capsule, warming to champagne under the pointer.
    g.setColour (hovering || listening ? ivory : ivory.withAlpha (0.7f));
    g.fillRoundedRectangle (r, radius);
    g.setColour (listening ? cobalt.withAlpha (0.7f) : (hovering ? champagneDeep.withAlpha (0.8f) : rim));
    g.drawRoundedRectangle (r, radius, listening ? 1.1f : 0.8f);

    // The lamp ring.
    const juce::Point<float> lamp { r.getX() + radius, r.getCentreY() };
    const float pulse = 0.5f + 0.5f * (float) std::sin (now * juce::MathConstants<double>::twoPi * 0.9);
    const float afterglow = state == ListenSession::State::done ? juce::jmax (0.0f, 1.0f - (float) (now - finishedAt) / 4.0f) : 0.0f;
    const float level = listening ? 0.45f + 0.55f * pulse : afterglow;
    LinenLamp::drawJewel (g, lamp, 9.0f, level, cobalt);

    if (listening)
    {
        // Progress: a cobalt arc filling around the lamp.
        juce::Path progress;
        const float ringR = 8.5f;
        progress.addCentredArc (lamp.x, lamp.y, ringR, ringR, 0.0f, 0.0f,
                                juce::MathConstants<float>::twoPi * juce::jmax (0.02f, session.getProgress()), true);
        g.setColour (cobalt);
        g.strokePath (progress, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    const juce::String text = listening ? "LISTENING"
                            : state == ListenSession::State::done && afterglow > 0.0f ? "TUNED"
                            : state == ListenSession::State::failed && now - finishedAt < 4.0 ? "TRY AGAIN"
                            : "AI LISTEN";
    g.setColour (listening ? cobaltDeep : espresso.withAlpha (hovering ? 1.0f : 0.8f));
    g.setFont (fonts::sansMedium (9.5f, 0.2f));
    g.drawText (text, r.withTrimmedLeft (radius * 2.0f - 2.0f).withTrimmedRight (radius * 0.6f), juce::Justification::centred, false);
}

} // namespace linen
