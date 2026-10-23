#include "AudioFeatures.h"
#include <juce_dsp/juce_dsp.h>

namespace wt
{
namespace
{
constexpr int fftOrder = 10;
constexpr int fftSize = 1 << fftOrder;
constexpr int hop = fftSize / 2;
constexpr int numBands = 20;

double hzToMel (double f) { return 2595.0 * std::log10 (1.0 + f / 700.0); }
double melToHz (double m) { return 700.0 * (std::pow (10.0, m / 2595.0) - 1.0); }

float toDb (double power) { return (float) (10.0 * std::log10 (power + 1.0e-12)); }
} // namespace

bool computeFeaturesFromMono (const std::vector<float>& mono, double sampleRate, double totalSeconds, FeatureVector& out)
{
    const auto n = (int) mono.size();
    if (n < 64)
        return false;

    // ---- level, crest, zero crossings
    double sumSq = 0.0;
    float peak = 0.0f;
    int zeroCrossings = 0;
    for (int i = 0; i < n; ++i)
    {
        sumSq += (double) mono[(size_t) i] * mono[(size_t) i];
        peak = juce::jmax (peak, std::abs (mono[(size_t) i]));
        if (i > 0 && (mono[(size_t) i] >= 0.0f) != (mono[(size_t) i - 1] >= 0.0f))
            ++zeroCrossings;
    }
    const auto rmsDb = toDb (sumSq / n);
    const auto peakDb = toDb ((double) peak * peak);

    // ---- 5 ms envelope: attack and decay
    const auto win = juce::jmax (1, (int) (0.005 * sampleRate));
    const auto numWin = juce::jmax (1, n / win);
    std::vector<float> env ((size_t) numWin);
    for (int w = 0; w < numWin; ++w)
    {
        double e = 0.0;
        const auto begin = w * win;
        const auto count = juce::jmin (win, n - begin);
        for (int i = 0; i < count; ++i)
            e += (double) mono[(size_t) (begin + i)] * mono[(size_t) (begin + i)];
        env[(size_t) w] = (float) std::sqrt (e / juce::jmax (1, count));
    }

    const auto envPeak = *std::max_element (env.begin(), env.end());
    int peakWin = 0, t10 = 0, t90 = 0;
    for (int w = 0; w < numWin; ++w)
        if (env[(size_t) w] >= envPeak)
        {
            peakWin = w;
            break;
        }
    for (int w = 0; w <= peakWin; ++w)
    {
        if (t10 == 0 && env[(size_t) w] >= 0.1f * envPeak)
            t10 = w;
        if (env[(size_t) w] >= 0.9f * envPeak)
        {
            t90 = w;
            break;
        }
    }
    const auto attackMs = juce::jmax (0.0, (double) (t90 - t10) * 5.0);

    int lastActive = numWin - 1;
    while (lastActive > peakWin && env[(size_t) lastActive] < 0.03f * envPeak)
        --lastActive;
    const auto tailStart = peakWin + juce::jmax (1, (lastActive - peakWin) * 3 / 4);
    double tail = 0.0;
    int tailCount = 0;
    for (int w = juce::jmin (tailStart, numWin - 1); w <= lastActive; ++w, ++tailCount)
        tail += (double) env[(size_t) w] * env[(size_t) w];
    const auto decayDb = juce::jlimit (-90.0f, 0.0f, toDb (tail / juce::jmax (1, tailCount)) - toDb ((double) envPeak * envPeak));

    // ---- spectrum frames
    juce::dsp::FFT fft (fftOrder);
    std::vector<float> window ((size_t) fftSize), frame ((size_t) fftSize * 2);
    for (int i = 0; i < fftSize; ++i)
        window[(size_t) i] = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (fftSize - 1)));

    const auto nyquist = sampleRate * 0.5;
    const auto fHigh = juce::jmin (16000.0, nyquist * 0.98);
    std::array<int, numBands + 1> edge {};
    for (int b = 0; b <= numBands; ++b)
    {
        const auto f = melToHz (hzToMel (50.0) + (hzToMel (fHigh) - hzToMel (50.0)) * b / numBands);
        edge[(size_t) b] = juce::jlimit (1, fftSize / 2 - 1, (int) std::round (f / sampleRate * fftSize));
        if (b > 0 && edge[(size_t) b] <= edge[(size_t) b - 1])
            edge[(size_t) b] = juce::jmin (fftSize / 2 - 1, edge[(size_t) b - 1] + 1);
    }

    struct Frame
    {
        std::array<float, numBands> bandDb {};
        float totalDb = 0.0f, centroid = 0.0f, rolloff = 0.0f;
    };
    std::vector<Frame> frames;

    for (int start = 0; start == 0 || start + fftSize <= n; start += hop)
    {
        std::fill (frame.begin(), frame.end(), 0.0f);
        for (int i = 0; i < fftSize && start + i < n; ++i)
            frame[(size_t) i] = mono[(size_t) (start + i)] * window[(size_t) i];
        fft.performFrequencyOnlyForwardTransform (frame.data(), true);

        Frame fr;
        double total = 0.0, weighted = 0.0;
        for (int k = 1; k < fftSize / 2; ++k)
        {
            const double p = (double) frame[(size_t) k] * frame[(size_t) k];
            total += p;
            weighted += p * ((double) k * sampleRate / fftSize);
        }
        fr.totalDb = toDb (total);
        fr.centroid = total > 0.0 ? (float) (weighted / total / nyquist) : 0.0f;

        double cumulative = 0.0;
        fr.rolloff = 1.0f;
        for (int k = 1; k < fftSize / 2; ++k)
        {
            cumulative += (double) frame[(size_t) k] * frame[(size_t) k];
            if (cumulative >= 0.85 * total)
            {
                fr.rolloff = (float) ((double) k * sampleRate / fftSize / nyquist);
                break;
            }
        }

        float meanDb = 0.0f;
        for (int b = 0; b < numBands; ++b)
        {
            double e = 0.0;
            for (int k = edge[(size_t) b]; k <= edge[(size_t) b + 1]; ++k)
                e += (double) frame[(size_t) k] * frame[(size_t) k];
            fr.bandDb[(size_t) b] = toDb (e);
            meanDb += fr.bandDb[(size_t) b];
        }
        meanDb /= numBands;
        for (auto& v : fr.bandDb)
            v -= meanDb; // spectral SHAPE only; the level is described by the separate RMS feature

        frames.push_back (fr);
    }

    float loudest = -200.0f;
    for (const auto& f : frames)
        loudest = juce::jmax (loudest, f.totalDb);

    std::vector<const Frame*> active;
    for (const auto& f : frames)
        if (f.totalDb >= loudest - 30.0f)
            active.push_back (&f);
    if (active.empty())
        active.push_back (&frames.front());

    std::array<double, numBands> mean {}, spread {};
    double centroid = 0.0, rolloff = 0.0;
    for (const auto* f : active)
    {
        for (int b = 0; b < numBands; ++b)
            mean[(size_t) b] += f->bandDb[(size_t) b];
        centroid += f->centroid;
        rolloff += f->rolloff;
    }
    const auto count = (double) active.size();
    for (auto& m : mean)
        m /= count;
    for (const auto* f : active)
        for (int b = 0; b < numBands; ++b)
            spread[(size_t) b] += std::pow (f->bandDb[(size_t) b] - mean[(size_t) b], 2.0);

    int i = 0;
    for (int b = 0; b < numBands; ++b)
        out[(size_t) i++] = (float) mean[(size_t) b];
    for (int b = 0; b < numBands; ++b)
        out[(size_t) i++] = (float) std::sqrt (spread[(size_t) b] / count);
    out[(size_t) i++] = (float) (centroid / count);
    out[(size_t) i++] = (float) (rolloff / count);
    out[(size_t) i++] = (float) zeroCrossings / (float) n;
    out[(size_t) i++] = rmsDb;
    out[(size_t) i++] = peakDb - rmsDb;
    out[(size_t) i++] = (float) std::log10 (totalSeconds + 0.01);
    out[(size_t) i++] = (float) std::log10 (attackMs + 1.0);
    out[(size_t) i++] = decayDb;

    for (auto& v : out)
        if (! std::isfinite (v))
            v = 0.0f;
    return true;
}

