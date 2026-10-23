#pragma once

#include "AudioData.h"
#include <juce_dsp/juce_dsp.h>
#include <juce_graphics/juce_graphics.h>

namespace wt
{
/**
    Renders only the visible range: one image column per screen pixel, computed on a worker thread.

    Columns are addressed by absolute index (sample / samplesPerPixel), so horizontal scrolling at the
    same zoom reuses already-computed columns and only the newly exposed ones are calculated.
    Memory use is independent of file length.
*/
class SpectrogramRenderer : private juce::Thread,
                            private juce::AsyncUpdater
{
public:
    static constexpr int fftOrder = 11;
    static constexpr int fftSize = 1 << fftOrder;

    struct Params
    {
        AudioDataPtr audio;
        juce::int64 firstColumn = 0;
        double samplesPerPixel = 1.0;
        int width = 0;
        int laneHeight = 0;
        float floorDb = -110.0f;
        double normLo = 0.0, normHi = 1.0; // visible part of the (mel-scaled) frequency axis
        int scheme = 0;

        bool sameLayout (const Params& o) const
        {
            return audio == o.audio && samplesPerPixel == o.samplesPerPixel && laneHeight == o.laneHeight
                && floorDb == o.floorDb && normLo == o.normLo && normHi == o.normHi && scheme == o.scheme;
        }
    };

    /** image: width x (laneHeight * numChannels), channels stacked vertically. */
    struct Result
    {
        Params params;
        juce::Image image;
        std::shared_ptr<const std::vector<uint8_t>> validColumns;
    };

    SpectrogramRenderer();
    ~SpectrogramRenderer() override;

    void request (const Params& params);
    Result getLatest() const;

    /** Called on the message thread whenever a (partial or complete) image is published. */
    std::function<void()> onNewImage;

    /** Vertical position (0 = bottom, 1 = top/Nyquist) of a frequency on the mel display scale. */
    static float freqToNorm (double freq, double sampleRate);
    static double normToFreq (double norm, double sampleRate);

private:
    void run() override;
    void handleAsyncUpdate() override;

    void render (const Params& params);
    void publish (const Params& params, const juce::Image& image, const std::vector<uint8_t>& valid);

    juce::CriticalSection lock;
    Params pending;
    bool hasPending = false;
    std::atomic<bool> newRequest { false };
    Result latest;
    juce::WaitableEvent wake;

    juce::dsp::FFT fft { fftOrder };
    std::vector<float> window, fftData, mags;
    float amplitudeScale = 1.0f;
    std::array<juce::PixelARGB, 256> lut;
    int lutScheme = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrogramRenderer)
};
} // namespace wt
