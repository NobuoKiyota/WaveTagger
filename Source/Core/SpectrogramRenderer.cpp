#include "SpectrogramRenderer.h"
#include "UI/Theme.h"

namespace wt
{
namespace
{
double hzToMel (double f) { return 2595.0 * std::log10 (1.0 + f / 700.0); }
double melToHz (double m) { return 700.0 * (std::pow (10.0, m / 2595.0) - 1.0); }

struct RowMap
{
    int lo = 0, hi = 0; // bin range when a row spans several bins
    float centre = 0;   // fractional bin when a row is narrower than a bin
};
} // namespace

float SpectrogramRenderer::freqToNorm (double freq, double sampleRate)
{
    return (float) (hzToMel (freq) / hzToMel (sampleRate * 0.5));
}

double SpectrogramRenderer::normToFreq (double norm, double sampleRate)
{
    return melToHz (norm * hzToMel (sampleRate * 0.5));
}

SpectrogramRenderer::SpectrogramRenderer()
    : juce::Thread ("Spectrogram")
{
    window.resize (fftSize);
    double sum = 0.0;
    for (int i = 0; i < fftSize; ++i)
    {
        window[(size_t) i] = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (fftSize - 1)));
        sum += window[(size_t) i];
    }
    amplitudeScale = (float) (2.0 / sum);

    fftData.resize ((size_t) fftSize * 2);
    mags.resize ((size_t) fftSize / 2 + 1);
    lut = theme::makeSpectrogramLut();

    startThread();
}

SpectrogramRenderer::~SpectrogramRenderer()
{
    cancelPendingUpdate();
    signalThreadShouldExit();
    wake.signal();
    stopThread (3000);
}

void SpectrogramRenderer::request (const Params& params)
{
    {
        const juce::ScopedLock sl (lock);
        pending = params;
        hasPending = true;
    }
    newRequest = true;
    wake.signal();
}

SpectrogramRenderer::Result SpectrogramRenderer::getLatest() const
{
    const juce::ScopedLock sl (lock);
    return latest;
}

void SpectrogramRenderer::run()
{
    while (! threadShouldExit())
    {
        wake.wait (200);

        Params p;
        {
            const juce::ScopedLock sl (lock);
            if (! hasPending)
                continue;
            p = pending;
            hasPending = false;
            newRequest = false;
        }

        render (p);
    }
}

void SpectrogramRenderer::handleAsyncUpdate()
{
    if (onNewImage)
        onNewImage();
}

void SpectrogramRenderer::publish (const Params& params, const juce::Image& image, const std::vector<uint8_t>& valid)
{
    Result r { params, image.createCopy(), std::make_shared<const std::vector<uint8_t>> (valid) };
    {
        const juce::ScopedLock sl (lock);
        latest = std::move (r);
    }
    triggerAsyncUpdate();
}