bool computeFeatures (juce::AudioFormatManager& formats, const juce::File& file, FeatureVector& out, double maxSeconds, juce::String* error)
{
    const auto fail = [&] (const juce::String& why) {
        if (error != nullptr)
            *error = why;
        return false;
    };

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->numChannels == 0)
        return fail ("unreadable");

    const auto sampleRate = reader->sampleRate;
    const auto totalSeconds = (double) reader->lengthInSamples / sampleRate;
    const auto n = (int) juce::jmin<juce::int64> (reader->lengthInSamples, (juce::int64) (maxSeconds * sampleRate));
    if (n < 64)
        return fail ("too short");

    juce::AudioBuffer<float> buffer ((int) reader->numChannels, n);
    if (! reader->read (&buffer, 0, n, 0, true, true))
        return fail ("read error");

    std::vector<float> mono ((size_t) n, 0.0f);
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        juce::FloatVectorOperations::add (mono.data(), buffer.getReadPointer (ch), n);
    juce::FloatVectorOperations::multiply (mono.data(), 1.0f / (float) buffer.getNumChannels(), n);

    return computeFeaturesFromMono (mono, sampleRate, totalSeconds, out) ? true : fail ("too short");
}

bool computeSegmentFeatures (const juce::AudioBuffer<float>& buffer, double sampleRate, juce::int64 start, juce::int64 end, FeatureVector& out, double maxSeconds)
{
    start = juce::jlimit<juce::int64> (0, buffer.getNumSamples(), start);
    end = juce::jlimit<juce::int64> (start, buffer.getNumSamples(), end);
    const auto total = (double) (end - start) / sampleRate;
    const auto n = (int) juce::jmin<juce::int64> (end - start, (juce::int64) (maxSeconds * sampleRate));
    if (n < 64 || buffer.getNumChannels() == 0)
        return false;

    std::vector<float> mono ((size_t) n, 0.0f);
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        juce::FloatVectorOperations::add (mono.data(), buffer.getReadPointer (ch) + start, n);
    juce::FloatVectorOperations::multiply (mono.data(), 1.0f / (float) buffer.getNumChannels(), n);
    return computeFeaturesFromMono (mono, sampleRate, total, out);
}

} // namespace wt
