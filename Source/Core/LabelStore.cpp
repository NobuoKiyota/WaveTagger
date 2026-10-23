#include "LabelStore.h"

namespace wt
{
namespace
{
juce::String safeName (juce::String s)
{
    juce::String out;
    for (auto c : s)
        out += (juce::CharacterFunctions::isLetterOrDigit (c) || c == '-' || c == '_') ? juce::String::charToString (c) : juce::String ("_");
    return out.isEmpty() ? juce::String ("pc") : out.substring (0, 40);
}

juce::String toLine (const LabelSample& s)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("key", s.key);
    o->setProperty ("file", s.fileName);
    o->setProperty ("start", s.start);
    o->setProperty ("end", s.end);
    o->setProperty ("category", s.category);
    o->setProperty ("type", s.type);
    o->setProperty ("source", s.source);
    o->setProperty ("human", s.human);
    o->setProperty ("sugCategory", s.suggestedCategory);
    o->setProperty ("sugType", s.suggestedType);
    o->setProperty ("sugConf", (double) s.suggestedConfidence);
    o->setProperty ("rating", s.rating);
    o->setProperty ("ng", s.ng);
    o->setProperty ("attrs", s.attributes);
    o->setProperty ("t", s.updatedAt);

    juce::Array<juce::var> f;
    for (auto x : s.v)
        f.add (std::round ((double) x * 10000.0) / 10000.0);
    o->setProperty ("f", f);

    return juce::JSON::toString (juce::var (o), true);
}

bool fromLine (const juce::String& line, LabelSample& s)
{
    const auto v = juce::JSON::parse (line);
    if (! v.isObject())
        return false;

    const auto* f = v.getProperty ("f", {}).getArray();
    if (f == nullptr || f->size() != featureDims)
        return false;

    s.key = v.getProperty ("key", "").toString();
    if (s.key.isEmpty())
        return false;

    s.fileName = v.getProperty ("file", "").toString();
    s.start = (juce::int64) v.getProperty ("start", 0);
    s.end = (juce::int64) v.getProperty ("end", 0);
    s.category = v.getProperty ("category", "").toString();
    s.type = v.getProperty ("type", "").toString();
    s.source = v.getProperty ("source", "").toString();
    s.human = (bool) v.getProperty ("human", true);
    s.suggestedCategory = v.getProperty ("sugCategory", "").toString();
    s.suggestedType = v.getProperty ("sugType", "").toString();
    s.suggestedConfidence = (float) (double) v.getProperty ("sugConf", -1.0);
    s.rating = (int) v.getProperty ("rating", 0);
    s.ng = (bool) v.getProperty ("ng", false);
    s.attributes = v.getProperty ("attrs", "").toString();
    s.updatedAt = (juce::int64) v.getProperty ("t", 0);
    for (int i = 0; i < featureDims; ++i)
        s.v[(size_t) i] = (float) (double) (*f)[i];
    return true;
}
} // namespace

void LabelStore::setLocation (juce::File directory, juce::String pcName)
{
    dir = std::move (directory);
    pc = safeName (std::move (pcName));
}

juce::String LabelStore::makeKey (const juce::String& fileName, juce::int64 fileSize, juce::int64 start, juce::int64 end)
{
    return fileName.toLowerCase() + "|" + juce::String (fileSize) + "|" + juce::String (start) + "|" + juce::String (end);
}

void LabelStore::load()
{
    samples.clear();
    ++revision;
    if (! dir.isDirectory())
        return;

    for (const auto& file : dir.findChildFiles (juce::File::findFiles, false, "labels_*.jsonl"))
        for (const auto& line : juce::StringArray::fromLines (file.loadFileAsString()))
        {
            if (line.trim().isEmpty())
                continue;

            LabelSample s;
            if (! fromLine (line, s))
                continue; // a damaged or partly synced line is skipped, never fatal

            const auto it = samples.find (s.key);
            if (it == samples.end() || it->second.updatedAt <= s.updatedAt)
                samples[s.key] = s;
        }
}

void LabelStore::upsert (std::vector<LabelSample> list)
{
    if (list.empty())
        return;

    juce::String text;
    for (auto& s : list)
    {
        if (s.updatedAt == 0)
            s.updatedAt = juce::Time::currentTimeMillis();
        samples[s.key] = s;
        text += toLine (s) + "\n";
    }
    ++revision;

    if (dir.getFullPathName().isNotEmpty())
    {
        dir.createDirectory();
        myFile().appendText (text, false, false);
    }
}

const LabelSample* LabelStore::find (const juce::String& key) const
{
    const auto it = samples.find (key);
    return it == samples.end() ? nullptr : &it->second;
}

size_t LabelStore::humanLabelCount() const
{
    size_t n = 0;
    for (const auto& [k, s] : samples)
        n += s.human && s.type.isNotEmpty() ? 1 : 0;
    return n;
}
} // namespace wt
