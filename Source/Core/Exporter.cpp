#include "Exporter.h"

namespace wt
{
ExportResult exportCycles (const AudioData& audio, const std::vector<ExportItem>& items, const juce::File& root,
                           int bitsPerSample, float fadeMs)
{
    ExportResult result;
    juce::WavAudioFormat wav;
    const auto channels = audio.getNumChannels();
    const auto fade = (int) (fadeMs * 0.001 * audio.sampleRate);

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
            segment.copyFrom (ch, 0, audio.buffer, ch, (int) start, length);

        const auto f = juce::jmin (fade, length / 2);
        if (f > 0)
        {
            segment.applyGainRamp (0, f, 0.0f, 1.0f);
            segment.applyGainRamp (length - f, f, 1.0f, 0.0f);
        }

        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
        if (stream == nullptr)
        {
            result.errors.add (file.getFullPathName());
            continue;
        }

        std::unordered_map<juce::String, juce::String> metadata;
        metadata[juce::WavAudioFormat::riffInfoComment] = item.comment;

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
