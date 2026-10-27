#include "Exporter.h"

namespace wt
{
juce::String exportSubFolder (const juce::String& category, const juce::String& prefix, const juce::String& typeName, bool typeFolders)
{
    juce::StringArray parts;
    const auto cat = juce::File::createLegalFileName (category.trim());
    const auto pre = juce::File::createLegalFileName (prefix.trim());
    if (cat.isNotEmpty())
        parts.add (cat);
    if (pre.isNotEmpty() && ! pre.equalsIgnoreCase (cat))
        parts.add (pre);
    if (typeFolders && typeName.isNotEmpty())
        parts.add (juce::File::createLegalFileName (typeName.trim()));
    return parts.joinIntoString ("/");
}

int renumberForDestination (std::vector<ExportItem>& items, const juce::File& root,
                            const std::function<juce::String (int, int)>& nameFor)
{
    std::map<juce::String, int> lastInGroup; // folder + type -> the last number given in this export
    std::set<juce::String> taken;            // folder/name pairs given in this export
    int changed = 0;

    for (auto& item : items)
    {
        if (item.type < 0 || item.number <= 0)
            continue;

        const auto dir = item.subFolder.isEmpty() ? root : root.getChildFile (item.subFolder);
        const auto group = item.subFolder + "|" + juce::String (item.type);
        auto n = item.number;
        if (const auto it = lastInGroup.find (group); it != lastInGroup.end() && n <= it->second)
            n = it->second + 1; // the numbers of one export keep their order

        for (;; ++n)
        {
            const auto name = nameFor (item.type, n);
            if (! dir.getChildFile (name + ".wav").exists() && taken.count (item.subFolder + "/" + name.toLowerCase()) == 0)
            {
                if (n != item.number)
                {
                    item.comment = item.comment.replace ("no=" + juce::String (item.number) + ";", "no=" + juce::String (n) + ";");
                    item.number = n;
                    ++changed;
                }
                item.fileName = name;
                taken.insert (item.subFolder + "/" + name.toLowerCase());
                break;
            }
        }
        lastInGroup[group] = n;
    }
    return changed;
}

ExportResult exportCycles (const AudioData& audio, const std::vector<ExportItem>& items, const juce::File& root,
                           int bitsPerSample, float fadeMs, const ConditionOptions& conditioning)
{
    ExportResult result;
    juce::WavAudioFormat wav;
    const auto channels = audio.getNumChannels();
    const auto fade = (int) (fadeMs * 0.001 * audio.sampleRate);

    // Stage A runs once over the whole recording (filters need the run-up), on a copy
    juce::AudioBuffer<float> filtered;
    const auto* source = &audio.buffer;
    ConditionReport fileReport;
    if (conditioning.needsFile())
    {
        filtered.makeCopyOf (audio.buffer);
        conditionFile (filtered, audio.sampleRate, conditioning, fileReport);
        source = &filtered;
        result.humHz = fileReport.humHz;
        result.humNotches = fileReport.humNotches;
        result.panApplied = fileReport.panApplied;
        result.panDb = fileReport.panDb;
        result.fileReport = fileReport;
    }

    bool firstGain = true;
    juce::int64 ditherSeed = 1;

    for (const auto& item : items)
    {
        const auto dir = item.subFolder.isEmpty() ? root : root.getChildFile (item.subFolder);
        const auto file = dir.getChildFile (item.fileName + ".wav");

        if (file.exists())
        {
            result.skippedExisting.add (file.getFullPathName());
            continue;
        }

        const auto start = juce::jlimit<juce::int64> (0, audio.getNumSamples(), item.start);
        const auto end = juce::jlimit<juce::int64> (start, audio.getNumSamples(), item.end);
        const auto length = (int) (end - start);
        if (length <= 0 || ! dir.createDirectory().wasOk())
        {
            result.errors.add (file.getFullPathName());
            continue;
        }

        juce::AudioBuffer<float> segment (channels, length);
        for (int ch = 0; ch < channels; ++ch)
            segment.copyFrom (ch, 0, *source, ch, (int) start, length);

        applyEdgeFade (segment, fade);

        ConditionReport report;
        conditionCycle (segment, audio.sampleRate, conditioning, report);
        report.dcOffset = fileReport.dcOffset;
        report.humHz = fileReport.humHz;
        report.humNotches = fileReport.humNotches;
        report.panApplied = fileReport.panApplied;
        report.panDb = fileReport.panDb;
        report.declipRuns = fileReport.declipRuns;
        report.declipGainDb = fileReport.declipGainDb;
        report.clicksFixed = fileReport.clicksFixed;
        report.clicksTooMany = fileReport.clicksTooMany;
        report.windFrames = fileReport.windFrames;
        report.windMaxCutoffHz = fileReport.windMaxCutoffHz;
        report.denoiseOutcome = fileReport.denoiseOutcome;
        report.noiseFloorDb = fileReport.noiseFloorDb;
        report.denoiseAvgDb = fileReport.denoiseAvgDb;
        if (conditioning.normalize != NormalizeMode::off)
        {
            result.minGainDb = firstGain ? report.gainDb : juce::jmin (result.minGainDb, report.gainDb);
            result.maxGainDb = firstGain ? report.gainDb : juce::jmax (result.maxGainDb, report.gainDb);
            result.gainLimited += report.gainLimited ? 1 : 0;
            firstGain = false;
        }

        if (conditioning.dither && conditioning.active() && bitsPerSample == 16)
            addTpdfDither (segment, 16, ditherSeed++);

        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
        if (stream == nullptr)
        {
            result.errors.add (file.getFullPathName());
            continue;
        }

        auto comment = item.comment;
        const auto chain = describeConditioning (conditioning, report);
        if (chain.isNotEmpty())
            comment += "; " + chain;

        std::unordered_map<juce::String, juce::String> metadata;
        metadata[juce::WavAudioFormat::riffInfoComment] = comment;

        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions {}
                                                       .withSampleRate (audio.sampleRate)
                                                       .withNumChannels (channels)
                                                       .withBitsPerSample (bitsPerSample)
                                                       .withMetadataValues (metadata));
        if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (segment, 0, length))
        {
            writer.reset();
            file.deleteFile();
            result.errors.add (file.getFullPathName());
            continue;
        }

        writer.reset();
        ++result.written;
        result.writtenFiles.push_back ({ file, chain });
    }

    return result;
}
} // namespace wt
