#pragma once

#include "AudioFeatures.h"

namespace wt
{
struct ClusterResult
{
    std::vector<int> assignment;     // group index per item
    std::vector<float> distance;     // distance of each item to its group centre (small = typical member)
    int k = 0;
};

/** Z-scores every feature across the items (so that dB, ratios and log-times weigh comparably). */
std::vector<std::vector<float>> normalise (const std::vector<FeatureVector>& items);

/** k-means with k-means++ seeding; deterministic for a given seed. */
ClusterResult kmeans (const std::vector<std::vector<float>>& data, int k, juce::uint32 seed = 42, int maxIterations = 80);

/** Best number of groups in [2, maxK] by silhouette score (on a sample for big folders). */
int suggestK (const std::vector<std::vector<float>>& data, int maxK = 12);

/** Silhouette of a given clustering (-1..1, higher = better separated). */
double silhouette (const std::vector<std::vector<float>>& data, const std::vector<int>& assignment, int k);
} // namespace wt
