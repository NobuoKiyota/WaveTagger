#include "Dataset.h"
#include "Attributes.h"

namespace wt
{
namespace
{
juce::String lineOf (const juce::var& v)
{
    return juce::JSON::toString (v, true);
}

juce::String join (const juce::StringArray& parts, const juce::String& separator)
{
    return parts.joinIntoString (separator);
}

juce::StringArray attributeWords (const juce::String& attributes, const juce::String& keyValueSeparator)
{
    juce::StringArray words;
    for (const auto& t : parseAttributes (attributes))
        words.add (t.key.isEmpty() ? t.value : t.key + keyValueSeparator + t.value);
    return words;
}

juce::var tagArray (const juce::StringArray& tags)
{
    juce::Array<juce::var> out;
    for (const auto& t : tags)
        out.add (t);
    return out;
}

juce::StringArray tagsFor (const DatasetClip& c, bool japanese)
{
    juce::StringArray tags;
    const auto add = [&tags] (const juce::String& t) {
        const auto clean = t.trim();
        if (clean.isNotEmpty())
            tags.addIfNotAlreadyThere (clean, true);
    };

    add (japanese && c.categoryJa.isNotEmpty() ? c.categoryJa : c.category.toLowerCase());
    add (japanese && c.typeJa.isNotEmpty() ? c.typeJa : c.type.toLowerCase());
    for (const auto& t : parseAttributes (c.attributes))
        add (t.value);
    return tags;
}

/** The metadata fields that follow the labelling (they can change after the clip was first added). */
void setLabelFields (juce::DynamicObject& o, const DatasetClip& c)
{
    o.setProperty ("category", c.category);
    o.setProperty ("type", c.type);
    o.setProperty ("caption_en", datasetCaptionEn (c));
    o.setProperty ("caption_ja", datasetCaptionJa (c));
    o.setProperty ("tags_en", tagArray (tagsFor (c, false)));
    o.setProperty ("tags_ja", tagArray (tagsFor (c, true)));

    auto* attrs = new juce::DynamicObject();
    juce::Array<juce::var> freeTags;
    for (const auto& t : parseAttributes (c.attributes))
    {
        if (t.key.isEmpty())
            freeTags.add (t.value);
        else
            attrs->setProperty (t.key, t.value);
    }
    o.setProperty ("attributes", juce::var (attrs));
    o.setProperty ("free_tags", freeTags);
    o.setProperty ("rating", c.rating);
    o.setProperty ("ng", c.ng);
}

struct Line
{
    juce::String id;  // empty: not one of ours, kept as it is
    juce::var value;
    juce::String raw;
};
} // namespace

juce::String datasetClipId (const juce::String& sourceFile, juce::int64 sourceSize, juce::int64 start, juce::int64 end)
{
    const auto key = sourceFile.toLowerCase() + "|" + juce::String (sourceSize) + "|" + juce::String (start) + "|" + juce::String (end);
    // two independent 64-bit FNV-1a runs: 128 bits, the same on every PC (String::hashCode64 is not specified to be)
    const auto fnv = [&key] (juce::uint64 hash) {
        for (const auto* p = key.toUTF8().getAddress(); *p != 0; ++p)
        {
            hash ^= (juce::uint8) *p;
            hash *= 0x100000001b3ULL;
        }
        return hash;
    };
    return juce::String::toHexString ((juce::int64) fnv (0xcbf29ce484222325ULL)).paddedLeft ('0', 16)
           + juce::String::toHexString ((juce::int64) fnv (0x84222325cbf29ce4ULL)).paddedLeft ('0', 16);
}

juce::String datasetCaptionEn (const DatasetClip& c)
{
    juce::StringArray parts;
    parts.add (c.type + (c.category.isNotEmpty() ? " (" + c.category + ")" : juce::String()));
    parts.addArray (attributeWords (c.attributes, " "));
    return join (parts, ", ");
}

juce::String datasetCaptionJa (const DatasetClip& c)
{
    const auto type = c.typeJa.isNotEmpty() ? c.typeJa : c.type;
    const auto category = c.categoryJa.isNotEmpty() ? c.categoryJa : c.category;

    juce::StringArray parts;
    parts.add (type + (category.isNotEmpty() ? " (" + category + ")" : juce::String()));
    parts.addArray (attributeWords (c.attributes, " "));
    return join (parts, juce::String::fromUTF8 ("、"));
}

DatasetResult addToDataset (const AudioData& audio, const std::vector<DatasetClip>& clips, const juce::File& folder,
                            const ConditionOptions& conditioning, const juce::String& appVersion)
{
    DatasetResult result;
    if (! folder.createDirectory().wasOk())
    {
        result.errors.add ("cannot create " + folder.getFullPathName());
        return result;
    }

    const auto metadataFile = folder.getChildFile ("metadata.jsonl");
    const auto audioDir = folder.getChildFile ("audio");

    // what is there already; lines that are not ours (or cannot be read) are kept exactly as they are
    std::vector<Line> lines;
    std::map<juce::String, size_t> byId;
    juce::StringArray usedNames;
    if (metadataFile.existsAsFile())
    {
        juce::StringArray raw;
        raw.addLines (metadataFile.loadFileAsString());
        for (const auto& text : raw)
        {
            if (text.trim().isEmpty())
                continue;

            Line l;
            l.raw = text;
            const auto v = juce::JSON::parse (text);
            if (v.isObject() && v.getProperty ("id", {}).toString().isNotEmpty())
            {
                l.id = v.getProperty ("id", {}).toString();
                l.value = v;
                usedNames.add (juce::File (v.getProperty ("audio", {}).toString()).getFileNameWithoutExtension().toLowerCase());
                byId[l.id] = lines.size();
            }
            lines.push_back (std::move (l));
        }
    }

    const auto now = juce::Time::getCurrentTime().toISO8601 (true);
    bool changed = false;

    struct Pending
    {
        DatasetClip clip;
        juce::String id, fileName;
    };
    std::vector<Pending> pending;

    for (const auto& clip : clips)
    {
        if (clip.type.isEmpty() || clip.end <= clip.start || clip.end > audio.getNumSamples())
        {
            result.errors.add (clip.name + ": not a typed clip inside the recording");
            continue;
        }

        const auto id = datasetClipId (clip.sourceFile, clip.sourceSize, clip.start, clip.end);
        if (const auto it = byId.find (id); it != byId.end())
        {
            auto& line = lines[it->second];
            const auto before = lineOf (line.value);
            const auto wasNg = (bool) line.value.getProperty ("ng", false);

            auto* o = line.value.getDynamicObject();
            setLabelFields (*o, clip);

            const auto labelChanged = lineOf (line.value) != before;
            if (clip.ng != wasNg && clip.ng)
                ++result.ngMarked;
            else if (labelChanged)
                ++result.updated;
            else
            {
                ++result.unchanged;
                continue;
            }

            o->setProperty ("updatedAt", now);
            changed = true;
            continue;
        }

        if (clip.ng)
        {
            ++result.skippedNg;
            continue;
        }

        // a name that is taken (by another recording's clip, or by a file that is there) gets the id's start
        auto fileName = clip.name;
        if (usedNames.contains (fileName.toLowerCase()) || audioDir.getChildFile (fileName + ".wav").exists())
            fileName += "_" + id.substring (0, 8);
        usedNames.add (fileName.toLowerCase());
        pending.push_back ({ clip, id, fileName });
    }

    if (! pending.empty())
    {
        std::vector<ExportItem> items;
        for (const auto& p : pending)
            items.push_back ({ p.clip.start, p.clip.end, "audio", p.fileName, "WaveTagger dataset; id=" + p.id });

        const auto written = exportCycles (audio, items, folder, 24, 2.0f, conditioning);
        for (const auto& e : written.errors)
            result.errors.add ("not written: " + e);

        for (const auto& p : pending)
        {
            const auto file = audioDir.getChildFile (p.fileName + ".wav");
            if (! file.existsAsFile())
                continue;

            juce::String chain;
            for (const auto& [f, c] : written.writtenFiles)
                if (f == file)
                    chain = c;

            auto* o = new juce::DynamicObject();
            o->setProperty ("schema", 1);
            o->setProperty ("id", p.id);
            o->setProperty ("audio", "audio/" + p.fileName + ".wav");

            auto* original = new juce::DynamicObject();
            original->setProperty ("file", p.clip.sourceFile);
            original->setProperty ("start", p.clip.start);
            original->setProperty ("end", p.clip.end);
            o->setProperty ("original", juce::var (original));

            o->setProperty ("name", p.clip.name);
            setLabelFields (*o, p.clip);
            o->setProperty ("duration", (double) (p.clip.end - p.clip.start) / audio.sampleRate);
            o->setProperty ("sampleRate", audio.sampleRate);
            o->setProperty ("channels", audio.getNumChannels());
            o->setProperty ("bits", 24);
            o->setProperty ("conditioning", chain);
            o->setProperty ("exportedAt", now);
            o->setProperty ("app", "WaveTagger " + appVersion);

            Line l;
            l.id = p.id;
            l.value = juce::var (o);
            byId[p.id] = lines.size();
            lines.push_back (std::move (l));
            ++result.added;
            changed = true;
        }
    }

    if (changed)
    {
        juce::String text;
        for (const auto& l : lines)
            text += (l.id.isEmpty() ? l.raw : lineOf (l.value)) + "\n";

        // written next to the real file and swapped in: a failure leaves the old metadata as it was
        juce::TemporaryFile temp (metadataFile);
        if (! temp.getFile().replaceWithText (text, false, false, "\n") || ! temp.overwriteTargetFileWithTemporary())
            result.errors.add ("could not write " + metadataFile.getFullPathName());
    }

    const auto manifest = folder.getChildFile ("dataset.json");
    if (! manifest.existsAsFile())
    {
        auto* m = new juce::DynamicObject();
        m->setProperty ("schema", 1);
        m->setProperty ("app", "WaveTagger");
        m->setProperty ("created", now);
        m->setProperty ("audio", "audio/");
        m->setProperty ("metadata", "metadata.jsonl");
        m->setProperty ("note", "One JSON object per line in metadata.jsonl; clips with \"ng\": true are rejected takes (their audio is kept, filter them out when training).");
        manifest.replaceWithText (juce::JSON::toString (juce::var (m), false), false, false, "\n");
    }

    return result;
}
} // namespace wt
