#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace wt::theme
{
inline const juce::Colour background { 0xff1b1f24 };
inline const juce::Colour panel { 0xff23292f };
inline const juce::Colour panelLight { 0xff2d343b };
inline const juce::Colour outline { 0xff3a424b };
inline const juce::Colour text { 0xffc9ced4 };
inline const juce::Colour textDim { 0xff8a939c };
inline const juce::Colour accent { 0xff3d8fd6 };
inline const juce::Colour waveform { 0xff6fa8e6 };
inline const juce::Colour cursor { 0xffffc533 };
inline const juce::Colour playhead { 0xffffffff };

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();
};

/** RX-like black → brown → orange → yellow → white. */
std::array<juce::PixelARGB, 256> makeSpectrogramLut();
} // namespace wt::theme
