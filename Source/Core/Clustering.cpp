#include "Clustering.h"

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

std::vector<std::vector<float>> normalise (const std::vector<FeatureVector>& items)
{
    std::vector<std::vector<float>> out (items.size(), std::vector<float> (featureDims, 0.0f));
    if (items.empty())
        return out;

    for (int d = 0; d < featureDims; ++d)
    {
        double mean = 0.0;
        for (const auto& it : items)
            mean += it[(size_t) d];
        mean /= (double) items.size();

        double var = 0.0;
        for (const auto& it : items)
            var += std::pow (it[(size_t) d] - mean, 2.0);
        const auto sd = std::sqrt (var / (double) items.size());

        for (size_t i = 0; i < items.size(); ++i)
            out[i][(size_t) d] = sd > 1.0e-9 ? (float) ((items[i][(size_t) d] - mean) / sd) : 0.0f;
    }
    return out;
}

ClusterResult kmeans (const std::vector<std::vector<float>>& data, int k, juce::uint32 seed, int maxIterations)
{
    ClusterResult result;
    const auto n = (int) data.size();
    k = juce::jlimit (1, juce::jmax (1, n), k);
    result.k = k;
    result.assignment.assign ((size_t) n, 0);
    result.distance.assign ((size_t) n, 0.0f);
    if (n == 0)
        return result;

    juce::Random rnd ((juce::int64) seed);

    // k-means++ seeding
    std::vector<std::vector<float>> centres;
    centres.push_back (data[(size_t) rnd.nextInt (n)]);
    std::vector<float> nearest ((size_t) n, std::numeric_limits<float>::max());
    while ((int) centres.size() < k)
    {
        double total = 0.0;
        for (int i = 0; i < n; ++i)
        {
            nearest[(size_t) i] = juce::jmin (nearest[(size_t) i], sqDist (data[(size_t) i], centres.back()));
            total += nearest[(size_t) i];
        }

        auto pick = rnd.nextDouble() * total;
        int chosen = n - 1;
        for (int i = 0; i < n; ++i)
        {
            pick -= nearest[(size_t) i];
            if (pick <= 0.0)
            {
                chosen = i;
                break;
            }
        }
        centres.push_back (data[(size_t) chosen]);
    }

    for (int iter = 0; iter < maxIterations; ++iter)
    {
        bool changed = false;
        for (int i = 0; i < n; ++i)
        {
            int best = 0;
            float bestD = std::numeric_limits<float>::max();
            for (int c = 0; c < k; ++c)
            {
                const auto d = sqDist (data[(size_t) i], centres[(size_t) c]);
                if (d < bestD)
                {
                    bestD = d;
                    best = c;
                }
            }
            if (result.assignment[(size_t) i] != best || iter == 0)
                changed = true;
            result.assignment[(size_t) i] = best;
            result.distance[(size_t) i] = std::sqrt (bestD);
        }

        std::vector<std::vector<float>> sums ((size_t) k, std::vector<float> (data[0].size(), 0.0f));
        std::vector<int> counts ((size_t) k, 0);
        for (int i = 0; i < n; ++i)
        {
            auto& s = sums[(size_t) result.assignment[(size_t) i]];
            for (size_t d = 0; d < s.size(); ++d)
                s[d] += data[(size_t) i][d];
            ++counts[(size_t) result.assignment[(size_t) i]];
        }

        for (int c = 0; c < k; ++c)
        {
            if (counts[(size_t) c] > 0)
            {
                for (size_t d = 0; d < sums[(size_t) c].size(); ++d)
                    centres[(size_t) c][d] = sums[(size_t) c][d] / (float) counts[(size_t) c];
            }
            else
            {
                // an empty group takes the item that is farthest from its own centre
                int far = 0;
                for (int i = 1; i < n; ++i)
                    if (result.distance[(size_t) i] > result.distance[(size_t) far])
                        far = i;
                centres[(size_t) c] = data[(size_t) far];
                changed = true;
            }
        }

        if (! changed)
            break;
    }

    return result;
}

double silhouette (const std::vector<std::vector<float>>& data, const std::vector<int>& assignment, int k)
{
    const auto n = (int) data.size();
    if (n < 3 || k < 2)
        return -1.0;

    double total = 0.0;
    int counted = 0;
    for (int i = 0; i < n; ++i)
    {
        std::vector<double> sum ((size_t) k, 0.0);
        std::vector<int> cnt ((size_t) k, 0);
        for (int j = 0; j < n; ++j)
            if (j != i)
            {
                sum[(size_t) assignment[(size_t) j]] += std::sqrt (sqDist (data[(size_t) i], data[(size_t) j]));
                ++cnt[(size_t) assignment[(size_t) j]];
            }

        const auto own = assignment[(size_t) i];
        if (cnt[(size_t) own] == 0)
            continue;

        const auto a = sum[(size_t) own] / cnt[(size_t) own];
        double b = std::numeric_limits<double>::max();
        for (int c = 0; c < k; ++c)
            if (c != own && cnt[(size_t) c] > 0)
                b = juce::jmin (b, sum[(size_t) c] / cnt[(size_t) c]);
        if (b == std::numeric_limits<double>::max())
            continue;

        total += (b - a) / juce::jmax (a, b);
        ++counted;
    }
    return counted > 0 ? total / counted : -1.0;
}

int suggestK (const std::vector<std::vector<float>>& data, int maxK)
{
    const auto n = (int) data.size();
    if (n < 4)
        return juce::jmax (1, n);

    // silhouette is O(n^2): judge on a sample for big folders
    std::vector<std::vector<float>> sample;
    if (n > 300)
    {
        juce::Random rnd (7);
        for (int i = 0; i < 300; ++i)
            sample.push_back (data[(size_t) rnd.nextInt (n)]);
    }
    const auto& pool = n > 300 ? sample : data;

    int bestK = 2;
    double best = -2.0;
    for (int k = 2; k <= juce::jmin (maxK, (int) pool.size() / 2); ++k)
    {
        const auto r = kmeans (pool, k);
        const auto s = silhouette (pool, r.assignment, k);
        if (s > best + 0.02) // prefer fewer groups unless a larger k is clearly better
        {
            best = s;
            bestK = k;
        }
    }
    return bestK;
}
} // namespace wt
