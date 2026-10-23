#pragma once

#include "FeatureStore.h"
#include "LibraryIndex.h"

namespace wt
{
//==============================================================================
// Spelling variants
//==============================================================================
enum class NameSensitivity { Strict, Normal, Loose };

struct NameVariant
{
    juce::String display; // the spelling as it appears in file names
    juce::String key;
    int count = 0;        // how many files use it
    juce::File example;   // one such file
};

struct NameGroup
{
    std::vector<NameVariant> variants; // most used first: that one is the likely correct spelling
};

/**
    Names that are almost, but not exactly, the same ("ImpactSoun" / "ImpactSound", "Door_Opne" / "Door_Open").
    Case and separators at the end are already folded together by the index, so what remains are real spelling differences.
    Strict: 1 character, names of 5+ letters. Normal: 1 character (2 for names of 9+ letters). Loose: up to ~30 % of the length.
    `ignored` holds "keyA|keyB" pairs the user has said are genuinely different names.
*/
std::vector<NameGroup> findSimilarNames (const IndexData& index, NameSensitivity sensitivity, const juce::StringArray& ignored = {});

/** The key used in the ignore list for a pair of variants (order-independent). */
juce::String ignoreKey (const juce::String& keyA, const juce::String& keyB);

//==============================================================================
// Exact duplicates
//==============================================================================
struct HashRecord
{
    juce::int64 size = 0, mtime = 0;
    juce::uint64 quick = 0, full = 0; // full == 0: not computed
};

/** Content hashes kept on this PC (derived data), append-only so that long runs can be stopped and resumed. */
class HashStore
{
public:
    explicit HashStore (juce::File file) : storeFile (std::move (file)) {}
    static juce::File defaultFile();

    void load();
    void flush();
    const HashRecord* find (const juce::String& path, juce::int64 size, juce::int64 mtime) const;
    void put (const juce::String& path, const HashRecord& r);
    size_t size() const { return records.size(); }
    int hashesComputed() const { return computed; }
    void countComputed() { ++computed; }

private:
    juce::File storeFile;
    std::map<juce::String, HashRecord> records;
    juce::MemoryOutputStream pending;
    bool needsHeader = true;
    int computed = 0;
};

struct DuplicateGroup
{
    std::vector<juce::File> files;
    juce::int64 size = 0;
    bool exact = true;
};

/**
    Byte-identical files across the whole index. Narrowed in three steps so that little is read:
    same size -> same first/last 64 KB -> same full content. Cancel by returning false from `progress`.
*/
std::vector<DuplicateGroup> findExactDuplicates (const IndexData& index, HashStore& hashes, const std::function<bool (int done, int total)>& progress = {});

/** The same sound stored differently (format, bit depth, a little silence): close in length and in every spectral feature. */
std::vector<DuplicateGroup> findNearDuplicates (const std::vector<juce::File>& files, const FeatureStore& features);
} // namespace wt
