#pragma once

#include <juce_core/juce_core.h>

namespace wt
{
struct RawMarker
{
    juce::int64 start = 0;
    juce::int64 length = 0; // 0 = point marker
    juce::String label;
};

/** Reads cue / adtl(labl, ltxt) markers as written by RX, Sound Forge, Audacity, Cubase etc. Read-only. */
std::vector<RawMarker> readWavMarkers (const juce::File& file);
} // namespace wt
