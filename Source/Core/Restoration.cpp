#include "Restoration.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>

namespace wt
{
namespace
{
//==============================================================================
/** Short-time Fourier transform with sqrt-Hann windows (analysis and synthesis) at 75 % overlap: overlap-add of the
    products (Hann) sums to 2, so the synthesis is scaled by 0.5 and a gain of 1 everywhere gives the input back.
    Frame k starts at -(n - hop) + k * hop, so every sample is covered by four frames; outside the signal is zero. */
class Stft
{
public:
    explicit Stft (int fftOrder) : fft (fftOrder), n (1 << fftOrder), window ((size_t) n)
    {
        for (int i = 0; i < n; ++i)
            window[(size_t) i] = (float) std::sqrt (0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * i / n));
    }

    int size() const { return n; }
    int hop() const { return n / 4; }
    int bins() const { return n / 2 + 1; }
    int frameStart (int k) const { return -(n - hop()) + k * hop(); }
    int frameCount (int total) const { return (total - 1 + n - hop()) / hop() + 1; }
    float hann (int i) const { return window[(size_t) i] * window[(size_t) i]; }

    /** Windowed spectrum of frame k: bins() complex pairs (re, im) in data[0 .. 2 * bins() - 1]. */
    void analyse (const float* x, int total, int k, std::vector<float>& data) const
    {
        data.assign ((size_t) n * 2, 0.0f);
        const auto s = frameStart (k);
        const auto lo = juce::jmax (0, -s), hi = juce::jmin (n, total - s);
        for (int i = lo; i < hi; ++i)
            data[(size_t) i] = x[s + i] * window[(size_t) i];
        fft.performRealOnlyForwardTransform (data.data(), true);
    }

