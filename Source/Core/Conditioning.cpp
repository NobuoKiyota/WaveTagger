#include "Conditioning.h"

#include <juce_dsp/juce_dsp.h>

namespace wt
{
namespace
{
constexpr double silenceDb = -240.0;

double toDb (double linear) { return linear > 1.0e-12 ? 20.0 * std::log10 (linear) : silenceDb; }

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    double process (double x)
    {
        const auto y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    static Biquad highPass (double sampleRate, double freq, double q)
    {
        const auto w0 = juce::MathConstants<double>::twoPi * freq / sampleRate;
        const auto cosw = std::cos (w0);
        const auto alpha = std::sin (w0) / (2.0 * q);
        const auto a0 = 1.0 + alpha;

        Biquad f;
        f.b0 = (1.0 + cosw) * 0.5 / a0;
        f.b1 = -(1.0 + cosw) / a0;
        f.b2 = f.b0;
        f.a1 = -2.0 * cosw / a0;
        f.a2 = (1.0 - alpha) / a0;
        return f;
    }

    static Biquad notch (double sampleRate, double freq, double q)
    {
        const auto w0 = juce::MathConstants<double>::twoPi * freq / sampleRate;
        const auto cosw = std::cos (w0);
        const auto alpha = std::sin (w0) / (2.0 * q);
        const auto a0 = 1.0 + alpha;

        Biquad f;
        f.b0 = 1.0 / a0;
        f.b1 = -2.0 * cosw / a0;
        f.b2 = f.b0;
        f.a1 = f.b1;
        f.a2 = (1.0 - alpha) / a0;
        return f;
    }

    /** BS.1770 stage 1: high shelf (+4 dB above ~1.7 kHz), derived for any sample rate. */
    static Biquad kShelf (double sampleRate)
    {
        constexpr double gainDb = 3.999843853973347, f0 = 1681.974450955533, q = 0.7071752369554196;
        const auto k = std::tan (juce::MathConstants<double>::pi * f0 / sampleRate);
        const auto vh = std::pow (10.0, gainDb / 20.0);
        const auto vb = std::pow (vh, 0.4996667741545416);
        const auto a0 = 1.0 + k / q + k * k;

        Biquad f;
        f.b0 = (vh + vb * k / q + k * k) / a0;
        f.b1 = 2.0 * (k * k - vh) / a0;
        f.b2 = (vh - vb * k / q + k * k) / a0;
        f.a1 = 2.0 * (k * k - 1.0) / a0;
        f.a2 = (1.0 - k / q + k * k) / a0;
        return f;
    }

    /** BS.1770 stage 2: the "RLB" high-pass at ~38 Hz. */
    static Biquad kHighPass (double sampleRate)
    {
        constexpr double f0 = 38.13547087602444, q = 0.5003270373238773;
        const auto k = std::tan (juce::MathConstants<double>::pi * f0 / sampleRate);
        const auto a0 = 1.0 + k / q + k * k;

        Biquad f;
        f.b0 = 1.0;
        f.b1 = -2.0;
        f.b2 = 1.0;
        f.a1 = 2.0 * (k * k - 1.0) / a0;
        f.a2 = (1.0 - k / q + k * k) / a0;
        return f;
    }
};

void removeDc (juce::AudioBuffer<float>& buffer, ConditionReport& report)
{
    const auto n = buffer.getNumSamples();
    if (n == 0)
        return;

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* d = buffer.getWritePointer (ch);
        double sum = 0.0;
        for (int i = 0; i < n; ++i)
            sum += d[i];

        const auto mean = sum / n;
        report.dcOffset = juce::jmax (report.dcOffset, std::abs (mean));
        for (int i = 0; i < n; ++i)
            d[i] = (float) (d[i] - mean);
    }
}

/** Runs the cascade over every channel, in place. The filters first run on the first sample for `warmUp` samples so a
    recording that starts away from zero does not click. */
void runCascade (juce::AudioBuffer<float>& buffer, const std::vector<Biquad>& proto, int warmUp)
{
    const auto n = buffer.getNumSamples();
    if (n == 0 || proto.empty())
        return;

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* d = buffer.getWritePointer (ch);
        auto sections = proto;
        for (int i = 0; i < warmUp; ++i)
        {
            auto x = (double) d[0];
            for (auto& s : sections)
                x = s.process (x);
        }

        for (int i = 0; i < n; ++i)
        {
            auto x = (double) d[i];
            for (auto& s : sections)
                x = s.process (x);
            d[i] = (float) x;
        }
    }
}

