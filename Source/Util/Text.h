#pragma once

#include <juce_core/juce_core.h>

// UTF-8 literal → juce::String (JUCE asserts on non-ASCII plain char* literals)
#define WT_U8(literal) juce::String::fromUTF8 (reinterpret_cast<const char*> (u8##literal))

namespace wt
{
/** "00:00:49.480" style, as shown in RX. */
inline juce::String formatTime (double seconds)
{
    if (seconds < 0.0)
        seconds = 0.0;

    const auto totalMs = (juce::int64) std::llround (seconds * 1000.0);
    const auto ms = (int) (totalMs % 1000);
    const auto s = (int) ((totalMs / 1000) % 60);
    const auto m = (int) ((totalMs / 60000) % 60);
    const auto h = (int) (totalMs / 3600000);
    return juce::String::formatted ("%02d:%02d:%02d.%03d", h, m, s, ms);
}
} // namespace wt
