#pragma once

#include "AudioFeatures.h"

namespace wt
{
/**
    One labelled sound: its features (so nothing else is needed to learn from it) and what a person decided it is.
    The suggestion that was on offer is kept next to the final answer: accepted / corrected is the interesting part.
*/
struct LabelSample
{
    juce::String key;      // "<file name>|<size>|<start>|<end>": independent of drive letters, stable across PCs
    juce::String fileName;
    juce::int64 start = 0, end = 0; // samples; 0,0 = the whole file
    FeatureVector v {};

    juce::String category, type;
    juce::String source;   // "editor" | "export" | "inbox" | "organizer"
    bool human = true;     // false: an automatic label nobody has confirmed (never used for learning)

    juce::String suggestedCategory, suggestedType;
    float suggestedConfidence = -1.0f;

    int rating = 0;
    bool ng = false;
    juce::String attributes;
    juce::int64 updatedAt = 0;

    bool hasLabel() const { return type.isNotEmpty() || category.isNotEmpty(); }
};

/**
    The learning data. Append-only JSON-lines, one file per PC (labels_<pc>.jsonl) inside a folder that Google Drive
    mirrors, so two PCs never write the same file; loading merges every labels_*.jsonl and the newest entry per key wins.
*/
class LabelStore
{
public:
    LabelStore() = default;
    LabelStore (juce::File directory, juce::String pcName) { setLocation (std::move (directory), std::move (pcName)); }

    void setLocation (juce::File directory, juce::String pcName);
    const juce::File& getDirectory() const { return dir; }

    static juce::String makeKey (const juce::String& fileName, juce::int64 fileSize, juce::int64 start, juce::int64 end);

    void load();
    /** Adds or replaces samples (by key) and appends them to this PC's file. */
    void upsert (std::vector<LabelSample> samples);
    void upsert (const LabelSample& s) { upsert (std::vector<LabelSample> { s }); }

    const LabelSample* find (const juce::String& key) const;
    const std::map<juce::String, LabelSample>& all() const { return samples; }
    size_t size() const { return samples.size(); }
    size_t humanLabelCount() const;
    /** Incremented on every change, so that dependants (the suggestion engine) know when to rebuild. */
    int getRevision() const { return revision; }

private:
    juce::File myFile() const { return dir.getChildFile ("labels_" + pc + ".jsonl"); }

    juce::File dir;
    juce::String pc { "pc" };
    std::map<juce::String, LabelSample> samples;
    int revision = 0;
};
} // namespace wt