    /** Inverse transform of (a possibly modified) spectrum, windowed and added into y. */
    void synthesise (std::vector<float>& data, int total, int k, float* y) const
    {
        fft.performRealOnlyInverseTransform (data.data());
        const auto s = frameStart (k);
        const auto lo = juce::jmax (0, -s), hi = juce::jmin (n, total - s);
        for (int i = lo; i < hi; ++i)
            y[s + i] += data[(size_t) i] * window[(size_t) i] * 0.5f;
    }

private:
    juce::dsp::FFT fft;
    int n;
    std::vector<float> window;
};

int stftOrder (double sampleRate) { return sampleRate > 64000.0 ? 12 : 11; }

double toDbPower (double power) { return power > 1.0e-30 ? 10.0 * std::log10 (power) : -300.0; }

//==============================================================================
// De-click helpers

/** Prediction-error filter a[0..p] (a[0] = 1) from the autocorrelation r[0..p] (Levinson-Durbin). */
bool levinson (const std::vector<double>& r, int p, std::vector<double>& a)
{
    a.assign ((size_t) p + 1, 0.0);
    a[0] = 1.0;
    double err = r[0];
    if (err <= 1.0e-18)
        return false;

    std::vector<double> previous;
    for (int i = 1; i <= p; ++i)
    {
        double acc = r[(size_t) i];
        for (int j = 1; j < i; ++j)
            acc += a[(size_t) j] * r[(size_t) (i - j)];
        const auto k = -acc / err;
        previous = a;
        for (int j = 1; j < i; ++j)
            a[(size_t) j] = previous[(size_t) j] + k * previous[(size_t) (i - j)];
        a[(size_t) i] = k;
        err *= 1.0 - k * k;
        if (err <= 1.0e-18)
            break;
    }
    return true;
}

/** Solves the symmetric positive definite system m * u = rhs (size n) by Cholesky; false if it is not positive definite. */
bool solveSpd (std::vector<double> m, std::vector<double> rhs, int n, std::vector<double>& u)
{
    for (int i = 0; i < n; ++i)
    {
        for (int j = 0; j <= i; ++j)
        {
            double s = m[(size_t) (i * n + j)];
            for (int k = 0; k < j; ++k)
                s -= m[(size_t) (i * n + k)] * m[(size_t) (j * n + k)];
            if (i == j)
            {
                if (s <= 1.0e-18)
                    return false;
                m[(size_t) (i * n + i)] = std::sqrt (s);
            }
            else
                m[(size_t) (i * n + j)] = s / m[(size_t) (j * n + j)];
        }
    }
    u.assign ((size_t) n, 0.0);
    for (int i = 0; i < n; ++i)
    {
        double s = rhs[(size_t) i];
        for (int k = 0; k < i; ++k)
            s -= m[(size_t) (i * n + k)] * u[(size_t) k];
        u[(size_t) i] = s / m[(size_t) (i * n + i)];
    }
    for (int i = n - 1; i >= 0; --i)
    {
        double s = u[(size_t) i];
        for (int k = i + 1; k < n; ++k)
            s -= m[(size_t) (k * n + i)] * u[(size_t) k];
        u[(size_t) i] = s / m[(size_t) (i * n + i)];
    }
    return true;
}

/** Replaces x[first..last] by the least-squares estimate that makes the AR prediction error over the gap (and the p
    samples after it) as small as possible. Needs p samples of known signal on both sides. */
bool interpolateAr (float* x, int first, int last, const std::vector<double>& a)
{
    const auto m = last - first + 1;
    const auto p = (int) a.size() - 1;
    std::vector<double> normal ((size_t) m * (size_t) m, 0.0), rhs ((size_t) m, 0.0), coef ((size_t) m, 0.0);

    for (int n = first; n <= last + p; ++n)
    {
        std::fill (coef.begin(), coef.end(), 0.0);
        double known = 0.0;
        int firstJ = m, lastJ = -1;
        for (int k = 0; k <= p; ++k)
        {
            const auto i = n - k;
            if (i >= first && i <= last)
            {
                coef[(size_t) (i - first)] += a[(size_t) k];
                firstJ = juce::jmin (firstJ, i - first);
                lastJ = juce::jmax (lastJ, i - first);
            }
            else
                known += a[(size_t) k] * x[i];
        }
        for (int j1 = firstJ; j1 <= lastJ; ++j1)
        {
            rhs[(size_t) j1] -= known * coef[(size_t) j1];
            for (int j2 = firstJ; j2 <= lastJ; ++j2)
                normal[(size_t) (j1 * m + j2)] += coef[(size_t) j1] * coef[(size_t) j2];
        }
    }

    double trace = 0.0;
    for (int i = 0; i < m; ++i)
        trace += normal[(size_t) (i * m + i)];
    for (int i = 0; i < m; ++i)
        normal[(size_t) (i * m + i)] += 1.0e-9 * trace + 1.0e-12;

    std::vector<double> u;
    if (! solveSpd (std::move (normal), std::move (rhs), m, u))
        return false;
    for (int i = 0; i < m; ++i)
        x[first + i] = (float) u[(size_t) i];
    return true;
}

double medianOf (std::vector<float>& v)
{
    if (v.empty())
        return 0.0;
    std::nth_element (v.begin(), v.begin() + (std::ptrdiff_t) v.size() / 2, v.end());
    return v[v.size() / 2];
}
} // namespace

//==============================================================================
DeclipResult declip (juce::AudioBuffer<float>& buffer)
{
    DeclipResult result;
    const auto total = buffer.getNumSamples();
    struct Run
    {
        int a, b;
        bool positive;
    };

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* x = buffer.getWritePointer (ch);
        float peak = 0.0f;
        for (int i = 0; i < total; ++i)
            peak = juce::jmax (peak, std::abs (x[i]));
        if (peak < 0.1f)
            continue;

        const auto level = peak * 0.99999f; // clipped samples are all exactly the same value
        std::vector<Run> runs;
        for (int i = 0; i < total;)
        {
            if (std::abs (x[i]) < level)
            {
                ++i;
                continue;
            }
            const auto positive = x[i] > 0.0f;
            int j = i;
            while (j + 1 < total && std::abs (x[j + 1]) >= level && (x[j + 1] > 0.0f) == positive)
                ++j;
            const auto length = j - i + 1;
            if (length >= 3 && length <= 64 && i >= 2 && j + 2 < total)
                runs.push_back ({ i, j, positive });
            i = j + 1;
        }

        if (runs.size() < 3)
            continue; // one or two flat tops are just a loud sound

        for (const auto& run : runs)
        {
            const auto p0 = (double) x[run.a - 1], p1 = (double) x[run.b + 1];
            const auto m0 = (double) x[run.a - 1] - x[run.a - 2], m1 = (double) x[run.b + 2] - x[run.b + 1];
            const auto span = (double) (run.b + 1 - (run.a - 1));
            for (int i = run.a; i <= run.b; ++i)
            {
                const auto t = (i - (run.a - 1)) / span;
                const auto t2 = t * t, t3 = t2 * t;
                auto v = (2 * t3 - 3 * t2 + 1) * p0 + (t3 - 2 * t2 + t) * span * m0 + (-2 * t3 + 3 * t2) * p1 + (t3 - t2) * span * m1;
                v = run.positive ? juce::jmax (v, (double) peak) : juce::jmin (v, -(double) peak);
                x[i] = (float) v;
            }
        }
        result.runs += (int) runs.size();
    }

    if (result.runs > 0)
    {
        float peak = 0.0f;
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            for (int i = 0; i < total; ++i)
                peak = juce::jmax (peak, std::abs (buffer.getSample (ch, i)));
        if (peak > 1.0f)
        {
            const auto gain = 0.9999f / peak;
            buffer.applyGain (gain);
            result.gainDb = 20.0 * std::log10 ((double) gain);
        }
    }
    return result;
}

