#include "Dataset.h"
#include "Attributes.h"
#include "EmbeddedState.h"
#include "WavChunks.h"

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

/** One typed word in both languages. `missing`: the dictionary was asked and has no translation. */
struct Bilingual
{
    juce::String en, ja;
    bool missing = false;
};

Bilingual bilingual (const juce::String& word, const Dictionary* dictionary)
{
    if (dictionary == nullptr || word.isEmpty() || Dictionary::isNeutral (word))
        return { word, word, false };

    if (Dictionary::isJapanese (word))
    {
        const auto en = dictionary->toEnglish (word);
        return { en ? *en : word, word, ! en };
    }

    const auto ja = dictionary->toJapanese (word);
    return { word, ja ? *ja : word, ! ja };
}

juce::StringArray attributeWords (const juce::String& attributes, bool japanese, const Dictionary* dictionary)
{
    juce::StringArray words;
    for (const auto& t : parseAttributes (attributes))
    {
        const auto value = bilingual (t.value, dictionary);
        if (t.key.isEmpty())
        {
            words.add (japanese ? value.ja : value.en);
            continue;
        }

        const auto key = bilingual (t.key, dictionary);
        words.add (japanese ? key.ja + " " + value.ja : key.en + " " + value.en);
    }
    return words;
}

juce::var tagArray (const juce::StringArray& tags)
{
    juce::Array<juce::var> out;
    for (const auto& t : tags)
        out.add (t);
    return out;
}

juce::StringArray tagsFor (const DatasetClip& c, bool japanese, const Dictionary* dictionary)
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
    {
        const auto value = bilingual (t.value, dictionary);
        add (japanese ? value.ja : value.en);
    }
    return tags;
}

/** The metadata fields that follow the labelling (they can change after the clip was first added). */
void setLabelFields (juce::DynamicObject& o, const DatasetClip& c, const Dictionary* dictionary, juce::StringArray& untranslated)
{
    o.setProperty ("category", c.category);
    o.setProperty ("type", c.type);
    o.setProperty ("caption_en", datasetCaptionEn (c, dictionary));
    o.setProperty ("caption_ja", datasetCaptionJa (c, dictionary));
    o.setProperty ("tags_en", tagArray (tagsFor (c, false, dictionary)));
    o.setProperty ("tags_ja", tagArray (tagsFor (c, true, dictionary)));

    // as typed, and in each language
    auto* attrs = new juce::DynamicObject();
    auto* attrsEn = new juce::DynamicObject();
    auto* attrsJa = new juce::DynamicObject();
    juce::Array<juce::var> freeTags;
    juce::StringArray missing;
    for (const auto& t : parseAttributes (c.attributes))
    {
        const auto value = bilingual (t.value, dictionary);
        if (value.missing)
            missing.addIfNotAlreadyThere (t.value);

        if (t.key.isEmpty())
        {
            freeTags.add (t.value);
            continue;
        }

        const auto key = bilingual (t.key, dictionary);
        if (key.missing)
            missing.addIfNotAlreadyThere (t.key);

        attrs->setProperty (t.key, t.value);
        attrsEn->setProperty (key.en, value.en);
        attrsJa->setProperty (key.ja, value.ja);
    }
    o.setProperty ("attributes", juce::var (attrs));
    o.setProperty ("attributes_en", juce::var (attrsEn));
    o.setProperty ("attributes_ja", juce::var (attrsJa));
    o.setProperty ("free_tags", freeTags);
    o.setProperty ("untranslated", tagArray (missing));
    untranslated.addArray (missing);
    o.setProperty ("rating", c.rating);
    o.setProperty ("ng", c.ng);
}

struct Line
{
    juce::String id;  // empty: not one of ours, kept as it is
    juce::var value;
    juce::String raw;
};
juce::String hashedId (const juce::String& key)
{
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
} // namespace

juce::String datasetClipId (const juce::String& sourceFile, juce::int64 sourceFrames, juce::int64 start, juce::int64 end)
{
    return hashedId (sourceFile.toLowerCase() + "|" + juce::String (sourceFrames) + "|" + juce::String (start) + "|" + juce::String (end));
}

juce::String datasetLegacyClipId (const juce::String& sourceFile, juce::int64 sourceSize, juce::int64 start, juce::int64 end)
{
    return hashedId (sourceFile.toLowerCase() + "|" + juce::String (sourceSize) + "|" + juce::String (start) + "|" + juce::String (end));
}

juce::String datasetCaptionEn (const DatasetClip& c, const Dictionary* dictionary)
{
    juce::StringArray parts;
    parts.add (c.type + (c.category.isNotEmpty() ? " (" + c.category + ")" : juce::String()));
    parts.addArray (attributeWords (c.attributes, false, dictionary));
    return join (parts, ", ");
}

