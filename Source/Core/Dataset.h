#pragma once

#include "AudioData.h"
#include "Conditioning.h"
#include "Exporter.h"

namespace wt
{
/**
    A growing training set: <folder>/audio/<name>.wav (24-bit, conditioned like an export) and <folder>/metadata.jsonl,
    one JSON object per clip. Adding the same recording again is safe: a clip that is already there keeps its audio and only
    has its metadata brought up to date; nothing is overwritten or deleted.
*/
struct DatasetClip
{
    juce::int64 start = 0, end = 0; // samples of the recording
    juce::String sourceFile;        // the recording's file name
    juce::int64 sourceSize = 0;     // and its size: together with the range they make the clip's id
    juce::String name;              // output name, e.g. Tools_Open03
    juce::String category, categoryJa, type, typeJa;
    int rating = 0;
    bool ng = false;
    juce::String attributes;
};

struct DatasetResult
{
    int added = 0;     // new clips: audio written, line added
    int updated = 0;   // clips already there whose metadata changed
    int ngMarked = 0;  // clips already there that are now NG (flagged in metadata, audio kept)
    int unchanged = 0;
    int skippedNg = 0; // NG clips that were never in the dataset: not added
    juce::StringArray errors;
};

/** 32 hex characters, the same for the same recording name + size + range on every PC. */
juce::String datasetClipId (const juce::String& sourceFile, juce::int64 sourceSize, juce::int64 start, juce::int64 end);

/** "Open (TOOLS), strength strong, material cardboard box, outdoor" / the Japanese one uses the catalogue's aliases where there are
    some and leaves the attribute words as they are (until there is a translation dictionary). */
juce::String datasetCaptionEn (const DatasetClip& clip);
juce::String datasetCaptionJa (const DatasetClip& clip);

/** Adds the clips of one recording. `conditioning` is applied to the audio that is written (the recording itself is never touched). */
DatasetResult addToDataset (const AudioData& audio, const std::vector<DatasetClip>& clips, const juce::File& folder,
                            const ConditionOptions& conditioning, const juce::String& appVersion);
} // namespace wt