//==============================================================================
DeclickResult declick (juce::AudioBuffer<float>& buffer, double sampleRate, Strength strength)
{
    DeclickResult result;
    const auto total = buffer.getNumSamples();
    constexpr int order = 16, context = 128, block = 1024;
    if (total < 4 * block)
        return result;

    const auto k = strength == Strength::low ? 12.0 : strength == Strength::medium ? 8.0 : 5.0;
    const auto maxShare = strength == Strength::low ? 0.003 : strength == Strength::medium ? 0.005 : 0.015;
    const auto maxLength = juce::jmax (8, (int) (0.002 * sampleRate));
    const auto window = juce::jmax (16, (int) (0.004 * sampleRate));
    constexpr float minimum = 0.004f; // a spike of -60 dBFS leaves about this in the second difference

    struct Region
    {
        int lo, hi;
    };
    std::vector<std::vector<Region>> found ((size_t) buffer.getNumChannels());
    std::vector<float> e ((size_t) total, 0.0f), scratch;
    long long repaired = 0;

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const auto* x = buffer.getReadPointer (ch);
        for (int n = 2; n < total; ++n)
            e[(size_t) n] = std::abs (x[n] - 2.0f * x[n - 1] + x[n - 2]);

        std::vector<float> threshold ((size_t) (total / block + 1));
        for (size_t b = 0; b < threshold.size(); ++b)
        {
            const auto from = (int) b * block, to = juce::jmin (total, from + block);
            scratch.assign (e.begin() + from, e.begin() + to);
            threshold[b] = (float) juce::jmax ((double) minimum, k * 1.4826 * medianOf (scratch));
        }

        const auto median = [&] (int from, int to) {
            from = juce::jmax (2, from);
            to = juce::jmin (total, to);
            if (to <= from)
                return 0.0;
            scratch.assign (e.begin() + from, e.begin() + to);
            return medianOf (scratch);
        };

        for (int n = 2; n < total;)
        {
            if (e[(size_t) n] <= threshold[(size_t) (n / block)])
            {
                ++n;
                continue;
            }

            int a = n, b = n;
            for (int m = n + 1; m < total && m <= b + 3; ++m)
                if (e[(size_t) m] > threshold[(size_t) (m / block)])
                    b = m;
            n = b + 1;

            const auto lo = a - 3, hi = b + 2;
            if (lo < order + 4 || hi + order + 4 >= total || hi - lo + 1 > maxLength)
                continue;

            // a real attack is followed by a louder signal; a spike leaves the signal after it as it was before
            if (median (hi + 1, hi + 1 + window) > 3.0 * median (lo - window, lo) + 0.002)
                continue;

            auto& list = found[(size_t) ch];
            if (! list.empty() && lo <= list.back().hi + 1)
                list.back().hi = hi;
            else
                list.push_back ({ lo, hi });
        }

        for (const auto& r : found[(size_t) ch])
            repaired += r.hi - r.lo + 1;
    }

    if ((double) repaired > maxShare * total * buffer.getNumChannels())
    {
        result.tooMany = repaired > 0;
        return result;
    }

    std::vector<double> r ((size_t) order + 1), a;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* x = buffer.getWritePointer (ch);
        for (const auto& region : found[(size_t) ch])
        {
            const auto before = juce::jmin (context, region.lo), after = juce::jmin (context, total - 1 - region.hi);
            if (before < order + 4 || after < order + 4)
                continue;

            std::fill (r.begin(), r.end(), 0.0);
            for (int lag = 0; lag <= order; ++lag)
            {
                for (int i = region.lo - before; i + lag < region.lo; ++i)
                    r[(size_t) lag] += (double) x[i] * x[i + lag];
                for (int i = region.hi + 1; i + lag <= region.hi + after; ++i)
                    r[(size_t) lag] += (double) x[i] * x[i + lag];
            }
            r[0] = r[0] * 1.0001 + 1.0e-12;

            const auto repairedOk = levinson (r, order, a) && interpolateAr (x, region.lo, region.hi, a);
            if (! repairedOk)
            {
                // flat signal or an ill-conditioned fit: a straight line is still better than a spike
                const auto left = (double) x[region.lo - 1], right = (double) x[region.hi + 1];
                const auto span = (double) (region.hi - region.lo + 2);
                for (int i = region.lo; i <= region.hi; ++i)
                    x[i] = (float) (left + (right - left) * (i - region.lo + 1) / span);
            }
            ++result.regions;
        }
    }
    return result;
}

