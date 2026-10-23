#pragma once

#include "AudioData.h"
#include "Catalog.h"
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
    juce::int64 sourceFrames = 0;   // and its length in samples: together with the range they make the clip's id
                                    // (not the file size: that changes whenever markers are written into the WAV)
    juce::int64 sourceSize = 0;     // the file size; only used to find lines made when ids still contained it
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

/** 32 hex characters, the same for the same recording name + length in samples + range on every PC. */
juce::String datasetClipId (const juce::String& sourceFile, juce::int64 sourceFrames, juce::int64 start, juce::int64 end);

/** The id that was used while it still contained the file size (kept to recognise older lines). */
juce::String datasetLegacyClipId (const juce::String& sourceFile, juce::int64 sourceSize, juce::int64 start, juce::int64 end);

/** "Open (TOOLS), strength strong, material cardboard box, outdoor". The Japanese one uses the catalogue's aliases for category and
    type; the attribute words of both are put into the language of the caption through `dictionary`. A word it has no translation for
    stays as it was typed (and is reported as untranslated). Without a dictionary nothing is translated. */
juce::String datasetCaptionEn (const DatasetClip& clip, const Dictionary* dictionary = nullptr);
juce::String datasetCaptionJa (const DatasetClip& clip, const Dictionary* dictionary = nullptr);

/** Gives the recording when something has to be cut from it. Called at most once, and not at all if every clip is already in the dataset. */
using DatasetAudioProvider = std::function<AudioDataPtr()>;

/** Adds the clips of one recording. `conditioning` is applied to the audio that is written (the recording itself is never touched). */
DatasetResult addToDataset (const DatasetAudioProvider& audio, const std::vector<DatasetClip>& clips, const juce::File& folder,
                            const ConditionOptions& conditioning, const juce::String& appVersion, const Dictionary* dictionary = nullptr);
DatasetResult addToDataset (const AudioData& audio, const std::vector<DatasetClip>& clips, const juce::File& folder,
                            const ConditionOptions& conditioning, const juce::String& appVersion, const Dictionary* dictionary = nullptr);

//==============================================================================
/** The catalogue's Japanese names, copied out so a worker thread can use them while the catalogue reloads. */
struct JapaneseNames
{
    std::map<juce::String, juce::String> category; // lower-case name -> Japanese
    std::map<juce::String, juce::String> type;     // "category|type" (lower case; category "*" = common) -> Japanese

    static JapaneseNames from (const Catalog& catalog);
    juce::String forCategory (const juce::String& categoryName) const;
    juce::String forType (const juce::String& categoryName, const juce::String& typeName) const;
};

enum class WavTagResult
{
    ok,
    noTags,     // no wtag chunk: this WAV was never tagged by WaveTagger
    unreadable
};

struct WavClips
{
    std::vector<DatasetClip> clips;
    int untyped = 0;     // cycles without a type
    int unconfirmed = 0; // typed by the Inbox's suggestions and never looked at: not taken (they are a guess, not a judgement)
    juce::int64 frames = 0;
};

/** The typed, confirmed cycles of a WAV's wtag chunk as clips. The audio is not read, only the header. */
WavTagResult datasetClipsFromWav (const juce::File& wav, juce::AudioFormatManager& formats, const JapaneseNames& names, WavClips& out);

struct DatasetBatchResult
{
    int wavFiles = 0;      // WAVs found under the folder
    int withClips = 0;     // of which had clips to look at
    int audioRead = 0;     // of which had to be read (something new to cut)
    int noTags = 0;
    int unreadable = 0;
    int untyped = 0, unconfirmed = 0; // cycles left out
    bool cancelled = false;
    DatasetResult total;   // summed over all files
};

/** Adds the tagged WAVs under `rawDir` (sub folders included) to the dataset. Recordings whose clips are all there already are not
    read at all; only their metadata is brought up to date. `progress(index, total, name)` is called before each file; returning false
    stops (after the files done so far). Nothing in `rawDir` is changed. */
DatasetBatchResult addRawFolderToDataset (const juce::File& rawDir, const juce::File& folder, juce::AudioFormatManager& formats, const JapaneseNames& names,
                                          const ConditionOptions& conditioning, const juce::String& appVersion, const Dictionary* dictionary,
                                          const std::function<bool (int index, int total, const juce::String& name)>& progress = {});
} // namespace wt
