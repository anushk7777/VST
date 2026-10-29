#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <melatonin_blur/melatonin_blur.h>

#include "Palette.h"
#include "dsp/ResonanceSuppressor.h"

namespace linen
{

/** The cream display well: the vocal's spectrum as a soft line, and De-harsh's
    gain reduction as a "silk veil" hanging from the top. Two handles set the
    focus range. The EQ shape (Tone shelves and console EQ) is drawn as a thin
    cobalt line around the middle, like an EQ plugin's curve. Animated on the
    display's refresh (VBlank), never on a timer. */
class SpectrumDisplay final : public juce::Component,
                              public juce::SettableTooltipClient
{
public:
    SpectrumDisplay (DeharshDisplay& source, juce::RangedAudioParameter& focusLow, juce::RangedAudioParameter& focusHigh);

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    /** Live context, polled every frame: Pain amount and stress (0..1 each),
        and Listen progress (0..1, or negative when not listening). */
    struct Context
    {
        float pain = 0.0f, stress = 0.0f, listening = -1.0f;
    };
    void setContextSource (std::function<Context()> newSource) { contextSource = std::move (newSource); }

    /** Shows a short message (e.g. what Listen did) across the display for a while. */
    void showMessage (const juce::String& text, bool good);

    /** The EQ response to draw, in dB at a frequency. It is read on the
        message thread, after markEqChanged() (which any thread may call). */
    void setEqResponse (std::function<float (float hz)> response)
    {
        eqResponse = std::move (response);
        markEqChanged();
    }

    void markEqChanged() noexcept { eqDirty.store (true); }

private:
    enum class Handle { none, low, high };

    void tick (double timestampSeconds);
    float xForHz (float hz) const;
    float hzForX (float x) const;
    float yForLevel (float db) const;
    float yForCut (float db) const;
    float yForEq (float db) const;
    void updateEqCurve();
    Handle handleAt (juce::Point<float> p) const;

    DeharshDisplay& source;
    juce::RangedAudioParameter& focusLowParam;
    juce::RangedAudioParameter& focusHighParam;
    juce::ParameterAttachment focusLowAttachment, focusHighAttachment;
    float focusLowHz = 120.0f, focusHighHz = 18000.0f;

    std::array<float, DeharshDisplay::numPoints> level {}, cut {};

    static constexpr int numEqPoints = 96;
    std::function<float (float)> eqResponse;
    std::atomic<bool> eqDirty { true };
    std::array<float, numEqPoints> eqDb {};
    bool eqVisible = false;
    std::function<Context()> contextSource;
    Context context;
    float backlight = 0.0f, listenGlow = 0.0f;
    juce::Random tremor;
    juce::String message;
    bool messageGood = true;
    double messageSince = -100.0, nowSeconds = 0.0;
    float pitchHz = 0.0f, pitchAlpha = 0.0f;
    double lastTick = 0.0;

    juce::Rectangle<float> plot;
    Handle hovered = Handle::none, dragging = Handle::none;

    melatonin::InnerShadow wellShadow {
        { linen::palette::espresso.withAlpha (0.16f), 12, { 0, 4 } },
        { juce::Colours::white.withAlpha (0.55f), 2, { 0, -1 } }
    };

    juce::VBlankAttachment vblank;
};

} // namespace linen