void lowCut (juce::AudioBuffer<float>& buffer, double sampleRate, float cutoff, int order)
{
    const auto freq = juce::jlimit (1.0, sampleRate * 0.25, (double) cutoff);
    // Butterworth: one section at Q 1/sqrt(2), or two at these Qs for 4th order
    std::vector<Biquad> sections;
    if (order >= 4)
    {
        sections.push_back (Biquad::highPass (sampleRate, freq, 0.5411961001461970));
        sections.push_back (Biquad::highPass (sampleRate, freq, 1.3065629648763766));
    }
    else
    {
        sections.push_back (Biquad::highPass (sampleRate, freq, 0.7071067811865476));
    }

    runCascade (buffer, sections, (int) juce::jmin (sampleRate * 10.0, sampleRate * 8.0 / freq));
}

void notchHum (juce::AudioBuffer<float>& buffer, double sampleRate, const std::vector<double>& freqs, double q)
{
    std::vector<Biquad> sections;
    double lowest = freqs.empty() ? 50.0 : freqs.front();
    for (const auto f : freqs)
    {
        sections.push_back (Biquad::notch (sampleRate, f, q));
        lowest = juce::jmin (lowest, f);
    }

    // a notch rings for about tau = q / (pi f) seconds
    const auto tau = q / (juce::MathConstants<double>::pi * lowest);
    const auto warmUp = (int) juce::jmin (sampleRate * 10.0, sampleRate * 8.0 * tau);

    // The hum is already there at the first sample, so a filter that starts there needs a few tau to settle. The head is
    // therefore also run backwards in time (where sample 0 is the *end* of the run and is clean) and the two results are
    // cross-faded: the forward one takes over once its start-up has died away, the backward one has its own at the other end.
    const auto n = buffer.getNumSamples();
    const auto head = juce::jmin (n, (int) (sampleRate * 10.0 * tau));
    juce::AudioBuffer<float> backward (buffer.getNumChannels(), head);
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int i = 0; i < head; ++i)
            backward.setSample (ch, i, buffer.getSample (ch, head - 1 - i));

    runCascade (backward, sections, warmUp);
    runCascade (buffer, sections, warmUp);

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int i = 0; i < head; ++i)
        {
            const auto w = head > 1 ? 0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * i / (head - 1)) : 1.0;
            buffer.setSample (ch, i, (float) (w * buffer.getSample (ch, i) + (1.0 - w) * backward.getSample (ch, head - 1 - i)));
        }
}
} // namespace

