#include "Exporter.h"

namespace wt
{
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

        const auto f = juce::jmin (fade, length / 2);
        if (f > 0)
        {
            segment.applyGainRamp (0, f, 0.0f, 1.0f);
            segment.applyGainRamp (length - f, f, 1.0f, 0.0f);
        }

        ConditionReport report;
        conditionCycle (segment, audio.sampleRate, conditioning, report);
        report.dcOffset = fileReport.dcOffset;
        report.humHz = fileReport.humHz;
        report.humNotches = fileReport.humNotches;
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
    }

    return result;
}
} // namespace wt
