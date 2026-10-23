#include "Theme.h"

namespace wt::theme
{
LookAndFeel::LookAndFeel()
{
    setColourScheme ({ background, panel, panel, outline, text, accent, juce::Colours::white, accent, text });
    setDefaultSansSerifTypefaceName ("Meiryo UI");

    setColour (juce::TextButton::buttonColourId, panelLight);
    setColour (juce::TextButton::buttonOnColourId, accent);
    setColour (juce::Slider::trackColourId, accent);
    setColour (juce::Slider::backgroundColourId, outline);
    setColour (juce::Slider::thumbColourId, text);
    setColour (juce::Label::textColourId, text);
}

juce::StringArray getSpectrogramSchemeNames()
{
    return { "RX Orange", "Grayscale", "Ice", "Inferno", "Phosphor" };
}

std::array<juce::PixelARGB, 256> makeSpectrogramLut (int scheme)
{
    struct Stop
    {
        float pos;
        juce::uint32 argb;
    };

    static const std::vector<std::vector<Stop>> schemes = {
        { { 0.00f, 0xff000000 }, { 0.30f, 0xff3a1405 }, { 0.55f, 0xffa8480c }, { 0.75f, 0xfff08a1c }, { 0.90f, 0xffffd27a }, { 1.00f, 0xffffffff } },
        { { 0.00f, 0xff000000 }, { 1.00f, 0xffffffff } },
        { { 0.00f, 0xff000000 }, { 0.30f, 0xff08122e }, { 0.55f, 0xff1b4f9c }, { 0.75f, 0xff38b4e8 }, { 0.90f, 0xffbdf0ff }, { 1.00f, 0xffffffff } },
        { { 0.00f, 0xff000004 }, { 0.25f, 0xff420a68 }, { 0.50f, 0xff932667 }, { 0.70f, 0xffdd513a }, { 0.88f, 0xfffca50a }, { 1.00f, 0xfffcffa4 } },
        { { 0.00f, 0xff000000 }, { 0.30f, 0xff021a0a }, { 0.55f, 0xff0a6a24 }, { 0.78f, 0xff3cd63c }, { 0.92f, 0xffd4ffb0 }, { 1.00f, 0xffffffff } },
    };

    const auto& stops = schemes[(size_t) juce::jlimit (0, (int) schemes.size() - 1, scheme)];

    std::array<juce::PixelARGB, 256> lut {};
    for (int i = 0; i < 256; ++i)
    {
        const auto t = (float) i / 255.0f;
        size_t s = 0;
        while (s + 2 < stops.size() && t > stops[s + 1].pos)
            ++s;

        const auto& a = stops[s];
        const auto& b = stops[s + 1];
        const auto local = juce::jlimit (0.0f, 1.0f, (t - a.pos) / (b.pos - a.pos));
        lut[(size_t) i] = juce::Colour (a.argb).interpolatedWith (juce::Colour (b.argb), local).getPixelARGB();
    }
    return lut;
}
} // namespace wt::theme
