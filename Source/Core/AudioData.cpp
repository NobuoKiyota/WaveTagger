#include "AudioData.h"

namespace wt
{
void PeakPyramid::build (const juce::AudioBuffer<float>& buffer)
{
    levels.clear();

    const auto numChannels = buffer.getNumChannels();
    const auto numSamples = buffer.getNumSamples();
    if (numChannels == 0 || numSamples == 0)
        return;

    const auto baseCount = (size_t) ((numSamples + baseBucket - 1) / baseBucket);
    std::vector<std::vector<MinMax>> base ((size_t) numChannels, std::vector<MinMax> (baseCount));

    for (int ch = 0; ch < numChannels; ++ch)
    {
        const auto* data = buffer.getReadPointer (ch);
        for (size_t i = 0; i < baseCount; ++i)
        {
            const auto start = (int) i * baseBucket;
            const auto len = juce::jmin (baseBucket, numSamples - start);
            const auto r = juce::FloatVectorOperations::findMinAndMax (data + start, len);
            base[(size_t) ch][i] = { r.getStart(), r.getEnd() };
        }
    }

    levels.push_back (std::move (base));

    while (levels.back()[0].size() > 1)
    {
        const auto& prev = levels.back();
        const auto prevCount = prev[0].size();
        const auto count = (prevCount + factor - 1) / factor;
        std::vector<std::vector<MinMax>> next ((size_t) numChannels, std::vector<MinMax> (count));

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const auto& src = prev[(size_t) ch];
            auto& dst = next[(size_t) ch];
            for (size_t i = 0; i < count; ++i)
            {
                MinMax m = src[i * factor];
                for (size_t j = i * factor + 1; j < juce::jmin (prevCount, (i + 1) * factor); ++j)
                {
                    m.lo = juce::jmin (m.lo, src[j].lo);
                    m.hi = juce::jmax (m.hi, src[j].hi);
                }
                dst[i] = m;
            }
        }

        levels.push_back (std::move (next));
    }
}

MinMax PeakPyramid::getRange (const juce::AudioBuffer<float>& buffer, int channel, juce::int64 start, juce::int64 end) const
{
    start = juce::jmax<juce::int64> (0, start);
    end = juce::jmin<juce::int64> (buffer.getNumSamples(), end);
    if (end <= start)
        return {};

    const auto len = end - start;

    if (levels.empty() || len < (juce::int64) baseBucket * 8)
    {
        const auto r = juce::FloatVectorOperations::findMinAndMax (buffer.getReadPointer (channel, (int) start), (int) len);
        return { r.getStart(), r.getEnd() };
    }

    size_t level = 0;
    juce::int64 bucket = baseBucket;
    while (level + 1 < levels.size() && bucket * factor * 8 <= len)
    {
        ++level;
        bucket *= factor;
    }

    const auto& data = levels[level][(size_t) channel];
    const auto i0 = (size_t) (start / bucket);
    const auto i1 = juce::jmin ((size_t) ((end + bucket - 1) / bucket), data.size());

    MinMax r = data[i0];
    for (auto i = i0 + 1; i < i1; ++i)
    {
        r.lo = juce::jmin (r.lo, data[i].lo);
        r.hi = juce::jmax (r.hi, data[i].hi);
    }
    return r;
}

AudioDataPtr loadAudioFile (juce::AudioFormatManager& formats, const juce::File& file, juce::String& error)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
    {
        error = "Unsupported or unreadable file: " + file.getFileName();
        return {};
    }

    if (reader->numChannels == 0 || reader->lengthInSamples <= 0
        || reader->lengthInSamples > std::numeric_limits<int>::max())
    {
        error = "Invalid length or channel count: " + file.getFileName();
        return {};
    }

    auto data = std::make_shared<AudioData>();
    data->file = file;
    data->sampleRate = reader->sampleRate;
    data->bitsPerSample = (int) reader->bitsPerSample;
    data->isFloat = reader->usesFloatingPointData;
    data->formatName = reader->getFormatName();

    const auto numSamples = (int) reader->lengthInSamples;
    data->buffer.setSize ((int) reader->numChannels, numSamples);
    if (! reader->read (&data->buffer, 0, numSamples, 0, true, true))
    {
        error = "Failed to read samples: " + file.getFileName();
        return {};
    }

    data->peaks.build (data->buffer);
    return data;
}
} // namespace wt