HumDetection detectHum (const juce::AudioBuffer<float>& buffer, double sampleRate, const ConditionOptions& options)
{
    HumDetection result;
    if (options.humMode == HumMode::off || buffer.getNumChannels() == 0)
        return result;

    const auto total = buffer.getNumSamples();
    int order = 17; // 0.37 Hz per bin at 48 kHz
    while (order > 14 && (1 << order) > total)
        --order;

    const auto n = 1 << order;
    if (total < n)
        return result;

    const auto hop = n / 2;
    const auto channels = buffer.getNumChannels();
    const auto binHz = sampleRate / n;

    struct Frame
    {
        int start;
        double energy;
    };
    std::vector<Frame> frames;
    for (int s = 0; s + n <= total; s += hop)
    {
        double e = 0.0;
        for (int ch = 0; ch < channels; ++ch)
        {
            const auto* d = buffer.getReadPointer (ch) + s;
            for (int i = 0; i < n; ++i)
                e += (double) d[i] * d[i];
        }
        frames.push_back ({ s, e });
    }

    // The quietest fifth is where the noise floor (and so the hum) shows up undisturbed by the sound itself
    const auto used = juce::jmax<size_t> (1, frames.size() / 5);
    std::partial_sort (frames.begin(), frames.begin() + (std::ptrdiff_t) used, frames.end(),
                       [] (const Frame& a, const Frame& b) { return a.energy < b.energy; });

    juce::dsp::FFT fft (order);
    std::vector<float> window ((size_t) n), data ((size_t) n * 2);
    for (int i = 0; i < n; ++i)
        window[(size_t) i] = 0.5f - 0.5f * (float) std::cos (juce::MathConstants<double>::twoPi * i / n);

    const auto bins = n / 2 + 1;
    std::vector<double> power ((size_t) bins, 0.0);
    for (size_t f = 0; f < used; ++f)
        for (int ch = 0; ch < channels; ++ch)
        {
            const auto* d = buffer.getReadPointer (ch) + frames[f].start;
            std::fill (data.begin(), data.end(), 0.0f);
            for (int i = 0; i < n; ++i)
                data[(size_t) i] = d[i] * window[(size_t) i];
            fft.performRealOnlyForwardTransform (data.data(), true);
            for (int k = 0; k < bins; ++k)
                power[(size_t) k] += (double) data[(size_t) k * 2] * data[(size_t) k * 2] + (double) data[(size_t) k * 2 + 1] * data[(size_t) k * 2 + 1];
        }

    struct Harmonic
    {
        int number;
        double hz, prominenceDb;
        bool exclusive;
    };

    // Every harmonic of f0 that stands out of the spectrum around it; hum drifts a little, so the peak is measured
    const auto analyse = [&] (double f0, int sharedStep) {
        std::vector<Harmonic> found;
        const auto searchHz = juce::jmax (1.0, binHz * 1.5);
        for (int h = 1; h <= options.humHarmonics; ++h)
        {
            const auto expected = f0 * h;
            if (expected > sampleRate * 0.4)
                break;

            const auto lo = juce::jmax (3, (int) std::floor ((expected - searchHz) / binHz));
            const auto hi = juce::jmin (bins - 2, (int) std::ceil ((expected + searchHz) / binHz));
            if (hi <= lo)
                continue;

            int pk = lo;
            for (int k = lo; k <= hi; ++k)
                if (power[(size_t) k] > power[(size_t) pk])
                    pk = k;

            std::vector<double> around;
            for (int k = juce::jmax (1, pk - 40); k <= juce::jmin (bins - 1, pk + 40); ++k)
                if (std::abs (k - pk) >= 4)
                    around.push_back (power[(size_t) k]);
            if (around.size() < 8)
                continue;

            std::nth_element (around.begin(), around.begin() + (std::ptrdiff_t) around.size() / 2, around.end());
            const auto median = around[around.size() / 2];
            if (median <= 0.0 || power[(size_t) pk] <= 0.0)
                continue;

            const auto prominence = 10.0 * std::log10 (power[(size_t) pk] / median);
            if (prominence < options.humProminenceDb)
                continue;

            // parabola through the log power of the three bins around the peak
            const auto a = std::log (power[(size_t) pk - 1] + 1.0e-30), b = std::log (power[(size_t) pk]), c = std::log (power[(size_t) pk + 1] + 1.0e-30);
            const auto denom = a - 2.0 * b + c;
            const auto shift = denom < 0.0 ? juce::jlimit (-0.5, 0.5, 0.5 * (a - c) / denom) : 0.0;
            found.push_back ({ h, (pk + shift) * binHz, prominence, h % sharedStep != 0 });
        }
        return found;
    };

    // 300 Hz is the 6th harmonic of 50 and the 5th of 60: harmonics that both share say nothing about which one it is
    const auto sum = [] (const std::vector<Harmonic>& v) { double s = 0.0; for (const auto& h : v) s += h.prominenceDb; return s; };
    const auto exclusiveCount = [] (const std::vector<Harmonic>& v) { return (int) std::count_if (v.begin(), v.end(), [] (const Harmonic& h) { return h.exclusive; }); };

    std::vector<Harmonic> best;
    double bestMains = 0.0;
    const auto consider = [&] (double mains, int sharedStep, int needed) {
        auto found = analyse (mains, sharedStep);
        if (exclusiveCount (found) < needed)
            return;
        if (bestMains == 0.0 || exclusiveCount (found) > exclusiveCount (best)
            || (exclusiveCount (found) == exclusiveCount (best) && sum (found) > sum (best)))
        {
            best = std::move (found);
            bestMains = mains;
        }
    };

    switch (options.humMode)
    {
        case HumMode::mains50:    consider (50.0, 6, 1); break;
        case HumMode::mains60:    consider (60.0, 5, 1); break;
        case HumMode::automatic:  consider (50.0, 6, 2); consider (60.0, 5, 2); break;
        case HumMode::off:        break;
    }

    if (bestMains > 0.0)
    {
        result.mainsHz = bestMains;
        for (const auto& h : best)
        {
            result.notchHz.push_back (h.hz);
            result.prominenceDb.push_back (h.prominenceDb);
        }
    }
    return result;
}

