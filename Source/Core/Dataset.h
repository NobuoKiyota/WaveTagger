#pragma once

#include "AudioData.h"
#include "Conditioning.h"
#include "Dictionary.h"
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
    juce::StringArray untranslated; // attribute words (of any clip looked at) the dictionary has no translation for
};

/** 32 hex characters, the same for the same recording name + size + range on every PC. */
juce::String datasetClipId (const juce::String& sourceFile, juce::int64 sourceSize, juce::int64 start, juce::int64 end);

/** "Open (TOOLS), strength strong, material cardboard box, outdoor". The Japanese one uses the catalogue's aliases for category and
    type; the attribute words of both are put into the language of the caption through `dictionary`. A word it has no translation for
    stays as it was typed (and is reported as untranslated). Without a dictionary nothing is translated. */
juce::String datasetCaptionEn (const DatasetClip& clip, const Dictionary* dictionary = nullptr);
juce::String datasetCaptionJa (const DatasetClip& clip, const Dictionary* dictionary = nullptr);

/** Adds the clips of one recording. `conditioning` is applied to the audio that is written (the recording itself is never touched). */
DatasetResult addToDataset (const AudioData& audio, const std::vector<DatasetClip>& clips, const juce::File& folder,
                            const ConditionOptions& conditioning, const juce::String& appVersion, const Dictionary* dictionary = nullptr);
} // namespace wt
