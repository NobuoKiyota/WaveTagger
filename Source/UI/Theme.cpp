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

std::array<juce::PixelARGB, 256> makeSpectrogramLut()
{
    struct Stop
    {
        float pos;
        juce::Colour colour;
    };

    const Stop stops[] = {
        { 0.00f, juce::Colour (0xff000000) },
        { 0.30f, juce::Colour (0xff3a1405) },
        { 0.55f, juce::Colour (0xffa8480c) },
        { 0.75f, juce::Colour (0xfff08a1c) },
        { 0.90f, juce::Colour (0xffffd27a) },
        { 1.00f, juce::Colour (0xffffffff) },
    };

    std::array<juce::PixelARGB, 256> lut {};
    for (int i = 0; i < 256; ++i)
    {
        const auto t = (float) i / 255.0f;
        size_t s = 0;
        while (s + 2 < std::size (stops) && t > stops[s + 1].pos)
            ++s;

        const auto& a = stops[s];
        const auto& b = stops[s + 1];
        const auto local = juce::jlimit (0.0f, 1.0f, (t - a.pos) / (b.pos - a.pos));
        lut[(size_t) i] = a.colour.interpolatedWith (b.colour, local).getPixelARGB();
    }
    return lut;
}
} // namespace wt::theme