PanDetection detectPanBalance (const juce::AudioBuffer<float>& buffer, double sampleRate, const ConditionOptions& options)
{
    PanDetection result;
    if (buffer.getNumChannels() != 2)
    {
        result.outcome = PanOutcome::notStereo;
        return result;
    }

    const auto frameLength = juce::jmax (1, (int) (0.05 * sampleRate));
    const auto frames = buffer.getNumSamples() / frameLength;
    if (frames < 10)
        return result;

    // energy per frame and channel, above 100 Hz
    std::vector<double> energy[2];
    for (int ch = 0; ch < 2; ++ch)
    {
        energy[ch].assign ((size_t) frames, 0.0);
        auto filter = Biquad::highPass (sampleRate, 100.0, 0.7071067811865476);
        const auto* d = buffer.getReadPointer (ch);
        for (int f = 0; f < frames; ++f)
        {
            double sum = 0.0;
            for (int i = 0; i < frameLength; ++i)
            {
                const auto y = filter.process ((double) d[f * frameLength + i]);
                sum += y * y;
            }
            energy[ch][(size_t) f] = sum;
        }
    }

    std::vector<double> totals ((size_t) frames);
    for (int f = 0; f < frames; ++f)
        totals[(size_t) f] = energy[0][(size_t) f] + energy[1][(size_t) f];

    auto sorted = totals;
    std::nth_element (sorted.begin(), sorted.begin() + (std::ptrdiff_t) (frames / 10), sorted.end());
    const auto floor = sorted[(size_t) (frames / 10)];
    std::nth_element (sorted.begin(), sorted.begin() + (std::ptrdiff_t) (frames * 9 / 10), sorted.end());
    const auto top = sorted[(size_t) (frames * 9 / 10)];

    // Sounds with a floor between them: only the frames 12 dB over the floor count. A steady sound (rain, a machine) has no
    // such frames; there everything above -60 dBFS counts instead.
    const auto dynamic = top > floor * 16.0;
    const auto threshold = dynamic ? juce::jmax (floor * 16.0, 2.0 * frameLength * 1.0e-8) : 2.0 * frameLength * 1.0e-6;

    std::vector<double> perFrame;
    double sumLeft = 0.0, sumRight = 0.0;
    for (int f = 0; f < frames; ++f)
    {
        if (totals[(size_t) f] <= threshold)
            continue;

        sumLeft += energy[0][(size_t) f];
        sumRight += energy[1][(size_t) f];
        perFrame.push_back (10.0 * std::log10 ((energy[0][(size_t) f] + 1.0e-18) / (energy[1][(size_t) f] + 1.0e-18)));
    }

    result.activeFrames = (int) perFrame.size();
    if (perFrame.size() < 3 || sumLeft <= 0.0 || sumRight <= 0.0)
        return result;

    result.differenceDb = 10.0 * std::log10 (sumLeft / sumRight);

    const auto percentile = [&] (double p) {
        const auto k = (size_t) std::lround (p * (double) (perFrame.size() - 1));
        std::nth_element (perFrame.begin(), perFrame.begin() + (std::ptrdiff_t) k, perFrame.end());
        return perFrame[k];
    };
    result.spreadDb = percentile (0.75) - percentile (0.25);

    if (result.spreadDb > options.panSpreadDb)
        result.outcome = PanOutcome::imageMoves;
    else if (std::abs (result.differenceDb) > options.panMaxDb)
        result.outcome = PanOutcome::tooLarge;
    else if (std::abs (result.differenceDb) < options.panThresholdDb)
        result.outcome = PanOutcome::balanced;
    else
        result.outcome = PanOutcome::applied;
    return result;
}

