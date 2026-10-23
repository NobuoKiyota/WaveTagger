#pragma once

#include "AudioData.h"

namespace wt
{
struct AutoSplitParams
{
    float aboveNoiseDb = 12.0f; // threshold above the estimated noise floor
    float minGapMs = 120.0f;    // gaps shorter than this are bridged
    float minLengthMs = 25.0f;  // shorter sounds are dropped
    float padMs = 40.0f;        // margin added before/after each sound
};

/** Finds sound events in [from, to) using a short-window RMS envelope relative to the noise floor. */
std::vector<std::pair<juce::int64, juce::int64>> detectSegments (const AudioData& audio, juce::int64 from, juce::int64 to,
                                                                 const AutoSplitParams& params);
} // namespace wt
