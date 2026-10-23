#pragma once

#include <juce_events/juce_events.h>
#include <unordered_map>

namespace wt
{
struct IndexEntry
{
    juce::String path;
    juce::String name;  // file name without extension
    juce::String lower; // lower-case path for searching
    juce::int64 size = 0, mtime = 0;
    int root = 0;
};

struct BaseInfo
{
    juce::String display;
    int count = 0;
    int maxNumber = 0;
};

struct ParsedName
{
    juce::String display, key; // key = lower-case base with trailing separators trimmed
    int number = -1;           // trailing serial number, -1 if none
};

/** "MasicTape_03" -> base "MasicTape", number 3. */
ParsedName parseName (const juce::String& nameWithoutExtension);
/** Same normalisation applied to a stem such as "pen_push". */
juce::String makeKey (const juce::String& stem);

/** Immutable snapshot of the whole library listing (read-only metadata, no audio is touched). */
struct IndexData
{
    std::vector<IndexEntry> entries;
    std::unordered_map<juce::String, BaseInfo> bases;
    juce::StringArray roots;
    int generation = 0;

    const BaseInfo* find (const juce::String& key) const;
    /** Existing names within a small edit distance of key, e.g. "MasicTape" for "MaskingTape". */
    juce::StringArray findSimilar (const juce::String& key, const juce::StringArray& excludeKeys, int maxResults = 3) const;
    std::vector<int> search (const juce::String& query, int maxResults) const;
};

using IndexDataPtr = std::shared_ptr<const IndexData>;

/**
    Background scanner + cache. Roots are only ever enumerated (never written, moved or renamed), so
    legacy folders that Cubase projects point at stay untouched.
*/
class LibraryIndex : public juce::ChangeBroadcaster,
                     private juce::Thread
{
public:
    LibraryIndex();
    ~LibraryIndex() override;

    /** Loads the cache if it matches the roots and is recent; otherwise schedules a full scan. */
    void setRoots (const juce::StringArray& roots);
    const juce::StringArray& getRoots() const { return roots; }

    void rescan();
    /** Re-enumerates one folder (after an export) without touching the rest. */
    void refreshFolder (const juce::File& folder);

    IndexDataPtr getData() const;
    bool isScanning() const { return scanning; }
    int getScannedCount() const { return scanned; }

private:
    void run() override;
    void scanAll();
    void scanFolder (const juce::File& folder);
    void publish (std::vector<IndexEntry> entries, bool saveCache);
    bool loadCache();

    struct Task
    {
        bool full = true;
        juce::File folder;
    };

    juce::StringArray roots;
    mutable juce::CriticalSection lock;
    IndexDataPtr data;
    std::vector<Task> queue;
    juce::WaitableEvent wake;
    std::atomic<bool> scanning { false };
    std::atomic<int> scanned { 0 };
    int generation = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LibraryIndex)
};
} // namespace wt
