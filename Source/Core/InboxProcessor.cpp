#include "InboxProcessor.h"
#include "AudioData.h"
#include "CategoryProfiles.h"
#include "AudioFeatures.h"
#include "EmbeddedState.h"
#include "Persistence.h"

namespace wt
{
namespace
{
const char* const audioPatterns = "*.wav;*.flac;*.aif;*.aiff;*.mp3;*.ogg";

bool isSkippedName (const juce::String& name)
{
    return name.startsWithChar ('~') || name.startsWithChar ('.') || name.endsWithIgnoreCase (".tmp") || name.endsWithIgnoreCase (".part~")
           || name.endsWithIgnoreCase (".wtmp~");
}

bool inSkippedFolder (const juce::File& file, const juce::File& inbox)
{
    auto rel = file.getRelativePathFrom (inbox).replaceCharacter ('\\', '/');
    for (const auto& part : juce::StringArray::fromTokens (rel, "/", ""))
        if (part.equalsIgnoreCase ("_Done") || part.equalsIgnoreCase ("_Failed") || (part.startsWithChar ('.') && part != "." && part != ".."))
            return true;
    return false;
}

juce::File uniqueFile (const juce::File& wanted)
{
    auto f = wanted;
    for (int i = 1; f.exists(); ++i)
        f = wanted.getSiblingFile (wanted.getFileNameWithoutExtension() + "_" + juce::String (i) + wanted.getFileExtension());
    return f;
}

/** Writes any readable format out as a 24-bit WAV (same rate and channels). */
bool convertToWav (juce::AudioFormatManager& formats, const juce::File& source, const juce::File& target, juce::String& error)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (source));
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->lengthInSamples > std::numeric_limits<int>::max())
    {
        error = "cannot decode " + source.getFileName();
        return false;
    }

    juce::AudioBuffer<float> buffer ((int) reader->numChannels, (int) reader->lengthInSamples);
    if (! reader->read (&buffer, 0, buffer.getNumSamples(), 0, true, true))
    {
        error = "read error in " + source.getFileName();
        return false;
    }

    std::unique_ptr<juce::OutputStream> stream (target.createOutputStream());
    juce::WavAudioFormat wav;
    auto writer = stream != nullptr ? wav.createWriterFor (stream, juce::AudioFormatWriterOptions {}
                                                                        .withSampleRate (reader->sampleRate)
                                                                        .withNumChannels ((int) reader->numChannels)
                                                                        .withBitsPerSample (24))
                                    : nullptr;
    if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples()))
    {
        error = "cannot write " + target.getFileName();
        return false;
    }
    return true;
}
} // namespace

//==============================================================================
juce::String InboxScanState::signature (const juce::File& f)
{
    return f.getFullPathName().toLowerCase() + "|" + juce::String (f.getSize()) + "|" + juce::String (f.getLastModificationTime().toMilliseconds());
}

std::vector<juce::File> listInboxCandidates (const juce::File& inbox)
{
    std::vector<juce::File> out;
    if (! inbox.isDirectory())
        return out;

    for (const auto& item : juce::RangedDirectoryIterator (inbox, true, audioPatterns, juce::File::findFiles))
    {
        const auto f = item.getFile();
        if (! isSkippedName (f.getFileName()) && ! inSkippedFolder (f, inbox))
            out.push_back (f);
    }
    return out;
}

std::vector<juce::File> scanForReady (const juce::File& inbox, InboxScanState& state, int requiredStable)
{
    std::vector<juce::File> ready;
    std::set<juce::String> present;

    for (const auto& f : listInboxCandidates (inbox))
    {
        const auto key = f.getFullPathName();
        present.insert (key);

        const auto size = f.getSize();
        const auto mtime = f.getLastModificationTime().toMilliseconds();
        auto& seen = state.seen[key];

        if (seen.stable > 0 && seen.size == size && seen.mtime == mtime)
            ++seen.stable;
        else
            seen = { size, mtime, 1 };

        if (seen.stable < requiredStable || size <= 44 || state.ignored.count (InboxScanState::signature (f)) != 0)
            continue;

        juce::FileInputStream probe (f); // still being written / locked by the sync client?
        if (probe.openedOk())
            ready.push_back (f);
    }

    for (auto it = state.seen.begin(); it != state.seen.end();)
        it = present.count (it->first) != 0 ? std::next (it) : state.seen.erase (it);

    return ready;
}

