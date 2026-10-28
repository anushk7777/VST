#include "Backdrop.h"
#include "Palette.h"

namespace linen
{

namespace
{
    /** Per-thread brightness offsets: mostly tiny, with occasional thicker
        "slubs" like real linen yarn. Smoothed so neighbouring threads blend. */
    std::vector<float> threadOffsets (int count, juce::Random& rng)
    {
        std::vector<float> v ((size_t) count);
        for (auto& x : v)
        {
            x = (rng.nextFloat() - 0.5f) * 0.018f;
            if (rng.nextFloat() < 0.04f)
                x -= 0.012f + rng.nextFloat() * 0.01f;
        }

        std::vector<float> smoothed (v.size());
        for (size_t i = 0; i < v.size(); ++i)
        {
            const float prev = v[i > 0 ? i - 1 : i];
            const float next = v[i + 1 < v.size() ? i + 1 : i];
            smoothed[i] = 0.25f * prev + 0.5f * v[i] + 0.25f * next;
        }
        return smoothed;
    }
}

juce::Image Backdrop::render (int w, int h, float pixelScale)
{
    juce::Image image (juce::Image::RGB, w, h, false, juce::SoftwareImageType());
    juce::Random rng (0x11e9);

    // One thread is about 1.6 logical px wide.
    const float threadPx = juce::jmax (1.0f, 1.6f * pixelScale);
    const int numCols = (int) std::ceil ((float) w / threadPx) + 1;
    const int numRows = (int) std::ceil ((float) h / threadPx) + 1;
    const auto warp = threadOffsets (numCols, rng);
    const auto weft = threadOffsets (numRows, rng);

    const auto base = palette::surface;
    const float br = base.getFloatRed(), bg = base.getFloatGreen(), bb = base.getFloatBlue();

    // Side edges fall off very slightly, like light across fabric.
    std::vector<float> edge ((size_t) w);
    for (int x = 0; x < w; ++x)
        edge[(size_t) x] = std::pow (std::abs ((float) x / (float) juce::jmax (1, w - 1) - 0.5f) * 2.0f, 3.0f) * 0.018f;

    juce::Image::BitmapData pixels (image, juce::Image::BitmapData::writeOnly);

    for (int y = 0; y < h; ++y)
    {
        const float fy = (float) y / (float) juce::jmax (1, h - 1);
        // Top-lit: a touch brighter at the top, gently darker toward the bottom edge.
        const float vignetteY = 0.012f * (1.0f - fy) - 0.03f * fy * fy;
        const int row = (int) ((float) y / threadPx);

        for (int x = 0; x < w; ++x)
        {
            const int col = (int) ((float) x / threadPx);

            // Over/under: which yarn is on top alternates in a plain weave.
            const bool warpOnTop = ((row + col) & 1) == 0;
            const float thread = warpOnTop ? warp[(size_t) col] * 1.0f + weft[(size_t) row] * 0.45f
                                           : weft[(size_t) row] * 1.0f + warp[(size_t) col] * 0.45f;

            const float grain = (rng.nextFloat() - 0.5f) * 0.012f;
            const float shade = 1.0f + thread + grain + vignetteY - edge[(size_t) x];

            pixels.setPixelColour (x, y, juce::Colour::fromFloatRGBA (juce::jlimit (0.0f, 1.0f, br * shade),
                                                                      juce::jlimit (0.0f, 1.0f, bg * shade),
                                                                      juce::jlimit (0.0f, 1.0f, bb * shade * 0.998f),
                                                                      1.0f));
        }
    }

    return image;
}

void Backdrop::draw (juce::Graphics& g, juce::Rectangle<int> area)
{
    const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    const int w = juce::roundToInt ((float) area.getWidth() * scale);
    const int h = juce::roundToInt ((float) area.getHeight() * scale);

    if (w <= 0 || h <= 0)
        return;

    if (! cache.isValid() || cache.getWidth() != w || cache.getHeight() != h)
        cache = render (w, h, scale);

    g.drawImage (cache, area.toFloat(), juce::RectanglePlacement::stretchToFit);
}

void Backdrop::drawScorch (juce::Graphics& g, juce::Rectangle<float> area, float amount)
{
    if (amount <= 0.001f)
        return;

    // A radial burn from the edges inward: up to ~6% espresso/rust at full Pain.
    const auto centre = area.getCentre();
    juce::ColourGradient burn (juce::Colours::transparentBlack, centre.x, centre.y,
                               palette::crimsonDeep.interpolatedWith (palette::espresso, 0.5f).withAlpha (0.09f * amount),
                               area.getX(), area.getY(), true);
    burn.addColour (0.55, juce::Colours::transparentBlack);
    g.setGradientFill (burn);
    g.fillRect (area);
}

} // namespace linen