void SpectrogramRenderer::render (const Params& p)
{
    if (p.audio == nullptr || p.width <= 0 || p.laneHeight <= 0)
        return;

    const auto& buffer = p.audio->buffer;
    const auto numChannels = buffer.getNumChannels();
    const auto numSamples = (juce::int64) buffer.getNumSamples();
    const auto height = p.laneHeight * numChannels;
    const auto width = p.width;

    juce::Image image (juce::Image::RGB, width, height, true, juce::SoftwareImageType());
    std::vector<uint8_t> valid ((size_t) width, 0);

    juce::Image::BitmapData dst (image, juce::Image::BitmapData::readWrite);

    // Reuse overlapping columns from the previous result (scrolling at the same zoom).
    {
        const auto prev = getLatest();
        if (prev.image.isValid() && prev.validColumns != nullptr && prev.params.sameLayout (p)
            && prev.image.getHeight() == height && prev.image.getFormat() == image.getFormat())
        {
            const juce::Image::BitmapData src (prev.image, juce::Image::BitmapData::readOnly);
            const auto& prevValid = *prev.validColumns;

            for (int x = 0; x < width; ++x)
            {
                const auto px = p.firstColumn + x - prev.params.firstColumn;
                if (px >= 0 && px < prev.params.width && prevValid[(size_t) px] != 0)
                    valid[(size_t) x] = 1;
            }

            for (int y = 0; y < height; ++y)
                for (int x = 0; x < width; ++x)
                    if (valid[(size_t) x] != 0)
                        std::memcpy (dst.getPixelPointer (x, y),
                                     src.getPixelPointer ((int) (p.firstColumn + x - prev.params.firstColumn), y),
                                     (size_t) dst.pixelStride);
        }
    }

    // Row → FFT bin mapping on a mel scale.
    const auto sampleRate = p.audio->sampleRate;
    const auto binHz = sampleRate / fftSize;
    const auto maxBin = fftSize / 2;
    std::vector<RowMap> rows ((size_t) p.laneHeight);
    for (int y = 0; y < p.laneHeight; ++y)
    {
        const auto fTop = normToFreq (1.0 - (double) y / p.laneHeight, sampleRate);
        const auto fBot = normToFreq (1.0 - (double) (y + 1) / p.laneHeight, sampleRate);
        auto& r = rows[(size_t) y];
        r.lo = juce::jlimit (0, maxBin, (int) std::floor (fBot / binHz));
        r.hi = juce::jlimit (0, maxBin, (int) std::ceil (fTop / binHz));
        r.centre = (float) juce::jlimit (0.0, (double) maxBin, 0.5 * (fTop + fBot) / binHz);
    }

    const auto spp = p.samplesPerPixel;
    const auto framesPerColumn = juce::jlimit (1, 4, (int) std::ceil (spp / (fftSize / 4)));
    const auto dbRange = -p.floorDb;
    auto lastPublish = juce::Time::getMillisecondCounter();

    for (int x = 0; x < width; ++x)
    {
        if (valid[(size_t) x] != 0)
            continue;

        if (newRequest || threadShouldExit())
        {
            publish (p, image, valid);
            return;
        }

        const auto columnStart = (double) (p.firstColumn + x) * spp;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            std::fill (mags.begin(), mags.end(), 0.0f);
            const auto* samples = buffer.getReadPointer (ch);

            for (int f = 0; f < framesPerColumn; ++f)
            {
                const auto centre = columnStart + (f + 0.5) * spp / framesPerColumn;
                const auto start = (juce::int64) std::floor (centre) - fftSize / 2;
                const auto a = juce::jmax<juce::int64> (start, 0);
                const auto b = juce::jmin<juce::int64> (start + fftSize, numSamples);
                if (b <= a)
                    continue;

                std::fill (fftData.begin(), fftData.end(), 0.0f);
                for (auto i = a; i < b; ++i)
                    fftData[(size_t) (i - start)] = samples[i] * window[(size_t) (i - start)];

                fft.performFrequencyOnlyForwardTransform (fftData.data(), true);

                for (int k = 0; k <= maxBin; ++k)
                    mags[(size_t) k] = juce::jmax (mags[(size_t) k], fftData[(size_t) k]);
            }

            for (int y = 0; y < p.laneHeight; ++y)
            {
                const auto& r = rows[(size_t) y];
                float v;
                if (r.hi - r.lo > 1)
                {
                    v = 0.0f;
                    for (int k = r.lo; k <= r.hi; ++k)
                        v = juce::jmax (v, mags[(size_t) k]);
                }
                else
                {
                    const auto k0 = juce::jmin ((int) r.centre, maxBin - 1);
                    const auto t = r.centre - (float) k0;
                    v = mags[(size_t) k0] * (1.0f - t) + mags[(size_t) k0 + 1] * t;
                }

                const auto db = 20.0f * std::log10 (v * amplitudeScale + 1.0e-10f);
                const auto idx = juce::jlimit (0, 255, (int) ((db - p.floorDb) / dbRange * 255.0f));
                reinterpret_cast<juce::PixelRGB*> (dst.getPixelPointer (x, ch * p.laneHeight + y))->set (lut[(size_t) idx]);
            }
        }

        valid[(size_t) x] = 1;

        const auto now = juce::Time::getMillisecondCounter();
        if (now - lastPublish > 40)
        {
            publish (p, image, valid);
            lastPublish = now;
        }
    }

    publish (p, image, valid);
}
} // namespace wt
