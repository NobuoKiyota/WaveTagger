#pragma once

#include "AudioData.h"
#include "Conditioning.h"
#include <functional>
#include <map>
#include <set>

namespace wt
{
struct ExportItem
{
    juce::int64 start = 0, end = 0;
    juce::String subFolder; // relative to the output root
    juce::String fileName;  // without extension
    juce::String comment;   // stored in the WAV INFO chunk
    int type = -1, number = 0; // what the name was made from, so that it can be renumbered against what the destination holds
};

/** The folder under the output root: <Category>/<Prefix>[/<Type>]. A Prefix that is just the Category again (any case) adds no level. */
juce::String exportSubFolder (const juce::String& category, const juce::String& prefix, const juce::String& typeName, bool typeFolders);

/** Keeps every file name free in its folder: where the number is already taken (on disk, or by an earlier item of this export),
    the next free number is used instead. Nothing is ever overwritten. Returns how many items got a different number. */
int renumberForDestination (std::vector<ExportItem>& items, const juce::File& root,
                            const std::function<juce::String (int type, int number)>& nameFor);

struct ExportResult
{
    int written = 0;
    juce::StringArray skippedExisting, errors;

    /** Every file that was written, with the conditioning that went into it (describeConditioning; empty if none). */
    std::vector<std::pair<juce::File, juce::String>> writtenFiles;

    // Normalisation: the gains that were applied and how many were stopped by the ceiling / max gain
    double minGainDb = 0.0, maxGainDb = 0.0;
    int gainLimited = 0;

    // Hum removal: the mains frequency that was found (0 = none) and how many harmonics were notched
    double humHz = 0.0;
    int humNotches = 0;

    // Left/right balance: whether it was corrected, and the measured left-minus-right difference in dB
    bool panApplied = false;
    double panDb = 0.0;
};

/** Writes each region as a 24-bit WAV (source rate / channels) with a short fade at both edges. Never overwrites.
    `conditioning` is applied to the copy that is written; the source audio is never touched. */
ExportResult exportCycles (const AudioData& audio, const std::vector<ExportItem>& items, const juce::File& root,
                           int bitsPerSample = 24, float fadeMs = 2.0f, const ConditionOptions& conditioning = {});
} // namespace wt
