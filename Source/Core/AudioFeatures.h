#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

namespace wt
{
constexpr int featureDims = 48;
using FeatureVector = std::array<float, featureDims>;

/**
    A compact description of how a sound "sounds", computed from (at most) the first few seconds:
      20 mel-band spectral-shape means + 20 spreads (level-independent, over the active frames),
      spectral centroid, roll-off, zero-crossing rate, RMS, crest factor, log duration, attack time, decay.
    Similar sounds end up close together, which is what the grouping relies on.
*/
bool computeFeatures (juce::AudioFormatManager& formats, const juce::File& file, FeatureVector& out,
                      double maxSeconds = 4.0, juce::String* error = nullptr);
} // namespace wt
