#include "ReclassDb.h"
#include "Persistence.h"

namespace wt
{
juce::String ReclassDb::makeKey (const juce::StringArray& roots, const juce::File& file)
{
    for (const auto& r : roots)
    {
        const juce::File root (r);
        if (file.isAChildOf (root))
            return root.getFileName() + "/" + file.getRelativePathFrom (root).replaceCharacter ('\\', '/');
    }
    return file.getFullPathName();
}

juce::File ReclassDb::resolve (const juce::StringArray& roots, const juce::String& key)
{
    const auto slash = key.indexOfChar ('/');
    if (slash < 0)
    {
        const juce::File f (key);
        return f.existsAsFile() ? f : juce::File();
    }

    const auto rootName = key.substring (0, slash);
    const auto relative = key.substring (slash + 1);
    for (const auto& r : roots)
    {
        const juce::File root (r);
        if (root.getFileName().equalsIgnoreCase (rootName))
        {
            const auto f = root.getChildFile (relative);
            if (f.existsAsFile())
                return f;
        }
    }
    return {};
}

void ReclassDb::assign (const juce::String& key, const juce::String& category, const juce::String& type, const juce::String& group)
{
    entries[key] = { key, category, type, group };
}

const ReclassEntry* ReclassDb::find (const juce::String& key) const
{
    const auto it = entries.find (key);
    return it == entries.end() ? nullptr : &it->second;
}

bool ReclassDb::save (const juce::File& file) const
{
    juce::Array<juce::var> arr;
    for (const auto& [k, e] : entries)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("key", e.key);
        o->setProperty ("category", e.category);
        o->setProperty ("type", e.type);
        o->setProperty ("group", e.group);
        arr.add (juce::var (o));
    }

    auto* root = new juce::DynamicObject();
    root->setProperty ("version", 1);
    root->setProperty ("entries", arr);
    return writeJson (file, juce::var (root));
}

void ReclassDb::load (const juce::File& file)
{
    entries.clear();
    const auto json = readJson (file); // keep the parsed value alive while its array is used
    if (const auto* arr = json.getProperty ("entries", {}).getArray())
        for (const auto& v : *arr)
        {
            ReclassEntry e;
            e.key = v.getProperty ("key", "").toString();
            e.category = v.getProperty ("category", "").toString();
            e.type = v.getProperty ("type", "").toString();
            e.group = v.getProperty ("group", "").toString();
            if (e.key.isNotEmpty())
                entries[e.key] = e;
        }
}

std::vector<juce::StringArray> ReclassDb::toRows() const
{
    std::vector<juce::StringArray> rows { { "Key", "Category", "Type", "Group" } };
    for (const auto& [k, e] : entries)
        rows.push_back ({ e.key, e.category, e.type, e.group });
    return rows;
}
} // namespace wt
