#pragma once

#include "AudioData.h"
#include <juce_audio_devices/juce_audio_devices.h>

namespace wt
{
/** Plays a range of the in-memory buffer; resamples to the device rate. */
class PlaybackEngine : private juce::AudioSource
{
public:
    PlaybackEngine();
    ~PlaybackEngine() override;

    void setAudio (AudioDataPtr audio);

    /** end < 0 plays to the end of the file. */
    void play (juce::int64 start, juce::int64 end, bool loop);
    void stop();
    void setLooping (bool shouldLoop) { source.loop = shouldLoop; }

    bool isPlaying() const { return source.playing; }
    juce::int64 getPosition() const { return source.position; }

    juce::AudioDeviceManager& getDeviceManager() { return deviceManager; }

private:
    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
    void releaseResources() override;
    void getNextAudioBlock (const juce::AudioSourceChannelInfo& info) override;

    struct BufferSource : public juce::AudioSource
    {
        void prepareToPlay (int, double) override {}
        void releaseResources() override {}
        void getNextAudioBlock (const juce::AudioSourceChannelInfo& info) override;

        juce::SpinLock lock;
        AudioDataPtr audio;
        std::atomic<juce::int64> position { 0 }, rangeStart { 0 }, rangeEnd { 0 };
        std::atomic<bool> playing { false }, loop { false }, stopRequested { false }, justStarted { false };
    };

    juce::AudioDeviceManager deviceManager;
    juce::AudioSourcePlayer player;
    BufferSource source;
    juce::ResamplingAudioSource resampler { &source, false, 2 };
    std::atomic<double> deviceRate { 48000.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlaybackEngine)
};
} // namespace wt