//==============================================================================
DewindResult dewind (juce::AudioBuffer<float>& buffer, double sampleRate, Strength strength)
{
    DewindResult result;
    const auto channels = buffer.getNumChannels();
    const auto total = buffer.getNumSamples();
    if (channels == 0 || total < (int) sampleRate)
        return result;

    const Stft stft (stftOrder (sampleRate));
    const auto n = stft.size(), bins = stft.bins(), frames = stft.frameCount (total);
    const auto binHz = sampleRate / n;
    const auto lowFrom = juce::jmax (1, (int) std::ceil (20.0 / binHz)), lowTo = juce::jmax (lowFrom, (int) std::floor (200.0 / binHz));

    // pass 1: low-end power per frame
    std::vector<float> lowDb ((size_t) frames);
    std::vector<float> data;
    for (int f = 0; f < frames; ++f)
    {
        double power = 0.0;
        for (int ch = 0; ch < channels; ++ch)
        {
            stft.analyse (buffer.getReadPointer (ch), total, f, data);
            for (int b = lowFrom; b <= lowTo; ++b)
                power += (double) data[(size_t) b * 2] * data[(size_t) b * 2] + (double) data[(size_t) b * 2 + 1] * data[(size_t) b * 2 + 1];
        }
        lowDb[(size_t) f] = (float) toDbPower (power);
    }

    auto sorted = lowDb;
    std::nth_element (sorted.begin(), sorted.begin() + (std::ptrdiff_t) sorted.size() / 2, sorted.end());
    const auto baseline = (double) sorted[sorted.size() / 2];

    // wind = low end well above the recording's own usual level, for at least 0.3 s: the running minimum over that span
    const auto span = juce::jmax (3, (int) std::round (0.3 * sampleRate / stft.hop()));
    std::vector<float> sustained ((size_t) frames, 0.0f);
    for (int f = 0; f < frames; ++f)
    {
        const auto from = juce::jmax (0, f - span / 2), to = juce::jmin (frames - 1, f + span / 2);
        if (to - from + 1 < span)
            continue; // the very ends: no 0.3 s to look at
        double lowest = 1.0e9;
        for (int g = from; g <= to; ++g)
            lowest = juce::jmin (lowest, (double) lowDb[(size_t) g] - baseline);
        sustained[(size_t) f] = (float) lowest;
    }

    const auto rise = strength == Strength::low ? 9.0 : strength == Strength::medium ? 6.0 : 4.0; // dB above usual before it counts
    std::vector<float> amount ((size_t) frames, 0.0f);
    bool any = false;
    for (int f = 0; f < frames; ++f)
    {
        amount[(size_t) f] = (float) juce::jlimit (0.0, 1.0, ((double) sustained[(size_t) f] - rise) / 18.0);
        any = any || amount[(size_t) f] > 0.0f;
    }
    if (! any)
        return result;

    // soften the on/off edges (about 0.1 s each way)
    const auto soft = juce::jmax (1, (int) std::round (0.1 * sampleRate / stft.hop()));
    std::vector<float> smooth ((size_t) frames, 0.0f);
    for (int f = 0; f < frames; ++f)
    {
        double s = 0.0;
        int count = 0;
        for (int g = juce::jmax (0, f - soft); g <= juce::jmin (frames - 1, f + soft); ++g, ++count)
            s += amount[(size_t) g];
        smooth[(size_t) f] = (float) (s / count);
    }

    const auto maxCut = strength == Strength::low ? 150.0 : strength == Strength::medium ? 220.0 : 300.0;
    const auto maxAtten = strength == Strength::low ? 12.0 : strength == Strength::medium ? 18.0 : 24.0;
    const auto floorGain = std::pow (10.0, -maxAtten / 20.0);

    // pass 2: a high-pass shaped gain whose corner moves with the amount of wind
    juce::AudioBuffer<float> out (channels, total);
    out.clear();
    std::vector<std::vector<float>> spectra ((size_t) channels);
    for (int f = 0; f < frames; ++f)
    {
        for (int ch = 0; ch < channels; ++ch)
            stft.analyse (buffer.getReadPointer (ch), total, f, spectra[(size_t) ch]);

        const auto s = (double) smooth[(size_t) f];
        if (s > 0.0)
        {
            const auto cutoff = 80.0 + (maxCut - 80.0) * s;
            result.maxCutoffHz = juce::jmax (result.maxCutoffHz, cutoff);
            ++result.frames;
            for (int b = 0; b < bins; ++b)
            {
                const auto hz = b * binHz;
                if (hz >= cutoff)
                    break;
                const auto raw = juce::jmax (floorGain, (hz / cutoff) * (hz / cutoff));
                const auto g = (float) (1.0 - s * (1.0 - raw));
                for (int ch = 0; ch < channels; ++ch)
                {
                    spectra[(size_t) ch][(size_t) b * 2] *= g;
                    spectra[(size_t) ch][(size_t) b * 2 + 1] *= g;
                }
            }
        }
        for (int ch = 0; ch < channels; ++ch)
            stft.synthesise (spectra[(size_t) ch], total, f, out.getWritePointer (ch));
    }

    for (int ch = 0; ch < channels; ++ch)
        buffer.copyFrom (ch, 0, out, ch, 0, total);
    return result;
}