//==============================================================================
InboxOutcome processInboxFile (const juce::File& file, const InboxOptions& options, juce::AudioFormatManager& formats)
{
    InboxOutcome out;
    out.original = file;

    if (! file.existsAsFile())
    {
        out.error = "file disappeared";
        return out;
    }
    if (options.raw.getFullPathName().isEmpty())
    {
        out.error = "no Raw folder is set";
        return out;
    }

    // --- folder names are labels: <Category>/<Type>/file.wav
    juce::StringArray folders = juce::StringArray::fromTokens (file.getParentDirectory().getRelativePathFrom (options.inbox).replaceCharacter ('\\', '/'), "/", "");
    folders.removeEmptyStrings();
    folders.removeString (".");

    juce::String category;
    if (! folders.isEmpty())
        for (const auto& name : options.categoryNames)
            if (name.equalsIgnoreCase (folders[0]))
                category = name;

    out.categoryHint = category;
    if (category.isNotEmpty() && folders.size() >= 2)
        out.typeHint = folders[1];
    else if (category.isEmpty() && folders.size() == 1)
        out.typeHint = folders[0]; // a single folder that is not a Category is taken as the Type

    // --- 1. copy (or convert) into Raw/<year>/, verified before anything else happens
    const auto year = juce::String (juce::Time::getCurrentTime().getYear());
    const auto rawFolder = options.raw.getChildFile (year);
    if (! rawFolder.createDirectory().wasOk())
    {
        out.error = "cannot create " + rawFolder.getFullPathName();
        return out;
    }

    const auto dest = uniqueFile (rawFolder.getChildFile (file.getFileNameWithoutExtension() + ".wav"));
    const auto part = dest.getSiblingFile (dest.getFileName() + ".part~");
    part.deleteFile();

    juce::String error;
    if (file.hasFileExtension ("wav"))
    {
        if (! file.copyFileTo (part) || ! part.hasIdenticalContentTo (file))
        {
            part.deleteFile();
            out.error = "copy to Raw failed or did not verify";
            return out;
        }
    }
    else if (! convertToWav (formats, file, part, error))
    {
        part.deleteFile();
        out.error = error;
        return out;
    }

    if (! part.moveFileTo (dest))
    {
        part.deleteFile();
        out.error = "cannot finish " + dest.getFullPathName();
        return out;
    }

    // --- 2. split + embed
    juce::String loadError;
    const auto audio = loadAudioFile (formats, dest, loadError);
    if (audio == nullptr)
    {
        dest.deleteFile();
        out.error = loadError.isNotEmpty() ? loadError : "cannot read the copy";
        return out;
    }

    CycleModel model;
    model.setPrefix (category.isNotEmpty() ? CategoryProfiles::defaultPrefix (category) : juce::String ("Rec"));
    model.setCategory (category);
    if (out.typeHint.isNotEmpty())
        model.setTypeName (0, out.typeHint);

    const auto segments = detectSegments (*audio, 0, audio->getNumSamples(), options.split);
    out.cycles = (int) segments.size();

    // a Type slot for a name: reuse, or take the first free one (the palette has nine)
    const auto slotFor = [&model] (const juce::String& name) {
        for (int i = 0; i < CycleModel::numSlots; ++i)
            if (model.getType (i).name.equalsIgnoreCase (name))
                return i;
        for (int i = 0; i < CycleModel::numSlots; ++i)
            if (model.getType (i).name.isEmpty())
            {
                model.setTypeName (i, name);
                return i;
            }
        return -1;
    };

    float minConfidence = 2.0f;
    for (size_t i = 0; i < segments.size(); ++i)
    {
        const auto& [start, end] = segments[i];
        const auto fromFolder = out.typeHint.isNotEmpty(); // a person put the file in a Type folder: that is a decision
        int slot = fromFolder ? 0 : -1;

        Suggestion sug;
        FeatureVector v {};
        if (options.suggester != nullptr && options.suggester->isReady() && computeSegmentFeatures (audio->buffer, audio->sampleRate, start, end, v))
            sug = options.suggester->suggest (v, category);

        const auto idx = model.add (start, end, slot);
        if (sug.valid())
        {
            minConfidence = juce::jmin (minConfidence, sug.confidence);
            const auto apply = ! fromFolder && sug.confidence >= options.autoThreshold;
            const auto autoSlot = apply ? slotFor (sug.type) : -1;
            model.setSuggestion (idx, sug.type, sug.confidence, autoSlot, apply && autoSlot >= 0);
            if (apply && autoSlot >= 0)
            {
                slot = autoSlot;
                ++out.autoTyped;
            }
        }

        if (slot < 0)
            ++out.uncertain;
    }
    out.minConfidence = minConfidence <= 1.0f ? minConfidence : -1.0f;

    const auto result = embedMarkers (dest, dest, buildEmbedRequest (model, audio->sampleRate, options.appVersion));
    if (! result.ok)
    {
        dest.deleteFile(); // the original is still in the inbox, so it will simply be retried
        out.error = result.error;
        return out;
    }
    out.rawFile = dest;

    // --- 3. only now move the original out of the inbox
    auto relative = file.getParentDirectory().getRelativePathFrom (options.inbox).replaceCharacter ('\\', '/');
    auto doneFolder = options.inbox.getChildFile ("_Done");
    if (relative.isNotEmpty() && relative != ".")
        doneFolder = doneFolder.getChildFile (relative);

    const auto archive = uniqueFile (doneFolder.getChildFile (file.getFileName()));
    if (doneFolder.createDirectory().wasOk() && file.moveFileTo (archive))
        out.archived = archive;
    else
        out.archiveNote = "the original could not be moved to _Done and is still in the inbox";

    out.ok = true;
    return out;
}