juce::String datasetCaptionJa (const DatasetClip& c, const Dictionary* dictionary)
{
    const auto type = c.typeJa.isNotEmpty() ? c.typeJa : c.type;
    const auto category = c.categoryJa.isNotEmpty() ? c.categoryJa : c.category;

    juce::StringArray parts;
    parts.add (type + (category.isNotEmpty() ? " (" + category + ")" : juce::String()));
    parts.addArray (attributeWords (c.attributes, true, dictionary));
    return join (parts, juce::String::fromUTF8 ("、"));
}

DatasetResult addToDataset (const DatasetAudioProvider& audioProvider, const std::vector<DatasetClip>& clips, const juce::File& folder,
                            const ConditionOptions& conditioning, const juce::String& appVersion, const Dictionary* dictionary)
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
    std::vector<Pending> candidates;

    for (const auto& clip : clips)
    {
        if (clip.type.isEmpty() || clip.end <= clip.start)
        {
            result.errors.add (clip.name + ": not a typed clip inside the recording");
            continue;
        }

        const auto id = datasetClipId (clip.sourceFile, clip.sourceFrames, clip.start, clip.end);

        // a line made before ids stopped depending on the file size (which changes whenever markers are written)
        auto found = byId.find (id);
        const auto legacyId = clip.sourceSize > 0 ? datasetLegacyClipId (clip.sourceFile, clip.sourceSize, clip.start, clip.end) : juce::String();
        const auto upgrading = found == byId.end() && legacyId.isNotEmpty() && byId.find (legacyId) != byId.end();
        if (upgrading)
            found = byId.find (legacyId);

        if (found != byId.end())
        {
            const auto index = found->second;
            auto& line = lines[index];
            const auto before = lineOf (line.value);
            const auto wasNg = (bool) line.value.getProperty ("ng", false);

            auto* o = line.value.getDynamicObject();
            setLabelFields (*o, clip, dictionary, result.untranslated);

            const auto labelChanged = lineOf (line.value) != before;
            if (clip.ng != wasNg && clip.ng)
                ++result.ngMarked;
            else if (labelChanged || upgrading)
                ++result.updated;
            else
            {
                ++result.unchanged;
                continue;
            }

            if (upgrading)
            {
                byId.erase (legacyId);
                byId[id] = index;
                line.id = id;
                o->setProperty ("id", id);
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

        candidates.push_back ({ clip, id, {} });
    }

    // the recording is read only now, and only because something has to be cut from it
    std::vector<Pending> pending;
    if (! candidates.empty())
    {
        const auto audioPtr = audioProvider ? audioProvider() : AudioDataPtr();
        const auto* audio = audioPtr.get();
        for (auto& p : candidates)
        {
            if (audio == nullptr)
            {
                result.errors.add (p.clip.name + ": the recording could not be read");
                continue;
            }
            if (p.clip.end > audio->getNumSamples())
            {
                result.errors.add (p.clip.name + ": not a typed clip inside the recording");
                continue;
            }

            // a name that is taken (by another recording's clip, or by a file that is there) gets the id's start
            auto fileName = p.clip.name;
            if (usedNames.contains (fileName.toLowerCase()) || audioDir.getChildFile (fileName + ".wav").exists())
                fileName += "_" + p.id.substring (0, 8);
            usedNames.add (fileName.toLowerCase());
            p.fileName = fileName;
            pending.push_back (p);
        }

        if (! pending.empty())
        {
            std::vector<ExportItem> items;
            for (const auto& p : pending)
                items.push_back ({ p.clip.start, p.clip.end, "audio", p.fileName, "WaveTagger dataset; id=" + p.id });

            const auto written = exportCycles (*audio, items, folder, 24, 2.0f, conditioning);
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
                setLabelFields (*o, p.clip, dictionary, result.untranslated);
                o->setProperty ("duration", (double) (p.clip.end - p.clip.start) / audio->sampleRate);
                o->setProperty ("sampleRate", audio->sampleRate);
                o->setProperty ("channels", audio->getNumChannels());
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
    }

    result.untranslated.removeDuplicates (true);

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

DatasetResult addToDataset (const AudioData& audio, const std::vector<DatasetClip>& clips, const juce::File& folder,
                            const ConditionOptions& conditioning, const juce::String& appVersion, const Dictionary* dictionary)
{
    // not owned: the caller's recording outlives the call
    const AudioDataPtr view (AudioDataPtr(), &audio);
    return addToDataset ([view] { return view; }, clips, folder, conditioning, appVersion, dictionary);
}

//==============================================================================
JapaneseNames JapaneseNames::from (const Catalog& catalog)
{
    JapaneseNames names;
    for (const auto& c : catalog.getCategories())
    {
        names.category[c.name.toLowerCase()] = c.jp;
        for (const auto& t : catalog.getTypes (c.name, false))
            names.type[c.name.toLowerCase() + "|" + t.name.toLowerCase()] = t.jp;
    }
    for (const auto& t : catalog.getTypes ({}, false)) // (an empty Category means the common Types; asking for "*" itself gives nothing)
        names.type[juce::String ("*|") + t.name.toLowerCase()] = t.jp;
    return names;
}

juce::String JapaneseNames::forCategory (const juce::String& categoryName) const
{
    const auto it = category.find (categoryName.toLowerCase());
    return it == category.end() ? juce::String() : it->second;
}

juce::String JapaneseNames::forType (const juce::String& categoryName, const juce::String& typeName) const
{
    for (const auto& key : { categoryName.toLowerCase() + "|" + typeName.toLowerCase(), juce::String ("*|") + typeName.toLowerCase() })
        if (const auto it = type.find (key); it != type.end())
            return it->second;
    return {};
}

WavTagResult datasetClipsFromWav (const juce::File& wav, juce::AudioFormatManager& formats, const JapaneseNames& names, WavClips& out)
{
    out = {};

    juce::var json;
    WtagData tags;
    if (! readWtag (wav, json) || ! parseWtag (json, tags))
        return WavTagResult::noTags;

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (wav));
    if (reader == nullptr)
        return WavTagResult::unreadable;

    out.frames = reader->lengthInSamples;
    const auto size = wav.getSize();
    const auto categoryJa = names.forCategory (tags.category);

    for (const auto& c : tags.cycles)
    {
        if (c.typeName.isEmpty())
        {
            ++out.untyped;
            continue;
        }
        if (! c.confirmed)
        {
            ++out.unconfirmed; // typed by the Inbox's suggestions and never looked at: not a person's judgement
            continue;
        }

        DatasetClip clip;
        clip.start = c.start;
        clip.end = c.end;
        clip.sourceFile = wav.getFileName();
        clip.sourceFrames = out.frames;
        clip.sourceSize = size;
        clip.name = c.name;
        clip.category = tags.category;
        clip.categoryJa = categoryJa;
        clip.type = c.typeName;
        clip.typeJa = names.forType (tags.category, c.typeName);
        clip.rating = c.rating;
        clip.ng = c.ng;
        clip.attributes = normaliseAttributes (c.attributes);
        out.clips.push_back (clip);
    }
    return WavTagResult::ok;
}

DatasetBatchResult addRawFolderToDataset (const juce::File& rawDir, const juce::File& folder, juce::AudioFormatManager& formats, const JapaneseNames& names,
                                          const ConditionOptions& conditioning, const juce::String& appVersion, const Dictionary* dictionary,
                                          const std::function<bool (int, int, const juce::String&)>& progress)
{
    DatasetBatchResult batch;

    auto files = rawDir.findChildFiles (juce::File::findFiles, true, "*.wav");
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b) { return a.getFullPathName().compareIgnoreCase (b.getFullPathName()) < 0; });
    batch.wavFiles = files.size();

    for (int i = 0; i < files.size(); ++i)
    {
        const auto& wav = files.getReference (i);
        if (progress && ! progress (i, files.size(), wav.getFileName()))
        {
            batch.cancelled = true;
            break;
        }

        WavClips found;
        const auto state = datasetClipsFromWav (wav, formats, names, found);
        if (state == WavTagResult::noTags)
        {
            ++batch.noTags;
            continue;
        }
        if (state == WavTagResult::unreadable)
        {
            ++batch.unreadable;
            batch.total.errors.add (wav.getFullPathName() + ": could not be read");
            continue;
        }

        batch.unconfirmed += found.unconfirmed;
        batch.untyped += found.untyped;
        if (found.clips.empty())
            continue;

        ++batch.withClips;
        const auto provider = [&] () -> AudioDataPtr {
            ++batch.audioRead;
            juce::String error;
            return loadAudioFile (formats, wav, error);
        };

        const auto r = addToDataset (provider, found.clips, folder, conditioning, appVersion, dictionary);
        batch.total.added += r.added;
        batch.total.updated += r.updated;
        batch.total.ngMarked += r.ngMarked;
        batch.total.unchanged += r.unchanged;
        batch.total.skippedNg += r.skippedNg;
        batch.total.errors.addArray (r.errors);
        batch.total.untranslated.addArray (r.untranslated);
    }

    batch.total.untranslated.removeDuplicates (true);
    return batch;
}
} // namespace wt