float samplePeak (const juce::AudioBuffer<float>& buffer)
{
    float peak = 0.0f;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        peak = juce::jmax (peak, buffer.getMagnitude (ch, 0, buffer.getNumSamples()));
    return peak;
}

double measureLoudness (const juce::AudioBuffer<float>& buffer, double sampleRate)
{
    const auto n = buffer.getNumSamples();
    if (n == 0 || buffer.getNumChannels() == 0)
        return silenceDb;

    // Sum of the K-weighted squares over all channels, as a running total
    std::vector<double> sq ((size_t) n, 0.0);
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto shelf = Biquad::kShelf (sampleRate);
        auto hp = Biquad::kHighPass (sampleRate);
        const auto* d = buffer.getReadPointer (ch);
        for (int i = 0; i < n; ++i)
        {
            const auto y = hp.process (shelf.process ((double) d[i]));
            sq[(size_t) i] += y * y;
        }
    }

    std::vector<double> cum ((size_t) n + 1, 0.0);
    for (int i = 0; i < n; ++i)
        cum[(size_t) i + 1] = cum[(size_t) i] + sq[(size_t) i];

    const auto toLufs = [] (double meanSquare) { return meanSquare > 0.0 ? -0.691 + 10.0 * std::log10 (meanSquare) : silenceDb; };

    const auto block = (int) std::lround (0.4 * sampleRate);
    const auto hop = juce::jmax (1, (int) std::lround (0.1 * sampleRate));
    if (n < block)
        return toLufs (cum[(size_t) n] / n);

    std::vector<double> blocks;
    for (int s = 0; s + block <= n; s += hop)
        blocks.push_back ((cum[(size_t) (s + block)] - cum[(size_t) s]) / block);

    const auto gatedMean = [&] (double thresholdLufs) {
        double sum = 0.0;
        int count = 0;
        for (const auto b : blocks)
            if (toLufs (b) > thresholdLufs)
            {
                sum += b;
                ++count;
            }
        return std::make_pair (count > 0 ? sum / count : 0.0, count);
    };

    const auto [absMean, absCount] = gatedMean (-70.0);
    if (absCount == 0)
        return silenceDb;

    const auto [relMean, relCount] = gatedMean (toLufs (absMean) - 10.0);
    return relCount > 0 ? toLufs (relMean) : toLufs (absMean);
}

