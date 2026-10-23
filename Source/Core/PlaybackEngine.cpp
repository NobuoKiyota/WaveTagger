#include "PlaybackEngine.h"

namespace wt
{
namespace
{
constexpr int fadeSamples = 96; // click-free start/stop
}

PlaybackEngine::PlaybackEngine()
{
    deviceManager.initialiseWithDefaultDevices (0, 2);
    player.setSource (this);
    deviceManager.addAudioCallback (&player);
}

PlaybackEngine::~PlaybackEngine()
{
    deviceManager.removeAudioCallback (&player);
    player.setSource (nullptr);
}

void PlaybackEngine::setAudio (AudioDataPtr audio)
{
    source.playing = false;
    {
        const juce::SpinLock::ScopedLockType sl (source.lock);
        source.audio = audio;
    }
    if (audio != nullptr)
        resampler.setResamplingRatio (audio->sampleRate / deviceRate.load());
}

void PlaybackEngine::play (juce::int64 start, juce::int64 end, bool loop)
{
    juce::int64 total = 0;
    {
        const juce::SpinLock::ScopedLockType sl (source.lock);
        if (source.audio == nullptr)
            return;
        total = source.audio->getNumSamples();
    }

    start = juce::jlimit<juce::int64> (0, total, start);
    end = end < 0 ? total : juce::jlimit<juce::int64> (start, total, end);
    if (end <= start)
        return;

    source.playing = false;
    resampler.flushBuffers();
    source.rangeStart = start;
    source.rangeEnd = end;
    source.position = start;
    source.loop = loop;
    source.stopRequested = false;
    source.justStarted = true;
    source.playing = true;
}

void PlaybackEngine::stop()
{
    if (deviceManager.getCurrentAudioDevice() == nullptr)
        source.playing = false;
    else
        source.stopRequested = true;
}

void PlaybackEngine::prepareToPlay (int samplesPerBlockExpected, double sampleRate)
{
    deviceRate = sampleRate;
    {
        const juce::SpinLock::ScopedLockType sl (source.lock);
        if (source.audio != nullptr)
            resampler.setResamplingRatio (source.audio->sampleRate / sampleRate);
    }
    resampler.prepareToPlay (samplesPerBlockExpected, sampleRate);
}

void PlaybackEngine::releaseResources()
{
    resampler.releaseResources();
}

void PlaybackEngine::getNextAudioBlock (const juce::AudioSourceChannelInfo& info)
{
    resampler.getNextAudioBlock (info);
}

void PlaybackEngine::BufferSource::getNextAudioBlock (const juce::AudioSourceChannelInfo& info)
{
    info.clearActiveBufferRegion();

    const juce::SpinLock::ScopedTryLockType sl (lock);
    if (! sl.isLocked() || audio == nullptr || ! playing)
        return;

    const auto& src = audio->buffer;
    const auto srcChannels = src.getNumChannels();
    const auto outChannels = info.buffer->getNumChannels();
    const auto n = info.numSamples;

    int written = 0;
    auto pos = position.load();

    while (written < n)
    {
        const auto avail = rangeEnd.load() - pos;
        if (avail <= 0)
        {
            if (loop && rangeEnd.load() > rangeStart.load())
            {
                pos = rangeStart;
                continue;
            }
            playing = false;
            break;
        }

        const auto count = (int) juce::jmin<juce::int64> (n - written, avail);
        for (int ch = 0; ch < outChannels; ++ch)
            info.buffer->copyFrom (ch, info.startSample + written, src, juce::jmin (ch, srcChannels - 1), (int) pos, count);

        pos += count;
        written += count;
    }

    position = pos;

    if (justStarted.exchange (false))
    {
        const auto len = juce::jmin (fadeSamples, n);
        info.buffer->applyGainRamp (info.startSample, len, 0.0f, 1.0f);
    }

    if (stopRequested.exchange (false))
    {
        const auto len = juce::jmin (fadeSamples, written);
        info.buffer->applyGainRamp (info.startSample, len, 1.0f, 0.0f);
        if (n > len)
            info.buffer->clear (info.startSample + len, n - len);
        playing = false;
    }
}
} // namespace wt
