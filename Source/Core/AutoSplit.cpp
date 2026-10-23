#include "AutoSplit.h"

namespace wt
{
std::vector<std::pair<juce::int64, juce::int64>> detectSegments (const AudioData& audio, juce::int64 from, juce::int64 to,
                                                                 const AutoSplitParams& params)
{
    std::vector<std::pair<juce::int64, juce::int64>> out;

    from = juce::jlimit<juce::int64> (0, audio.getNumSamples(), from);
    to = juce::jlimit<juce::int64> (0, audio.getNumSamples(), to);
    const auto window = juce::jmax (1, (int) (audio.sampleRate * 0.005));
    const auto numWindows = (int) ((to - from) / window);
    if (numWindows < 4)
        return out;

    const auto channels = audio.getNumChannels();
    std::vector<float> db ((size_t) numWindows);

    for (int w = 0; w < numWindows; ++w)
    {
        double sum = 0.0;
        for (int ch = 0; ch < channels; ++ch)
        {
            const auto* p = audio.buffer.getReadPointer (ch) + from + (juce::int64) w * window;
            for (int i = 0; i < window; ++i)
                sum += (double) p[i] * p[i];
        }
        const auto rms = std::sqrt (sum / ((double) window * channels));
        db[(size_t) w] = 20.0f * std::log10 ((float) rms + 1.0e-9f);
    }

    auto sorted = db;
    std::nth_element (sorted.begin(), sorted.begin() + (long) (sorted.size() / 5), sorted.end());
    const auto threshold = sorted[sorted.size() / 5] + params.aboveNoiseDb;

    const auto msToWindows = [&] (float ms) { return juce::jmax (1, (int) std::ceil (ms / 5.0f)); };
    const auto gapWindows = msToWindows (params.minGapMs);
    const auto minWindows = msToWindows (params.minLengthMs);

    // active runs, bridging short gaps
    std::vector<std::pair<int, int>> runs;
    int w = 0;
    while (w < numWindows)
    {
        if (db[(size_t) w] <= threshold)
        {
            ++w;
            continue;
        }

        int first = w, last = w;
        for (int k = w + 1; k < numWindows && k - last <= gapWindows; ++k)
            if (db[(size_t) k] > threshold)
                last = k;

        runs.emplace_back (first, last);
        w = last + 1;
    }

    const auto pad = (juce::int64) (params.padMs * 0.001 * audio.sampleRate);
    for (const auto& [first, last] : runs)
    {
        if (last - first + 1 < minWindows)
            continue;

        auto s = juce::jmax (from, from + (juce::int64) first * window - pad);
        auto e = juce::jmin (to, from + (juce::int64) (last + 1) * window + pad);

        if (! out.empty() && s <= out.back().second)
            out.back().second = e;
        else
            out.emplace_back (s, e);
    }

    return out;
}
} // namespace wt