//==============================================================================
DenoiseResult denoise (juce::AudioBuffer<float>& buffer, double sampleRate, Strength strength)
{
    DenoiseResult result;
    const auto channels = buffer.getNumChannels();
    const auto total = buffer.getNumSamples();
    if (channels == 0 || total < (int) sampleRate)
        return result; // tooShort

    const Stft stft (stftOrder (sampleRate));
    const auto n = stft.size(), hop = stft.hop(), bins = stft.bins(), frames = stft.frameCount (total);

    // frames that lie completely inside the recording: their energy tells quiet from loud
    struct Frame
    {
        int index;
        double energy;
    };
    std::vector<Frame> inside;
    for (int f = 0; f < frames; ++f)
    {
        const auto s = stft.frameStart (f);
        if (s < 0 || s + n > total)
            continue;
        double e = 0.0;
        for (int ch = 0; ch < channels; ++ch)
        {
            const auto* x = buffer.getReadPointer (ch) + s;
            for (int i = 0; i < n; ++i)
                e += (double) x[i] * x[i] * stft.hann (i);
        }
        inside.push_back ({ f, e });
    }
    if (inside.size() < 8)
        return result; // tooShort

    std::vector<double> energies;
    for (const auto& fr : inside)
        energies.push_back (fr.energy);
    std::sort (energies.begin(), energies.end());
    const auto quietEnergy = energies[energies.size() / 10];
    const auto loudEnergy = energies[juce::jmin (energies.size() - 1, (size_t) (0.995 * (double) energies.size()))];

    std::vector<int> quiet;
    for (const auto& fr : inside)
        if (fr.energy <= quietEnergy)
            quiet.push_back (fr.index);
    if (quiet.size() > 4000) // a long recording: a spread-out sample of the quiet frames is enough
    {
        std::vector<int> thinned;
        const auto stride = (double) quiet.size() / 4000.0;
        for (int i = 0; i < 4000; ++i)
            thinned.push_back (quiet[(size_t) (i * stride)]);
        quiet = std::move (thinned);
    }

    double quietSum = 0.0;
    for (const auto& fr : inside)
        if (fr.energy <= quietEnergy)
            quietSum += fr.energy;
    long long quietCount = 0;
    for (const auto& fr : inside)
        quietCount += fr.energy <= quietEnergy ? 1 : 0;
    result.floorDb = toDbPower (quietSum / (double) quietCount / (channels * (n / 2.0)));

    if (result.floorDb < -100.0 || toDbPower (loudEnergy) - toDbPower (quietEnergy) < 6.0)
    {
        result.outcome = DenoiseOutcome::noFloor;
        return result;
    }

    // the noise: mean power per bin over the quiet frames
    std::vector<double> noise ((size_t) bins, 0.0);
    std::vector<float> data;
    for (const auto f : quiet)
        for (int ch = 0; ch < channels; ++ch)
        {
            stft.analyse (buffer.getReadPointer (ch), total, f, data);
            for (int b = 0; b < bins; ++b)
                noise[(size_t) b] += (double) data[(size_t) b * 2] * data[(size_t) b * 2] + (double) data[(size_t) b * 2 + 1] * data[(size_t) b * 2 + 1];
        }
    for (auto& v : noise)
        v = juce::jmax (1.0e-20, v / ((double) quiet.size() * channels));

    const auto over = strength == Strength::low ? 1.3 : strength == Strength::medium ? 1.8 : 2.4;
    const auto reduction = strength == Strength::low ? 6.0 : strength == Strength::medium ? 12.0 : 18.0;
    const auto floorGain = (float) std::pow (10.0, -reduction / 20.0);
    const auto release = (float) std::exp (-(double) hop / (0.04 * sampleRate)); // the gain closes over about 40 ms, opens at once

    juce::AudioBuffer<float> out (channels, total);
    out.clear();
    std::vector<std::vector<float>> spectra ((size_t) channels);
    std::vector<float> power ((size_t) bins), smoothed ((size_t) bins), previous ((size_t) bins, 1.0f), gain ((size_t) bins);
    double reductionSum = 0.0;
    long long reductionCount = 0;

    for (int f = 0; f < frames; ++f)
    {
        for (int ch = 0; ch < channels; ++ch)
            stft.analyse (buffer.getReadPointer (ch), total, f, spectra[(size_t) ch]);

        for (int b = 0; b < bins; ++b)
        {
            double p = 0.0;
            for (int ch = 0; ch < channels; ++ch)
                p += (double) spectra[(size_t) ch][(size_t) b * 2] * spectra[(size_t) ch][(size_t) b * 2]
                     + (double) spectra[(size_t) ch][(size_t) b * 2 + 1] * spectra[(size_t) ch][(size_t) b * 2 + 1];
            power[(size_t) b] = (float) (p / channels);
        }
        for (int b = 0; b < bins; ++b) // a few bins wide: single bins of noise jump around, which is what makes "musical noise"
        {
            double s = 0.0;
            int count = 0;
            for (int c = juce::jmax (0, b - 2); c <= juce::jmin (bins - 1, b + 2); ++c, ++count)
                s += power[(size_t) c];
            smoothed[(size_t) b] = (float) (s / count);
        }

        for (int b = 0; b < bins; ++b)
        {
            const auto estimate = (float) (over * noise[(size_t) b]);
            auto g = smoothed[(size_t) b] > estimate ? std::sqrt (1.0f - estimate / smoothed[(size_t) b]) : 0.0f;
            g = juce::jmax (g, floorGain);
            if (g < previous[(size_t) b])
                g = release * previous[(size_t) b] + (1.0f - release) * g;
            previous[(size_t) b] = g;
            gain[(size_t) b] = g;
        }

        if (f % 16 == 0)
            for (int b = 0; b < bins; ++b)
            {
                reductionSum += 20.0 * std::log10 ((double) gain[(size_t) b]);
                ++reductionCount;
            }

        for (int ch = 0; ch < channels; ++ch)
        {
            for (int b = 0; b < bins; ++b)
            {
                spectra[(size_t) ch][(size_t) b * 2] *= gain[(size_t) b];
                spectra[(size_t) ch][(size_t) b * 2 + 1] *= gain[(size_t) b];
            }
            stft.synthesise (spectra[(size_t) ch], total, f, out.getWritePointer (ch));
        }
    }

    for (int ch = 0; ch < channels; ++ch)
        buffer.copyFrom (ch, 0, out, ch, 0, total);

    result.outcome = DenoiseOutcome::applied;
    result.averageReductionDb = reductionCount > 0 ? reductionSum / (double) reductionCount : 0.0;
    return result;
}
} // namespace wt
