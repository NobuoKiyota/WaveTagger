#pragma once

#include "AudioData.h"

namespace wt
{
struct ExportItem
{
    juce::int64 start = 0, end = 0;
    juce::String subFolder; // relative to the output root
    juce::String fileName;  // without extension
    juce::String comment;   // stored in the WAV INFO chunk
};

struct ExportResult
{
    int written = 0;
    juce::StringArray skippedExisting, errors;
};

/** Writes each region as a 24-bit WAV (source rate / channels) with a short fade at both edges. Never overwrites. */
ExportResult exportCycles (const AudioData& audio, const std::vector<ExportItem>& items, const juce::File& root,
                           int bitsPerSample = 24, float fadeMs = 2.0f);
} // namespace wt
