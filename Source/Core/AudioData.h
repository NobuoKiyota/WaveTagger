#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

namespace wt
{
struct MinMax
{
    float lo = 0.0f, hi = 0.0f;
};

/** Multi-resolution min/max table so waveform drawing cost depends on pixel count, not file length. */
class PeakPyramid
{
public:
    static constexpr int baseBucket = 64;
    static constexpr int factor = 4;

    void build (const juce::AudioBuffer<float>& buffer);

    /** Min/max of [start, end) on a channel; uses the coarsest level that still resolves the range. */
    MinMax getRange (const juce::AudioBuffer<float>& buffer, int channel, juce::int64 start, juce::int64 end) const;

private:
    std::vector<std::vector<std::vector<MinMax>>> levels; // [level][channel][bucket]
};

/** Immutable once loaded; shared between UI, spectrogram worker and audio thread. */
struct AudioData
{
    juce::File file;
    double sampleRate = 48000.0;
    int bitsPerSample = 0;
    bool isFloat = false;
    juce::String formatName;
    juce::AudioBuffer<float> buffer;
    PeakPyramid peaks;

    int getNumChannels() const { return buffer.getNumChannels(); }
    juce::int64 getNumSamples() const { return buffer.getNumSamples(); }
    double getLengthSeconds() const { return (double) getNumSamples() / sampleRate; }
};

using AudioDataPtr = std::shared_ptr<const AudioData>;

/** Blocking; call from a worker thread. Returns nullptr and fills error on failure. */
AudioDataPtr loadAudioFile (juce::AudioFormatManager& formats, const juce::File& file, juce::String& error);
} // namespace wt
