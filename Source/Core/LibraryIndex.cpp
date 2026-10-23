#include "LibraryIndex.h"
#include "Persistence.h"

namespace wt
{
namespace
{
constexpr int cacheMagic = 0x58495457; // "WTIX"
constexpr int cacheVersion = 1;
const char* const audioPatterns = "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg;*.m4a";

bool isSeparator (juce::juce_wchar c) { return c == '_' || c == '-' || c == ' ' || c == '.'; }

juce::File getCacheFile() { return getAppDataDir().getChildFile ("index.bin"); }

int editDistance (const juce::String& a, const juce::String& b, int limit)
{
    const auto n = a.length(), m = b.length();
    if (std::abs (n - m) > limit)
        return limit + 1;

    std::vector<int> prev ((size_t) m + 1), cur ((size_t) m + 1);
    for (int j = 0; j <= m; ++j)
        prev[(size_t) j] = j;

    for (int i = 1; i <= n; ++i)
    {
        cur[0] = i;
        int rowMin = cur[0];
        for (int j = 1; j <= m; ++j)
        {
            const auto cost = a[i - 1] == b[j - 1] ? 0 : 1;
            cur[(size_t) j] = juce::jmin (prev[(size_t) j] + 1, cur[(size_t) j - 1] + 1, prev[(size_t) j - 1] + cost);
            rowMin = juce::jmin (rowMin, cur[(size_t) j]);
        }
        if (rowMin > limit)
            return limit + 1;
        std::swap (prev, cur);
    }
    return prev[(size_t) m];
}

std::shared_ptr<IndexData> buildData (std::vector<IndexEntry> entries, const juce::StringArray& roots, int generation)
{
    auto d = std::make_shared<IndexData>();
    d->roots = roots;
    d->generation = generation;

    for (auto& e : entries)
    {
        if (e.lower.isEmpty())
            e.lower = e.path.toLowerCase();

        const auto parsed = parseName (e.name);
        auto& b = d->bases[parsed.key];
        if (b.count == 0)
            b.display = parsed.display;
        ++b.count;
        b.maxNumber = juce::jmax (b.maxNumber, parsed.number);
    }

    d->entries = std::move (entries);
    return d;
}
} // namespace

ParsedName parseName (const juce::String& name)
{
    ParsedName p;
    auto end = name.length(), i = end;
    while (i > 0 && juce::CharacterFunctions::isDigit (name[i - 1]))
        --i;

    auto base = name;
    if (i < end && end - i <= 6 && i > 0)
    {
        p.number = name.substring (i).getIntValue();
        base = name.substring (0, i);
    }

    while (base.isNotEmpty() && isSeparator (base[base.length() - 1]))
        base = base.dropLastCharacters (1);

    p.display = base.isEmpty() ? name : base;
    p.key = p.display.toLowerCase();
    return p;
}

juce::String makeKey (const juce::String& stem)
{
    auto s = stem.trim();
    while (s.isNotEmpty() && isSeparator (s[s.length() - 1]))
        s = s.dropLastCharacters (1);
    return s.toLowerCase();
}

//==============================================================================
const BaseInfo* IndexData::find (const juce::String& key) const
{
    const auto it = bases.find (key);
    return it == bases.end() ? nullptr : &it->second;
}

juce::StringArray IndexData::findSimilar (const juce::String& key, const juce::StringArray& excludeKeys, int maxResults) const
{
    juce::StringArray out;
    if (key.length() < 4)
        return out;

    const auto limit = key.length() >= 8 ? 2 : 1;
    std::vector<std::pair<int, juce::String>> hits; // count, text

    for (const auto& [k, info] : bases)
    {
        if (k == key || excludeKeys.contains (k) || k.length() < 4)
            continue;
        if (editDistance (key, k, limit) <= limit)
            hits.emplace_back (info.count, info.display + " (" + juce::String (info.count) + ")");
    }

    std::sort (hits.begin(), hits.end(), [] (const auto& a, const auto& b) { return a.first > b.first; });
    for (size_t i = 0; i < hits.size() && (int) i < maxResults; ++i)
        out.add (hits[i].second);
    return out;
}

std::vector<int> IndexData::search (const juce::String& query, int maxResults) const
{
    std::vector<int> out;
    const auto tokens = juce::StringArray::fromTokens (query.toLowerCase(), " 　", "");
    if (tokens.isEmpty())
        return out;

    for (int i = 0; i < (int) entries.size() && (int) out.size() < maxResults; ++i)
    {
        bool all = true;
        for (const auto& t : tokens)
            if (! entries[(size_t) i].lower.contains (t))
            {
                all = false;
                break;
            }
        if (all)
            out.push_back (i);
    }
    return out;
}

//==============================================================================
LibraryIndex::LibraryIndex()
    : juce::Thread ("LibraryIndex")
{
    data = buildData ({}, {}, 0);
    startThread (juce::Thread::Priority::background);
}

LibraryIndex::~LibraryIndex()
{
    signalThreadShouldExit();
    wake.signal();
    stopThread (5000);
}

IndexDataPtr LibraryIndex::getData() const
{
    const juce::ScopedLock sl (lock);
    return data;
}

void LibraryIndex::setRoots (const juce::StringArray& newRoots)
{
    {
        const juce::ScopedLock sl (lock);
        roots = newRoots;
    }
    if (! loadCache() || getCacheFile().getLastModificationTime() < juce::Time::getCurrentTime() - juce::RelativeTime::hours (12))
        rescan();
}

bool LibraryIndex::loadCache()
{
    juce::FileInputStream in (getCacheFile());
    if (! in.openedOk() || in.readInt() != cacheMagic || in.readInt() != cacheVersion)
        return false;

    juce::StringArray cachedRoots;
    const auto numRoots = in.readInt();
    for (int i = 0; i < numRoots; ++i)
        cachedRoots.add (in.readString());
    if (cachedRoots != roots)
        return false;

    const auto count = in.readInt();
    if (count < 0)
        return false;

    std::vector<IndexEntry> entries;
    entries.reserve ((size_t) count);
    for (int i = 0; i < count && ! in.isExhausted(); ++i)
    {
        IndexEntry e;
        e.path = in.readString();
        e.size = in.readInt64();
        e.mtime = in.readInt64();
        e.root = in.readInt();
        e.name = juce::File (e.path).getFileNameWithoutExtension();
        entries.push_back (std::move (e));
    }

    publish (std::move (entries), false);
    return true;
}

void LibraryIndex::publish (std::vector<IndexEntry> entries, bool saveCache)
{
    juce::StringArray rootsCopy;
    {
        const juce::ScopedLock sl (lock);
        rootsCopy = roots;
    }
    auto built = buildData (std::move (entries), rootsCopy, ++generation);

    if (saveCache)
    {
        juce::MemoryOutputStream out;
        out.writeInt (cacheMagic);
        out.writeInt (cacheVersion);
        out.writeInt (rootsCopy.size());
        for (const auto& r : rootsCopy)
            out.writeString (r);
        out.writeInt ((int) built->entries.size());
        for (const auto& e : built->entries)
        {
            out.writeString (e.path);
            out.writeInt64 (e.size);
            out.writeInt64 (e.mtime);
            out.writeInt (e.root);
        }
        getCacheFile().replaceWithData (out.getData(), out.getDataSize());
    }

    {
        const juce::ScopedLock sl (lock);
        data = std::move (built);
    }
    sendChangeMessage();
}

void LibraryIndex::rescan()
{
    {
        const juce::ScopedLock sl (lock);
        queue.erase (std::remove_if (queue.begin(), queue.end(), [] (const Task& t) { return t.full; }), queue.end());
        queue.push_back ({ true, {} });
    }
    scanning = true;
    wake.signal();
}

void LibraryIndex::refreshFolder (const juce::File& folder)
{
    {
        const juce::ScopedLock sl (lock);
        queue.push_back ({ false, folder });
    }
    wake.signal();
}

void LibraryIndex::run()
{
    while (! threadShouldExit())
    {
        Task task;
        bool have = false;
        {
            const juce::ScopedLock sl (lock);
            if (! queue.empty())
            {
                task = queue.front();
                queue.erase (queue.begin());
                have = true;
            }
        }

        if (! have)
        {
            wake.wait (500);
            continue;
        }

        if (task.full)
            scanAll();
        else
            scanFolder (task.folder);
    }
}

void LibraryIndex::scanAll()
{
    scanning = true;
    scanned = 0;

    juce::StringArray rootsCopy;
    {
        const juce::ScopedLock sl (lock);
        rootsCopy = roots;
    }

    std::vector<IndexEntry> entries;
    for (int r = 0; r < rootsCopy.size(); ++r)
    {
        for (const auto& item : juce::RangedDirectoryIterator (juce::File (rootsCopy[r]), true, audioPatterns, juce::File::findFiles))
        {
            if (threadShouldExit())
                return;

            IndexEntry e;
            e.path = item.getFile().getFullPathName();
            e.name = item.getFile().getFileNameWithoutExtension();
            e.size = item.getFileSize();
            e.mtime = item.getModificationTime().toMilliseconds();
            e.root = r;
            entries.push_back (std::move (e));
            ++scanned;
        }
    }

    publish (std::move (entries), true);
    scanning = false;
}

void LibraryIndex::scanFolder (const juce::File& folder)
{
    auto current = getData();
    const auto prefix = folder.getFullPathName();

    std::vector<IndexEntry> entries;
    entries.reserve (current->entries.size() + 64);
    for (const auto& e : current->entries)
        if (! e.path.startsWithIgnoreCase (prefix))
            entries.push_back (e);

    int root = 0;
    for (int r = 0; r < current->roots.size(); ++r)
        if (prefix.startsWithIgnoreCase (current->roots[r]))
            root = r;

    for (const auto& item : juce::RangedDirectoryIterator (folder, true, audioPatterns, juce::File::findFiles))
    {
        IndexEntry e;
        e.path = item.getFile().getFullPathName();
        e.name = item.getFile().getFileNameWithoutExtension();
        e.size = item.getFileSize();
        e.mtime = item.getModificationTime().toMilliseconds();
        e.root = root;
        entries.push_back (std::move (e));
    }

    publish (std::move (entries), true);
}
} // namespace wt