void conditionFile (juce::AudioBuffer<float>& buffer, double sampleRate, const ConditionOptions& options, ConditionReport& report)
{
    report = {};
    if (options.dcRemove)
        removeDc (buffer, report);

    // flat tops and spikes are repaired before any filter spreads them
    if (options.declip)
    {
        const auto r = declip (buffer);
        report.declipRuns = r.runs;
        report.declipGainDb = r.gainDb;
    }
    if (options.declick != Strength::off)
    {
        const auto r = declick (buffer, sampleRate, options.declick);
        report.clicksFixed = r.regions;
        report.clicksTooMany = r.tooMany;
    }

    if (options.humMode != HumMode::off)
    {
        const auto hum = detectHum (buffer, sampleRate, options);
        if (hum.mainsHz > 0.0)
        {
            notchHum (buffer, sampleRate, hum.notchHz, options.humQ);
            report.humHz = hum.mainsHz;
            report.humNotches = (int) hum.notchHz.size();
        }
    }

    if (options.lowCutHz > 0.0f)
        lowCut (buffer, sampleRate, options.lowCutHz, options.lowCutOrder);

    if (options.dewind != Strength::off)
    {
        const auto r = dewind (buffer, sampleRate, options.dewind);
        report.windFrames = r.frames;
        report.windMaxCutoffHz = r.maxCutoffHz;
    }
    if (options.denoise != Strength::off)
    {
        const auto r = denoise (buffer, sampleRate, options.denoise);
        report.denoiseOutcome = r.outcome;
        report.noiseFloorDb = r.floorDb;
        report.denoiseAvgDb = r.averageReductionDb;
    }

    if (options.panMode != PanMode::off)
    {
        const auto pan = detectPanBalance (buffer, sampleRate, options);
        if (pan.outcome == PanOutcome::applied)
        {
            // each channel moves half of the difference towards the other: the overall level stays, the peak grows the least
            buffer.applyGain (0, 0, buffer.getNumSamples(), (float) std::pow (10.0, -pan.differenceDb / 40.0));
            buffer.applyGain (1, 0, buffer.getNumSamples(), (float) std::pow (10.0, pan.differenceDb / 40.0));
            report.panApplied = true;
            report.panDb = pan.differenceDb;
        }
    }
}

void conditionCycle (juce::AudioBuffer<float>& buffer, double sampleRate, const ConditionOptions& options, ConditionReport& report)
{
    report = {};
    const auto peak = samplePeak (buffer);
    report.peakBeforeDb = report.peakAfterDb = toDb (peak);

    // Below -120 dBFS there is nothing to normalise, only rounding noise
    if (options.normalize == NormalizeMode::off || peak < 1.0e-6f)
        return;

    double gain;
    if (options.normalize == NormalizeMode::peak)
    {
        gain = options.targetDb - report.peakBeforeDb;
    }
    else
    {
        report.lufsBefore = measureLoudness (buffer, sampleRate);
        if (report.lufsBefore <= silenceDb)
            return;
        gain = options.targetDb - report.lufsBefore;
    }

    const auto ceilingGain = options.ceilingDb - report.peakBeforeDb;
    if (gain > ceilingGain)
    {
        gain = ceilingGain;
        report.gainLimited = true;
    }
    if (gain > options.maxGainDb)
    {
        gain = options.maxGainDb;
        report.gainLimited = true;
    }

    buffer.applyGain ((float) std::pow (10.0, gain / 20.0));
    report.gainDb = gain;
    report.peakAfterDb = report.peakBeforeDb + gain;
}

void addTpdfDither (juce::AudioBuffer<float>& buffer, int bits, juce::int64 seed)
{
    juce::Random rnd (seed);
    const auto lsb = 1.0f / (float) (1 << (bits - 1));
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* d = buffer.getWritePointer (ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            d[i] += (rnd.nextFloat() - rnd.nextFloat()) * lsb;
    }
}

void applyEdgeFade (juce::AudioBuffer<float>& buffer, int fadeSamples)
{
    const auto length = buffer.getNumSamples();
    const auto f = juce::jmin (fadeSamples, length / 2);
    if (f > 0)
    {
        buffer.applyGainRamp (0, f, 0.0f, 1.0f);
        buffer.applyGainRamp (length - f, f, 1.0f, 0.0f);
    }
}

