#include "Suggester.h"

namespace wt
{
namespace
{
float sqDist (const std::vector<float>& a, const std::vector<float>& b)
{
    float d = 0.0f;
    for (size_t i = 0; i < a.size(); ++i)
    {
        const auto x = a[i] - b[i];
        d += x * x;
    }
    return d;
}
} // namespace

std::vector<float> Suggester::normalise (const FeatureVector& v) const
{
    std::vector<float> z (featureDims);
    for (int d = 0; d < featureDims; ++d)
        z[(size_t) d] = sd[(size_t) d] > 1.0e-9f ? (v[(size_t) d] - mean[(size_t) d]) / sd[(size_t) d] : 0.0f;
    return z;
}

void Suggester::rebuild (const LabelStore& store)
{
    items.clear();

    std::vector<const LabelSample*> used;
    for (const auto& [key, s] : store.all())
        if (s.human && s.type.isNotEmpty() && ! s.ng)
            used.push_back (&s);

    mean.assign (featureDims, 0.0f);
    sd.assign (featureDims, 1.0f);
    typicalNearest = 1.0f;
    if (used.size() < (size_t) minSamples)
        return;

    for (int d = 0; d < featureDims; ++d)
    {
        double m = 0.0;
        for (const auto* s : used)
            m += s->v[(size_t) d];
        m /= (double) used.size();

        double var = 0.0;
        for (const auto* s : used)
            var += std::pow (s->v[(size_t) d] - m, 2.0);

        mean[(size_t) d] = (float) m;
        sd[(size_t) d] = (float) std::sqrt (var / (double) used.size());
    }

    for (const auto* s : used)
        items.push_back ({ normalise (s->v), s->category, s->type });

    // how far apart are neighbours of the same collection? used to judge whether a new sound is "close enough"
    std::vector<float> nearest;
    const auto sample = juce::jmin ((size_t) 400, items.size());
    for (size_t i = 0; i < sample; ++i)
    {
        float best = std::numeric_limits<float>::max();
        for (size_t j = 0; j < items.size(); ++j)
            if (j != i)
                best = juce::jmin (best, sqDist (items[i].z, items[j].z));
        nearest.push_back (std::sqrt (best));
    }
    std::sort (nearest.begin(), nearest.end());
    typicalNearest = juce::jmax (0.5f, nearest[nearest.size() / 2]);
}

std::vector<Suggestion> Suggester::suggestTop (const FeatureVector& v, int count, const juce::String& categoryHint) const
{
    std::vector<Suggestion> out;
    if (! isReady())
        return out;

    const auto z = normalise (v);

    struct Near
    {
        float d;
        const Item* item;
    };
    std::vector<Near> near;
    for (const auto& it : items)
        if (categoryHint.isEmpty() || it.category == categoryHint)
            near.push_back ({ std::sqrt (sqDist (z, it.z)), &it });

    if (near.empty())
        return out;

    const auto k = juce::jmin ((size_t) 7, near.size());
    std::partial_sort (near.begin(), near.begin() + (long) k, near.end(), [] (const Near& a, const Near& b) { return a.d < b.d; });

    // weights fall off with distance; labels vote for "category/type"
    std::map<juce::String, std::pair<double, int>> votes;
    double total = 0.0;
    for (size_t i = 0; i < k; ++i)
    {
        const auto w = 1.0 / (0.25 + (double) near[i].d * (double) near[i].d);
        auto& v2 = votes[near[i].item->category + "\n" + near[i].item->type];
        v2.first += w;
        ++v2.second;
        total += w;
    }

    // a sound far from everything labelled is not trusted: ~1 near typical spacing, falling to 0 around 3x
    const auto ratio = near[0].d / typicalNearest;
    const auto closeness = (float) std::exp (-std::pow (juce::jmax (0.0f, ratio - 1.0f) / 1.6f, 2.0f));

    std::vector<std::pair<double, juce::String>> ranked;
    for (const auto& [label, vote] : votes)
        ranked.emplace_back (vote.first, label);
    std::sort (ranked.begin(), ranked.end(), [] (const auto& a, const auto& b) { return a.first > b.first; });

    for (size_t i = 0; i < ranked.size() && (int) i < count; ++i)
    {
        Suggestion s;
        s.category = ranked[i].second.upToFirstOccurrenceOf ("\n", false, false);
        s.type = ranked[i].second.fromFirstOccurrenceOf ("\n", false, false);
        s.confidence = (float) (ranked[i].first / total) * closeness;
        s.votes = votes[ranked[i].second].second;
        out.push_back (s);
    }
    return out;
}

Suggestion Suggester::suggest (const FeatureVector& v, const juce::String& categoryHint) const
{
    const auto top = suggestTop (v, 1, categoryHint);
    return top.empty() ? Suggestion() : top.front();
}
} // namespace wt
