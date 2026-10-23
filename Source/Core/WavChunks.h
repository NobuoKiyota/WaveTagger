#pragma once

#include <juce_core/juce_core.h>

namespace wt
{
struct EmbedRegion
{
    juce::int64 start = 0, length = 0;
    juce::String label;
};

struct EmbedRequest
{
    std::vector<EmbedRegion> regions; // written as cue + labl + ltxt (what RX, Sound Forge, Audacity ... show as regions)
    juce::String wtagJson;            // written as a "wtag" chunk: full WaveTagger state (types, numbers, version ...)
};

struct EmbedResult
{
    bool ok = false;
    juce::String error;
    int regionsWritten = 0;
};

/**
    Rewrites a RIFF/WAVE file with markers embedded:
      - any existing "cue ", LIST/adtl and "wtag" chunks are replaced; EVERY other chunk (fmt, data, bext, iXML, smpl, INFO ...) is copied byte for byte
      - streamed in 1 MB blocks, so large recordings are fine
      - the new file is written beside the destination, the audio and format chunks are re-read and compared with the source,
        and only then does it replace the destination (so a failure never damages an existing file)
    source and dest may be the same file. RF64 / files over 4 GB are refused.
*/
EmbedResult embedMarkers (const juce::File& source, const juce::File& dest, const EmbedRequest& request);

/** Chunk ids in file order ("LIST" chunks as "LIST:adtl", etc.). Empty if the file is not RIFF/WAVE. */
juce::StringArray listChunkIds (const juce::File& file);

/** The parsed JSON inside the "wtag" chunk, if present. */
bool readWtag (const juce::File& file, juce::var& out);
} // namespace wt