ConditionOptions presetOptions (ConditionPreset preset)
{
    ConditionOptions o;
    if (preset == ConditionPreset::standard)
    {
        o.dcRemove = true;
        o.humMode = HumMode::automatic;
        o.lowCutHz = 20.0f;
        o.lowCutOrder = 2;
        o.panMode = PanMode::automatic;
        o.normalize = NormalizeMode::peak;
        o.targetDb = -1.0;
    }
    return o;
}

namespace
{
const char* const presetNames[] = { "off", "standard", "custom" };
const char* const humNames[] = { "off", "auto", "50", "60" };
const char* const panNames[] = { "off", "auto" };
const char* const normalizeNames[] = { "off", "peak", "lufs" };
const char* const strengthNames[] = { "off", "low", "mid", "high" };

template <typename Enum, size_t N>
Enum enumFrom (const juce::var& v, const char* const (&names)[N], Enum fallback)
{
    const auto s = v.toString();
    for (size_t i = 0; i < N; ++i)
        if (s == names[i])
            return (Enum) i;
    return fallback;
}
} // namespace

juce::var conditionSettingsToVar (const ConditionSettings& s)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("preset", presetNames[(int) s.preset]);
    o->setProperty ("dcRemove", s.options.dcRemove);
    o->setProperty ("declip", s.options.declip);
    o->setProperty ("declick", strengthNames[(int) s.options.declick]);
    o->setProperty ("dewind", strengthNames[(int) s.options.dewind]);
    o->setProperty ("denoise", strengthNames[(int) s.options.denoise]);
    o->setProperty ("hum", humNames[(int) s.options.humMode]);
    o->setProperty ("pan", panNames[(int) s.options.panMode]);
    o->setProperty ("lowCutHz", (double) s.options.lowCutHz);
    o->setProperty ("lowCutOrder", s.options.lowCutOrder);
    o->setProperty ("normalize", normalizeNames[(int) s.options.normalize]);
    o->setProperty ("targetDb", s.options.targetDb);
    o->setProperty ("ceilingDb", s.options.ceilingDb);
    o->setProperty ("dither", s.options.dither);
    o->setProperty ("previewGain", s.previewGain);
    return juce::var (o);
}

ConditionSettings conditionSettingsFromVar (const juce::var& v)
{
    ConditionSettings s;
    if (! v.isObject())
        return s;

    s.preset = enumFrom (v.getProperty ("preset", {}), presetNames, ConditionPreset::off);
    auto& o = s.options;
    o.dcRemove = (bool) v.getProperty ("dcRemove", false);
    o.declip = (bool) v.getProperty ("declip", false);
    o.declick = enumFrom (v.getProperty ("declick", {}), strengthNames, Strength::off);
    o.dewind = enumFrom (v.getProperty ("dewind", {}), strengthNames, Strength::off);
    o.denoise = enumFrom (v.getProperty ("denoise", {}), strengthNames, Strength::off);
    o.humMode = enumFrom (v.getProperty ("hum", {}), humNames, HumMode::off);
    o.panMode = enumFrom (v.getProperty ("pan", {}), panNames, PanMode::off);

    const auto cut = (double) v.getProperty ("lowCutHz", 0.0);
    o.lowCutHz = cut >= 1.0 ? (float) juce::jlimit (10.0, 500.0, cut) : 0.0f;
    o.lowCutOrder = (int) v.getProperty ("lowCutOrder", 2) >= 4 ? 4 : 2;

    o.normalize = enumFrom (v.getProperty ("normalize", {}), normalizeNames, NormalizeMode::off);
    o.targetDb = juce::jlimit (-60.0, 0.0, (double) v.getProperty ("targetDb", -1.0));
    o.ceilingDb = juce::jlimit (-24.0, 0.0, (double) v.getProperty ("ceilingDb", -1.0));
    o.dither = (bool) v.getProperty ("dither", true);
    s.previewGain = (bool) v.getProperty ("previewGain", false);

    // a stored "preset" that no longer matches the values is just custom
    if (s.preset != ConditionPreset::custom && o != presetOptions (s.preset))
        s.preset = ConditionPreset::custom;
    return s;
}

