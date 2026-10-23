#include "Conditioning.h"

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

void lowCut (juce::AudioBuffer<float>& buffer, double sampleRate, float cutoff, int order)
{
    const auto n = buffer.getNumSamples();
    if (n == 0)
        return;

    const auto freq = juce::jlimit (1.0, sampleRate * 0.25, (double) cutoff);
    // Butterworth: one section at Q 1/sqrt(2), or two at these Qs for 4th order
    std::vector<Biquad> proto;
    if (order >= 4)
    {
        proto.push_back (Biquad::highPass (sampleRate, freq, 0.5411961001461970));
        proto.push_back (Biquad::highPass (sampleRate, freq, 1.3065629648763766));
    }
    else
    {
        proto.push_back (Biquad::highPass (sampleRate, freq, 0.7071067811865476));
    }

    // Run-up on the first sample so a recording that starts away from zero does not click
    const auto warmUp = (int) juce::jmin (sampleRate * 10.0, sampleRate * 8.0 / freq);

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
} // namespace

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
    if (options.lowCutHz > 0.0f)
        lowCut (buffer, sampleRate, options.lowCutHz, options.lowCutOrder);
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

juce::String describeConditioning (const ConditionOptions& options, const ConditionReport& report)
{
    juce::StringArray parts;
    if (options.dcRemove)
        parts.add ("dc");
    if (options.lowCutHz > 0.0f)
        parts.add ("lc" + juce::String (options.lowCutHz, 0) + "Hz/" + juce::String (options.lowCutOrder >= 4 ? 4 : 2));

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
