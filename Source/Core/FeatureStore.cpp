#include "FeatureStore.h"
#include "Persistence.h"

namespace wt
{
namespace
{
constexpr int magic = 0x45465457; // "WTFE"
constexpr int version = 1;
} // namespace

juce::File FeatureStore::defaultFile()
{
    return getAppDataDir().getChildFile ("features.bin");
}

void FeatureStore::load()
{
    const juce::ScopedLock sl (lock);
    records.clear();
    pending.reset();
    needsHeader = true;

    juce::FileInputStream in (storeFile);
    if (! in.openedOk() || in.getTotalLength() < 12)
        return;

    if (in.readInt() != magic || in.readInt() != version || in.readInt() != featureDims)
        return; // another layout: start fresh (the file is rewritten on the next flush)

    needsHeader = false;
    while (! in.isExhausted())
    {
        const auto path = in.readString();
        FeatureRecord r;
        r.size = in.readInt64();
        r.mtime = in.readInt64();
        for (auto& f : r.v)
            f = in.readFloat();
        if (path.isEmpty())
            break;
        records[path] = r; // a later record for the same file replaces the earlier one
    }
}

void FeatureStore::flush()
{
    const juce::ScopedLock sl (lock);
    if (pending.getDataSize() == 0)
        return;

    if (needsHeader)
    {
        storeFile.deleteFile();
        juce::FileOutputStream header (storeFile);
        if (! header.openedOk())
            return;
        header.writeInt (magic);
        header.writeInt (version);
        header.writeInt (featureDims);
        needsHeader = false;
    }

    juce::FileOutputStream out (storeFile);
    if (out.openedOk())
    {
        out.setPosition (storeFile.getSize());
        out.write (pending.getData(), pending.getDataSize());
        pending.reset();
    }
}

bool FeatureStore::isCurrent (const juce::File& f) const
{
    const juce::ScopedLock sl (lock);
    const auto it = records.find (keyOf (f));
    return it != records.end() && it->second.size == f.getSize() && it->second.mtime == f.getLastModificationTime().toMilliseconds();
}

const FeatureRecord* FeatureStore::find (const juce::File& f) const
{
    const juce::ScopedLock sl (lock);
    const auto it = records.find (keyOf (f));
    return it == records.end() ? nullptr : &it->second; // records are never erased while a window is using them
}

void FeatureStore::put (const juce::File& f, const FeatureVector& v)
{
    const juce::ScopedLock sl (lock);
    FeatureRecord r;
    r.size = f.getSize();
    r.mtime = f.getLastModificationTime().toMilliseconds();
    r.v = v;
    records[keyOf (f)] = r;

    pending.writeString (keyOf (f));
    pending.writeInt64 (r.size);
    pending.writeInt64 (r.mtime);
    for (auto x : r.v)
        pending.writeFloat (x);
}

size_t FeatureStore::size() const
{
    const juce::ScopedLock sl (lock);
    return records.size();
}

//==============================================================================
FeatureAnalyzer::FeatureAnalyzer (juce::AudioFormatManager& f, FeatureStore& s)
    : juce::Thread ("FeatureAnalyzer"), formats (f), store (s)
{
}

FeatureAnalyzer::~FeatureAnalyzer()
{
    *alive = false;
    signalThreadShouldExit();
    stopThread (30000);
}

void FeatureAnalyzer::start (std::vector<juce::File> files)
{
    if (running)
        return;

    queue = std::move (files);
    done = 0;
    failed = 0;
    total = (int) queue.size();
    running = true;
    startThread (juce::Thread::Priority::background);
}

void FeatureAnalyzer::cancel()
{
    signalThreadShouldExit();
}

void FeatureAnalyzer::run()
{
    int sinceFlush = 0;
    for (const auto& f : queue)
    {
        if (threadShouldExit())
            break;

        if (! store.isCurrent (f))
        {
            FeatureVector v {};
            if (computeFeatures (formats, f, v))
                store.put (f, v);
            else
                ++failed;

            if (++sinceFlush >= 100)
            {
                store.flush();
                sinceFlush = 0;
            }
        }
        ++done;
    }

    store.flush();
    running = false;
    juce::MessageManager::callAsync ([this, stillAlive = alive] {
        if (*stillAlive && onFinished)
            onFinished();
    });
}
} // namespace wt