PreviewGain previewGainFor (const juce::AudioBuffer<float>& source, double sampleRate, juce::int64 start, juce::int64 end,
                            const ConditionOptions& options, float fadeMs)
{
    PreviewGain result;
    if (options.normalize == NormalizeMode::off)
        return result;

    start = juce::jlimit<juce::int64> (0, source.getNumSamples(), start);
    end = juce::jlimit<juce::int64> (start, source.getNumSamples(), end);
    if (end <= start)
        return result;

    if ((double) (end - start) > maxPreviewSeconds * sampleRate)
    {
        result.available = false;
        return result;
    }

    juce::AudioBuffer<float> segment (source.getNumChannels(), (int) (end - start));
    for (int ch = 0; ch < source.getNumChannels(); ++ch)
        segment.copyFrom (ch, 0, source, ch, (int) start, segment.getNumSamples());

    applyEdgeFade (segment, (int) (fadeMs * 0.001 * sampleRate));
    conditionCycle (segment, sampleRate, options, result.report);
    result.gain = std::pow (10.0, result.report.gainDb / 20.0);
    return result;
}

juce::String describeConditioning (const ConditionOptions& options, const ConditionReport& report)
{
    juce::StringArray parts;
    if (options.dcRemove)
        parts.add ("dc");
    if (options.declip)
        parts.add (report.declipRuns > 0 ? "declip=" + juce::String (report.declipRuns) + (report.declipGainDb < -0.05 ? "/" + juce::String (report.declipGainDb, 1) + "dB" : juce::String())
                                         : juce::String ("declip=none"));
    if (options.declick != Strength::off)
        parts.add (report.clicksTooMany ? juce::String ("declick=skipped")
                                        : report.clicksFixed > 0 ? "declick=" + juce::String (report.clicksFixed) : juce::String ("declick=none"));
    if (options.humMode != HumMode::off)
        parts.add (report.humHz > 0.0 ? "hum" + juce::String (report.humHz, 0) + "Hz/" + juce::String (report.humNotches) : juce::String ("hum=none"));
    if (options.lowCutHz > 0.0f)
        parts.add ("lc" + juce::String (options.lowCutHz, 0) + "Hz/" + juce::String (options.lowCutOrder >= 4 ? 4 : 2));

    if (options.dewind != Strength::off)
        parts.add (report.windFrames > 0 ? "dewind=" + juce::String (report.windFrames) + "f/" + juce::String (report.windMaxCutoffHz, 0) + "Hz" : juce::String ("dewind=none"));
    if (options.denoise != Strength::off)
        parts.add (report.denoiseOutcome == DenoiseOutcome::applied
                       ? "denoise=" + juce::String (report.denoiseAvgDb, 1) + "dB/floor" + juce::String (report.noiseFloorDb, 0) + "dBFS"
                       : juce::String ("denoise=none"));

    if (options.panMode != PanMode::off)
        parts.add (report.panApplied ? "pan" + juce::String (report.panDb >= 0 ? "+" : "") + juce::String (report.panDb, 1) + "dB" : juce::String ("pan=none"));

    if (options.normalize == NormalizeMode::peak)
        parts.add ("norm=" + juce::String (options.targetDb, 1) + "dBFS");
    else if (options.normalize == NormalizeMode::lufs)
        parts.add ("norm=" + juce::String (options.targetDb, 1) + "LUFS");

    if (options.normalize != NormalizeMode::off)
        parts.add ("gain=" + juce::String (report.gainDb >= 0 ? "+" : "") + juce::String (report.gainDb, 1) + "dB"
                   + (report.gainLimited ? "(limited)" : ""));

    return parts.isEmpty() ? juce::String() : "cond=" + parts.joinIntoString (",");
}
} // namespace wt
