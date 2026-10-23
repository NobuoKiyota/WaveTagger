#pragma once

#include "InboxProcessor.h"
#include "Persistence.h"

namespace wt
{
struct QueueItem
{
    juce::String raw;        // the Raw copy that carries the markers (empty for a failure)
    juce::String original;   // where the recording was found
    juce::String status;     // "new" | "reviewed" | "failed"
    juce::String error;
    juce::String category, type;
    juce::int64 importedAt = 0;
    int cycles = 0;
};

/** What the inbox has brought in and what still needs a human look. Per PC (the paths are local), kept in %APPDATA%. */
class InboxQueue
{
public:
    static juce::File defaultFile() { return getAppDataDir().getChildFile ("inbox_queue.json"); }

    const std::vector<QueueItem>& getItems() const { return items; }
    int size() const { return (int) items.size(); }

    void add (const InboxOutcome& o)
    {
        QueueItem q;
        q.original = o.original.getFullPathName();
        q.raw = o.rawFile.getFullPathName();
        q.status = o.ok ? "new" : "failed";
        q.error = o.ok ? o.archiveNote : o.error;
        q.category = o.categoryHint;
        q.type = o.typeHint;
        q.cycles = o.cycles;
        q.importedAt = juce::Time::currentTimeMillis();
        items.insert (items.begin(), q); // newest first
    }

    void markReviewed (const juce::String& rawPath)
    {
        for (auto& q : items)
            if (q.raw.equalsIgnoreCase (rawPath) && q.status == "new")
                q.status = "reviewed";
    }

    void setStatus (int index, const juce::String& status)
    {
        if (index >= 0 && index < size())
            items[(size_t) index].status = status;
    }

    void remove (int index)
    {
        if (index >= 0 && index < size())
            items.erase (items.begin() + index);
    }

    int countNew() const
    {
        int n = 0;
        for (const auto& q : items)
            n += q.status == "new" ? 1 : 0;
        return n;
    }

    bool save (const juce::File& file = defaultFile()) const
    {
        juce::Array<juce::var> arr;
        for (const auto& q : items)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("raw", q.raw);
            o->setProperty ("original", q.original);
            o->setProperty ("status", q.status);
            o->setProperty ("error", q.error);
            o->setProperty ("category", q.category);
            o->setProperty ("type", q.type);
            o->setProperty ("importedAt", q.importedAt);
            o->setProperty ("cycles", q.cycles);
            arr.add (juce::var (o));
        }
        return writeJson (file, arr);
    }

    void load (const juce::File& file = defaultFile())
    {
        items.clear();
        const auto json = readJson (file); // keep the parsed value alive while its array is used
        if (const auto* arr = json.getArray())
            for (const auto& v : *arr)
            {
                QueueItem q;
                q.raw = v.getProperty ("raw", "").toString();
                q.original = v.getProperty ("original", "").toString();
                q.status = v.getProperty ("status", "new").toString();
                q.error = v.getProperty ("error", "").toString();
                q.category = v.getProperty ("category", "").toString();
                q.type = v.getProperty ("type", "").toString();
                q.importedAt = (juce::int64) v.getProperty ("importedAt", 0);
                q.cycles = (int) v.getProperty ("cycles", 0);
                items.push_back (q);
            }
    }

private:
    std::vector<QueueItem> items;
};
} // namespace wt
