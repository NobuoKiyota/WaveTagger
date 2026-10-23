#pragma once

#include "CycleModel.h"
#include "WavChunks.h"

namespace wt
{
/** The "wtag" chunk: schema 1. Unknown extra fields are ignored on read, so newer files stay readable by older builds. */
struct WtagCycle
{
    juce::int64 start = 0, end = 0;
    int type = -1;         // palette slot when it was written (only a hint)
    juce::String typeName; // what really identifies the type; empty = untyped
    juce::String name;     // output name at that moment, e.g. pen_push03
    juce::String autoType; // suggestion that was made for it, and how sure the engine was
    float autoConf = -1.0f;
    bool confirmed = true;
    int rating = 0;
    bool ng = false;
    juce::String attributes;
};

struct WtagData
{
    int schema = 0;
    juce::int64 savedAt = 0; // ms since epoch; decides whether the WAV or the local project is newer
    double sampleRate = 0.0;
    std::vector<WtagCycle> cycles;
};

constexpr int currentWtagSchema = 1;

/** Everything needed to write the markers into a WAV: RX-visible regions plus the wtag JSON. */
EmbedRequest buildEmbedRequest (const CycleModel& cycles, double sampleRate, const juce::String& appVersion);

bool parseWtag (const juce::var& json, WtagData& out);

/** Which copy of a file's cycles wins on open: the one inside the WAV, or this PC's project file? The one saved later. */
inline bool preferEmbedded (bool hasEmbedded, juce::int64 embeddedSavedAt, bool hasProject, juce::int64 projectModified)
{
    return hasEmbedded && (! hasProject || embeddedSavedAt > projectModified);
}
} // namespace wt
