#pragma once

#include "AudioFeatures.h"
#include <juce_events/juce_events.h>

namespace wt
{
struct FeatureRecord
{
    juce::int64 size = 0, mtime = 0;
    FeatureVector v {};
};

/**
    Features per file, kept on this PC (derived data: it can always be recomputed). Append-only on disk, so an
    analysis of 90,000 files can be stopped and resumed at any point; a file is re-analysed only if its size or
    modification time changed.
*/
class FeatureStore
{
public:
    explicit FeatureStore (juce::File file) : storeFile (std::move (file)) {}

    static juce::File defaultFile();

    void load();
    void flush();

    bool isCurrent (const juce::File& f) const;
    const FeatureRecord* find (const juce::File& f) const;
    void put (const juce::File& f, const FeatureVector& v);
    size_t size() const;

private:
    static juce::String keyOf (const juce::File& f) { return f.getFullPathName().toLowerCase(); }

    juce::File storeFile;
    mutable juce::CriticalSection lock;
    std::map<juce::String, FeatureRecord> records;
    juce::MemoryOutputStream pending;
    bool needsHeader = true;
};

/** Computes features for a list of files on a background thread (resumable: current files are skipped). */
class FeatureAnalyzer : private juce::Thread
{
public:
    FeatureAnalyzer (juce::AudioFormatManager& formats, FeatureStore& store);
    ~FeatureAnalyzer() override;

    void start (std::vector<juce::File> files);
    void cancel();

    bool isRunning() const { return running; }
    int getDone() const { return done; }
    int getTotal() const { return total; }
    int getFailed() const { return failed; }

    /** Message thread, when a run ends (finished or cancelled). */
    std::function<void()> onFinished;

private:
    void run() override;

    juce::AudioFormatManager& formats;
    FeatureStore& store;
    std::vector<juce::File> queue;
    std::atomic<bool> running { false };
    std::atomic<int> done { 0 }, total { 0 }, failed { 0 };
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
};
} // namespace wt