//==============================================================================
InboxWatcher::InboxWatcher (juce::AudioFormatManager& f)
    : juce::Thread ("InboxWatcher"), formats (f)
{
    startThread (juce::Thread::Priority::background);
}

InboxWatcher::~InboxWatcher()
{
    *alive = false;
    signalThreadShouldExit();
    wake.signal();
    stopThread (30000); // an import in progress is allowed to finish
}

void InboxWatcher::saveIgnored() const
{
    if (config.stateFile.getFullPathName().isEmpty())
        return;

    juce::Array<juce::var> arr;
    for (const auto& sig : state.ignored)
        arr.add (sig);
    writeJson (config.stateFile, arr);
}

void InboxWatcher::loadIgnored()
{
    if (config.stateFile.getFullPathName().isEmpty())
        return;

    const auto json = readJson (config.stateFile); // keep the parsed value alive while its array is used
    if (const auto* arr = json.getArray())
        for (const auto& v : *arr)
            state.ignored.insert (v.toString());
}

void InboxWatcher::configure (const Config& c)
{
    {
        const juce::ScopedLock sl (lock);
        const auto stateFileChanged = c.stateFile != config.stateFile;
        config = c;
        status = c.enabled ? "watching" : "stopped";
        if (stateFileChanged)
            loadIgnored();
    }
    enabledFlag = c.enabled;
    wake.signal();
}

int InboxWatcher::ignoreExisting()
{
    juce::File inbox;
    {
        const juce::ScopedLock sl (lock);
        inbox = config.inbox;
    }

    int n = 0;
    for (const auto& f : listInboxCandidates (inbox))
    {
        const juce::ScopedLock sl (lock);
        state.ignored.insert (InboxScanState::signature (f));
        ++n;
    }

    const juce::ScopedLock sl (lock);
    saveIgnored();
    return n;
}

void InboxWatcher::forgetFailure (const juce::File& f)
{
    const juce::ScopedLock sl (lock);
    state.ignored.erase (InboxScanState::signature (f));
    state.seen.erase (f.getFullPathName());
    saveIgnored();
}

void InboxWatcher::scanNow()
{
    wake.signal();
}

juce::String InboxWatcher::getStatus() const
{
    const juce::ScopedLock sl (lock);
    return status;
}

void InboxWatcher::run()
{
    while (! threadShouldExit())
    {
        wake.wait (2000);
        if (threadShouldExit())
            break;

        Config cfg;
        std::vector<juce::File> ready;
        {
            const juce::ScopedLock sl (lock);
            cfg = config;
            if (! cfg.enabled || ! cfg.inbox.isDirectory())
            {
                status = cfg.enabled ? "inbox folder not found" : "stopped";
                continue;
            }
            ready = scanForReady (cfg.inbox, state, stableScans);
            status = ready.empty() ? "watching" : "importing";
        }

        for (const auto& file : ready)
        {
            if (threadShouldExit())
                break;

            {
                const juce::ScopedLock sl (lock);
                status = "importing " + file.getFileName();
            }

            InboxOptions options;
            options.inbox = cfg.inbox;
            options.raw = cfg.raw;
            options.categoryNames = cfg.categoryNames;
            options.suggester = cfg.suggester;

            const auto outcome = processInboxFile (file, options, formats);
            {
                const juce::ScopedLock sl (lock);
                if (! outcome.ok || outcome.archived == juce::File())
                {
                    state.ignored.insert (InboxScanState::signature (file)); // do not retry in a loop; the user can ask again
                    saveIgnored();
                }
                status = "watching";
            }

            juce::MessageManager::callAsync ([this, outcome, stillAlive = alive] {
                if (*stillAlive && onOutcome)
                    onOutcome (outcome);
            });
        }
    }
}
} // namespace wt
