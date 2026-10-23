#pragma once

#include "LabelStore.h"

namespace wt
{
struct Suggestion
{
    juce::String category, type;
    float confidence = 0.0f; // 0..1
    int votes = 0;           // neighbours that agreed
    bool valid() const { return type.isNotEmpty() && confidence > 0.0f; }
};

/**
    Proposes a Type (and Category) for a sound by looking at the sounds a person has already labelled that sound
    most like it (k nearest neighbours on the z-scored features). Nothing is downloaded or trained: it improves the
    moment a new label is saved.

    Confidence = how much of the neighbours' weight agrees on the answer, reduced when even the nearest labelled
    sound is far away (a sound unlike anything seen before should not be trusted).
    The engine is deliberately replaceable (it only needs "features in, suggestion out").
*/
class Suggester
{
public:
    static constexpr int minSamples = 6;

    /** Only human-confirmed labels with a Type are used. */
    void rebuild (const LabelStore& store);

    bool isReady() const { return items.size() >= (size_t) minSamples; }
    size_t sampleCount() const { return items.size(); }

    /** If `categoryHint` is set, only labels from that Category are considered (more precise among its Types). */
    Suggestion suggest (const FeatureVector& v, const juce::String& categoryHint = {}) const;
    std::vector<Suggestion> suggestTop (const FeatureVector& v, int count, const juce::String& categoryHint = {}) const;

private:
    struct Item
    {
        std::vector<float> z;
        juce::String category, type;
    };

    std::vector<float> normalise (const FeatureVector& v) const;

    std::vector<Item> items;
    std::vector<float> mean, sd;
    float typicalNearest = 1.0f; // median distance from a labelled sound to its nearest labelled neighbour
};
} // namespace wt
