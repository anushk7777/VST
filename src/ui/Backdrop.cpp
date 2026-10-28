#include "Backdrop.h"
#include "Palette.h"

namespace linen
{

juce::Image Backdrop::render (int w, int h)
{
    juce::Image image (juce::Image::RGB, w, h, false, juce::SoftwareImageType());
    juce::Random rng (0xc0ba17);

    const auto top = palette::surface, bottom = palette::surfaceShade;
    const float diagonal = std::sqrt ((float) (w * w + h * h));

    juce::Image::BitmapData pixels (image, juce::Image::BitmapData::writeOnly);

    for (int y = 0; y < h; ++y)
    {
        const float fy = (float) y / (float) juce::jmax (1, h - 1);
        const float t = fy * fy * (3.0f - 2.0f * fy);
        const float r0 = top.getFloatRed()   + (bottom.getFloatRed()   - top.getFloatRed())   * t;
        const float g0 = top.getFloatGreen() + (bottom.getFloatGreen() - top.getFloatGreen()) * t;
        const float b0 = top.getFloatBlue()  + (bottom.getFloatBlue()  - top.getFloatBlue())  * t;

        for (int x = 0; x < w; ++x)
        {
            // A broad, soft gloss band running down from the top-left corner.
            const float along = ((float) x * 0.8f + (float) y) / diagonal;
            const float sheen = 0.018f * std::exp (-std::pow ((along - 0.28f) / 0.16f, 2.0f));
            const float grain = (rng.nextFloat() - 0.5f) * 0.004f;
            const float lift = sheen + grain;

            pixels.setPixelColour (x, y, juce::Colour::fromFloatRGBA (juce::jlimit (0.0f, 1.0f, r0 + lift),
                                                                      juce::jlimit (0.0f, 1.0f, g0 + lift),
                                                                      juce::jlimit (0.0f, 1.0f, b0 + lift * 0.6f),
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
        cache = render (w, h);

    g.drawImage (cache, area.toFloat(), juce::RectanglePlacement::stretchToFit);
}

} // namespace linen
