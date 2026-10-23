#include "Checks.h"
#include "Persistence.h"

namespace wt
{
namespace
{
/**
    Largest number of edits that still counts as "the same name, misspelt" for a name of this length.
    Strict: only longer names (1 edit from 8 letters, 2 from 16). Normal: 1 edit from 6 letters, 2 from 10.
    Loose: from 4 letters, up to ~30 % of the length. A swap of two neighbouring letters counts as one edit.
*/
int allowedEdits (int length, NameSensitivity s)
{
    switch (s)
    {
        case NameSensitivity::Strict: return length >= 16 ? 2 : (length >= 8 ? 1 : 0);
        case NameSensitivity::Loose:  return length >= 5 ? juce::jmax (2, (int) std::round (length * 0.3)) : (length >= 4 ? 1 : 0);
        default:                      return length >= 10 ? 2 : (length >= 6 ? 1 : 0);
    }
}

using Chars = std::vector<juce::juce_wchar>;

/** Edit distance with adjacent swaps (optimal string alignment), giving up above `limit`. Common ends are trimmed first. */
int editsWithin (const Chars& a, const Chars& b, int limit)
{
    const auto n0 = (int) a.size(), m0 = (int) b.size();
    if (std::abs (n0 - m0) > limit)
        return limit + 1;

    int p = 0;
    while (p < n0 && p < m0 && a[(size_t) p] == b[(size_t) p])
        ++p;
    int s = 0;
    while (s < n0 - p && s < m0 - p && a[(size_t) (n0 - 1 - s)] == b[(size_t) (m0 - 1 - s)])
        ++s;

    const auto n = n0 - p - s, m = m0 - p - s;
    if (n == 0 || m == 0)
        return juce::jmax (n, m);
    if (n > 24 || m > 24)
        return limit + 1;

    int d[26][26];
    for (int i = 0; i <= n; ++i) d[i][0] = i;
    for (int j = 0; j <= m; ++j) d[0][j] = j;

    for (int i = 1; i <= n; ++i)
    {
        int rowMin = limit + 1;
        for (int j = 1; j <= m; ++j)
        {
            const auto x = a[(size_t) (p + i - 1)], y = b[(size_t) (p + j - 1)];
            int v = juce::jmin (d[i - 1][j] + 1, d[i][j - 1] + 1, d[i - 1][j - 1] + (x == y ? 0 : 1));
            if (i > 1 && j > 1 && x == b[(size_t) (p + j - 2)] && a[(size_t) (p + i - 2)] == y)
                v = juce::jmin (v, d[i - 2][j - 2] + 1);
            d[i][j] = v;
            rowMin = juce::jmin (rowMin, v);
        }
        if (rowMin > limit)
            return limit + 1;
    }
    return d[n][m];
}

bool isSeparatorChar (juce::juce_wchar c) { return c == '_' || c == '-' || c == ' ' || c == '.'; }

/**
    The name with its "series label" taken off: digits, and one trailing label letter that is upper-case or set off by a
    separator (SynthC, Synth_B, "Piano d", "Eliot 3 V"). Two names that normalise to the same thing are numbered / lettered
    variants of one sound family, not misspellings of each other.
*/
juce::String seriesStem (const juce::String& display)
{
    juce::String s;
    for (auto c : display)
        if (! juce::CharacterFunctions::isDigit (c))
            s += c;

    const auto trim = [&s] {
        while (s.isNotEmpty() && isSeparatorChar (s[s.length() - 1]))
            s = s.dropLastCharacters (1);
    };
    trim();

    if (s.length() > 1)
    {
        const auto last = s[s.length() - 1];
        if (juce::CharacterFunctions::isLetter (last) && (juce::CharacterFunctions::isUpperCase (last) || isSeparatorChar (s[s.length() - 2])))
        {
            s = s.dropLastCharacters (1);
            trim();
        }
    }
    return s.toLowerCase();
}

struct DisjointSets
{
    std::vector<int> parent;
    explicit DisjointSets (size_t n) : parent (n) { std::iota (parent.begin(), parent.end(), 0); }
    int find (int x) { return parent[(size_t) x] == x ? x : parent[(size_t) x] = find (parent[(size_t) x]); }
    void join (int a, int b) { parent[(size_t) find (a)] = find (b); }
};

constexpr juce::uint64 fnvBasis = 1469598103934665603ULL;
constexpr juce::uint64 fnvPrime = 1099511628211ULL;

void fnvUpdate (juce::uint64& h, const char* data, size_t n)
{
    for (size_t i = 0; i < n; ++i)
    {
        h ^= (juce::uint64) (juce::uint8) data[i];
        h *= fnvPrime;
    }
}

juce::uint64 quickHash (const juce::File& f, juce::int64 size)
{
    juce::FileInputStream in (f);
    if (! in.openedOk())
        return 0;

    constexpr int block = 64 * 1024;
    std::vector<char> buf ((size_t) block);
    juce::uint64 h = fnvBasis;
    fnvUpdate (h, reinterpret_cast<const char*> (&size), sizeof (size));

    const auto n = in.read (buf.data(), block);
    fnvUpdate (h, buf.data(), (size_t) juce::jmax (0, n));
    if (size > 2 * block)
    {
        in.setPosition (size - block);
        const auto m = in.read (buf.data(), block);
        fnvUpdate (h, buf.data(), (size_t) juce::jmax (0, m));
    }
    return h == 0 ? 1 : h;
}

juce::uint64 fullHash (const juce::File& f)
{
    juce::FileInputStream in (f);
    if (! in.openedOk())
        return 0;

    std::vector<char> buf (1024 * 1024);
    juce::uint64 h = fnvBasis;
    for (;;)
    {
        const auto n = in.read (buf.data(), (int) buf.size());
        if (n <= 0)
            break;
        fnvUpdate (h, buf.data(), (size_t) n);
    }
    return h == 0 ? 1 : h;
}

constexpr int hashMagic = 0x48485457; // "WTHH"
} // namespace

//==============================================================================
juce::String ignoreKey (const juce::String& a, const juce::String& b)
{
    return a < b ? a + "|" + b : b + "|" + a;
}

std::vector<NameGroup> findSimilarNames (const IndexData& index, NameSensitivity sensitivity, const juce::StringArray& ignored)
{
    // one example file per base name
    std::unordered_map<juce::String, juce::File> example;
    for (const auto& e : index.entries)
    {
        const auto key = parseName (e.name).key;
        if (example.find (key) == example.end())
            example.emplace (key, juce::File (e.path));
    }

    std::vector<NameVariant> names;
    names.reserve (index.bases.size());
    for (const auto& [key, info] : index.bases)
        if (key.length() >= 4)
            names.push_back ({ info.display, key, info.count, example[key] });

    const auto n = (int) names.size();
    std::vector<Chars> chars ((size_t) n);
    std::vector<juce::String> stems ((size_t) n);
    std::unordered_map<juce::String, int> lastLetterSiblings; // "piano" -> how many names are "piano" + one more character
    for (int i = 0; i < n; ++i)
    {
        for (auto c : names[(size_t) i].key)
            chars[(size_t) i].push_back (c);
        stems[(size_t) i] = seriesStem (names[(size_t) i].display);
        ++lastLetterSiblings[names[(size_t) i].key.dropLastCharacters (1)];
    }

    const auto strippedDigits = [] (const juce::String& k) {
        juce::String out;
        for (auto c : k)
            if (! juce::CharacterFunctions::isDigit (c))
                out += c;
        return out;
    };

    // ---- candidate pairs: only names that share their first (or last) three characters and have a close length
    std::vector<std::vector<int>> neighbours ((size_t) n);
    std::set<std::pair<int, int>> pairSeen;

    const auto consider = [&] (int a, int b) {
        const auto& x = names[(size_t) a];
        const auto& y = names[(size_t) b];
        const auto limit = juce::jmin (allowedEdits (x.key.length(), sensitivity), allowedEdits (y.key.length(), sensitivity));
        if (limit <= 0)
            return;

        if (sensitivity == NameSensitivity::Loose)
        {
            if (strippedDigits (x.key) == strippedDigits (y.key)) // numbers only: never a misspelling
                return;
        }
        else
        {
            if (stems[(size_t) a] == stems[(size_t) b]) // numbered / lettered series of one sound family
                return;

            // "Pianoa", "Pianob", "Pianoc" ...: three or more names that differ only in the last letter are a lettered series
            const auto px = x.key.dropLastCharacters (1), py = y.key.dropLastCharacters (1);
            if (px == py && lastLetterSiblings[px] >= 3)
                return;
            if ((x.key == py && lastLetterSiblings[x.key] >= 3) || (y.key == px && lastLetterSiblings[y.key] >= 3))
                return;
        }

        if (editsWithin (chars[(size_t) a], chars[(size_t) b], limit) > limit || ! pairSeen.insert ({ juce::jmin (a, b), juce::jmax (a, b) }).second)
            return;
        if (ignored.contains (ignoreKey (x.key, y.key)))
            return;

        neighbours[(size_t) a].push_back (b);
        neighbours[(size_t) b].push_back (a);
    };

    for (const auto tail : { false, true })
    {
        std::unordered_map<juce::String, std::vector<int>> buckets;
        for (int i = 0; i < n; ++i)
        {
            const auto& k = names[(size_t) i].key;
            buckets[(tail ? k.substring (k.length() - 3) : k.substring (0, 3)) + "#" + juce::String (k.length())].push_back (i);
        }

        for (int i = 0; i < n; ++i)
        {
            const auto& k = names[(size_t) i].key;
            const auto part = tail ? k.substring (k.length() - 3) : k.substring (0, 3);
            for (int delta = 0; delta <= allowedEdits (k.length(), sensitivity); ++delta)
            {
                const auto it = buckets.find (part + "#" + juce::String (k.length() + delta));
                if (it == buckets.end())
                    continue;
                for (auto j : it->second)
                    if (delta > 0 || j > i)
                        consider (i, j);
            }
        }
    }

    // ---- clusters built around the most-used spelling (no long chains of look-alikes)
    std::vector<int> order ((size_t) n);
    std::iota (order.begin(), order.end(), 0);
    std::sort (order.begin(), order.end(), [&] (int a, int b) { return names[(size_t) a].count > names[(size_t) b].count; });

    std::vector<int> cluster ((size_t) n, -1);
    std::vector<NameGroup> out;
    for (auto i : order)
    {
        if (cluster[(size_t) i] >= 0 || neighbours[(size_t) i].empty())
            continue;

        int joined = -1, best = -1;
        for (auto j : neighbours[(size_t) i])
            if (cluster[(size_t) j] >= 0 && names[(size_t) j].count > best)
            {
                best = names[(size_t) j].count;
                joined = cluster[(size_t) j];
            }

        if (joined < 0)
        {
            joined = (int) out.size();
            out.emplace_back();
            out[(size_t) joined].variants.push_back (names[(size_t) i]);
            cluster[(size_t) i] = joined;
            for (auto j : neighbours[(size_t) i])
                if (cluster[(size_t) j] < 0)
                {
                    cluster[(size_t) j] = joined;
                    out[(size_t) joined].variants.push_back (names[(size_t) j]);
                }
        }
        else
        {
            cluster[(size_t) i] = joined;
            out[(size_t) joined].variants.push_back (names[(size_t) i]);
        }
    }

    std::vector<NameGroup> result;
    for (auto& g : out)
        if (g.variants.size() >= 2)
        {
            std::sort (g.variants.begin(), g.variants.end(), [] (const NameVariant& a, const NameVariant& b) { return a.count > b.count; });
            result.push_back (std::move (g));
        }

    // Most likely typos first: a common spelling next to one that is used only once or twice. Groups where the
    // spellings are about equally common (two intentional naming styles) go to the end.
    const auto imbalance = [] (const NameGroup& g) { return (double) g.variants[0].count / (double) juce::jmax (1, g.variants[1].count); };
    std::sort (result.begin(), result.end(), [&] (const NameGroup& a, const NameGroup& b) {
        const auto ra = imbalance (a), rb = imbalance (b);
        return ra != rb ? ra > rb : a.variants[0].count > b.variants[0].count;
    });
    return result;
}

//==============================================================================
juce::File HashStore::defaultFile()
{
    return getAppDataDir().getChildFile ("hashes.bin");
}

void HashStore::load()
{
    records.clear();
    pending.reset();
    needsHeader = true;

    juce::FileInputStream in (storeFile);
    if (! in.openedOk() || in.getTotalLength() < 8 || in.readInt() != hashMagic || in.readInt() != 1)
        return;

    needsHeader = false;
    while (! in.isExhausted())
    {
        const auto path = in.readString();
        HashRecord r;
        r.size = in.readInt64();
        r.mtime = in.readInt64();
        r.quick = (juce::uint64) in.readInt64();
        r.full = (juce::uint64) in.readInt64();
        if (path.isEmpty())
            break;
        records[path] = r;
    }
}

void HashStore::flush()
{
    if (pending.getDataSize() == 0)
        return;

    if (needsHeader)
    {
        storeFile.deleteFile();
        juce::FileOutputStream header (storeFile);
        if (! header.openedOk())
            return;
        header.writeInt (hashMagic);
        header.writeInt (1);
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

const HashRecord* HashStore::find (const juce::String& path, juce::int64 size, juce::int64 mtime) const
{
    const auto it = records.find (path.toLowerCase());
    return it != records.end() && it->second.size == size && it->second.mtime == mtime ? &it->second : nullptr;
}

void HashStore::put (const juce::String& path, const HashRecord& r)
{
    const auto key = path.toLowerCase();
    records[key] = r;
    pending.writeString (key);
    pending.writeInt64 (r.size);
    pending.writeInt64 (r.mtime);
    pending.writeInt64 ((juce::int64) r.quick);
    pending.writeInt64 ((juce::int64) r.full);
}

std::vector<DuplicateGroup> findExactDuplicates (const IndexData& index, HashStore& hashes, const std::function<bool (int, int)>& progress)
{
    // stage 0: only files that share their size with another file can be duplicates
    std::unordered_map<juce::int64, std::vector<int>> bySize;
    for (int i = 0; i < (int) index.entries.size(); ++i)
        if (index.entries[(size_t) i].size >= 512)
            bySize[index.entries[(size_t) i].size].push_back (i);

    std::vector<std::vector<int>> candidates;
    int total = 0;
    for (auto& [size, members] : bySize)
        if (members.size() >= 2)
        {
            total += (int) members.size();
            candidates.push_back (std::move (members));
        }

    int done = 0;
    bool cancelled = false;
    const auto tick = [&] {
        if (progress && ! progress (++done, total * 2))
            cancelled = true;
        return ! cancelled;
    };

    // stage 1: first/last 64 KB
    std::vector<std::vector<int>> narrowed;
    for (const auto& group : candidates)
    {
        std::map<juce::uint64, std::vector<int>> byQuick;
        for (auto i : group)
        {
            if (! tick())
                return {};

            const auto& e = index.entries[(size_t) i];
            auto* rec = hashes.find (e.path, e.size, e.mtime);
            HashRecord r;
            if (rec != nullptr && rec->quick != 0)
            {
                r = *rec;
            }
            else
            {
                r = { e.size, e.mtime, quickHash (juce::File (e.path), e.size), 0 };
                hashes.countComputed();
                hashes.put (e.path, r);
            }
            if (r.quick != 0)
                byQuick[r.quick].push_back (i);
        }

        for (auto& [q, members] : byQuick)
            if (members.size() >= 2)
                narrowed.push_back (std::move (members));
    }

    // stage 2: the whole file (only for those that still look identical)
    std::vector<DuplicateGroup> out;
    for (const auto& group : narrowed)
    {
        std::map<juce::uint64, std::vector<int>> byFull;
        for (auto i : group)
        {
            if (progress && ! progress (done, total * 2))
                return out;

            const auto& e = index.entries[(size_t) i];
            auto* rec = hashes.find (e.path, e.size, e.mtime);
            HashRecord r = rec != nullptr ? *rec : HashRecord { e.size, e.mtime, 0, 0 };
            if (r.full == 0)
            {
                r.full = fullHash (juce::File (e.path));
                hashes.countComputed();
                hashes.put (e.path, r);
            }
            if (r.full != 0)
                byFull[r.full].push_back (i);
        }

        for (auto& [h, members] : byFull)
            if (members.size() >= 2)
            {
                DuplicateGroup g;
                g.size = index.entries[(size_t) members[0]].size;
                for (auto i : members)
                    g.files.push_back (juce::File (index.entries[(size_t) i].path));
                out.push_back (std::move (g));
            }
    }

    hashes.flush();
    std::sort (out.begin(), out.end(), [] (const DuplicateGroup& a, const DuplicateGroup& b) { return a.size * (juce::int64) a.files.size() > b.size * (juce::int64) b.files.size(); });
    return out;
}

std::vector<DuplicateGroup> findNearDuplicates (const std::vector<juce::File>& files, const FeatureStore& features)
{
    struct Item
    {
        juce::File file;
        FeatureVector v;
    };
    std::vector<Item> items;
    for (const auto& f : files)
        if (const auto* r = features.find (f))
            items.push_back ({ f, r->v });

    // sort by (log) duration so that only neighbours within a few percent are compared
    std::sort (items.begin(), items.end(), [] (const Item& a, const Item& b) { return a.v[45] < b.v[45]; });

    const auto similar = [] (const FeatureVector& a, const FeatureVector& b) {
        double mean = 0.0, spread = 0.0;
        for (int i = 0; i < 20; ++i)
        {
            mean += std::abs (a[(size_t) i] - b[(size_t) i]);
            spread += std::abs (a[(size_t) 20 + (size_t) i] - b[(size_t) 20 + (size_t) i]);
        }
        return mean / 20.0 < 1.0 && spread / 20.0 < 1.0 && std::abs (a[40] - b[40]) < 0.01 && std::abs (a[41] - b[41]) < 0.02
               && std::abs (a[43] - b[43]) < 1.0;
    };

    DisjointSets sets (items.size());
    for (size_t i = 0; i < items.size(); ++i)
        for (size_t j = i + 1; j < items.size() && items[j].v[45] - items[i].v[45] < 0.015f; ++j)
            if (similar (items[i].v, items[j].v))
                sets.join ((int) i, (int) j);

    std::map<int, DuplicateGroup> grouped;
    for (size_t i = 0; i < items.size(); ++i)
    {
        auto& g = grouped[sets.find ((int) i)];
        g.files.push_back (items[i].file);
        g.size = juce::jmax (g.size, items[i].file.getSize());
        g.exact = false;
    }

    std::vector<DuplicateGroup> out;
    for (auto& [root, g] : grouped)
        if (g.files.size() >= 2)
            out.push_back (std::move (g));
    return out;
}
} // namespace wt
