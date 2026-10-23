#include "WavMarkers.h"

namespace wt
{
namespace
{
juce::String decodeText (const char* data, size_t size)
{
    size_t n = 0;
    while (n < size && data[n] != 0)
        ++n;

    if (juce::CharPointer_UTF8::isValidString (data, (int) n))
        return juce::String::fromUTF8 (data, (int) n);

    juce::String s;
    for (size_t i = 0; i < n; ++i)
        s += (juce::juce_wchar) (juce::uint8) data[i];
    return s;
}

juce::uint32 rd32 (const juce::MemoryBlock& b, size_t pos)
{
    if (pos + 4 > b.getSize())
        return 0;
    const auto* p = reinterpret_cast<const juce::uint8*> (b.getData()) + pos;
    return (juce::uint32) p[0] | ((juce::uint32) p[1] << 8) | ((juce::uint32) p[2] << 16) | ((juce::uint32) p[3] << 24);
}
} // namespace

std::vector<RawMarker> readWavMarkers (const juce::File& file)
{
    std::vector<RawMarker> result;

    juce::FileInputStream in (file);
    if (! in.openedOk())
        return result;

    char head[12] {};
    if (in.read (head, 12) != 12 || std::memcmp (head, "RIFF", 4) != 0 || std::memcmp (head + 8, "WAVE", 4) != 0)
        return result;

    struct CuePoint
    {
        juce::uint32 id, sampleOffset;
    };
    std::vector<CuePoint> points;
    std::map<juce::uint32, juce::String> labels;
    std::map<juce::uint32, juce::uint32> lengths;

    while (! in.isExhausted())
    {
        char id[4];
        if (in.read (id, 4) != 4)
            break;
        const auto size = (juce::uint32) in.readInt();
        const auto next = in.getPosition() + (juce::int64) size + (size & 1);

        const auto isId = [&] (const char* s) { return std::memcmp (id, s, 4) == 0; };

        if (isId ("cue ") || isId ("LIST"))
        {
            juce::MemoryBlock block;
            in.readIntoMemoryBlock (block, (juce::ssize_t) juce::jmin<juce::uint32> (size, 64u * 1024u * 1024u));

            if (isId ("cue "))
            {
                const auto count = rd32 (block, 0);
                for (juce::uint32 i = 0; i < count && 4 + (size_t) (i + 1) * 24 <= block.getSize(); ++i)
                {
                    const auto base = 4 + (size_t) i * 24;
                    points.push_back ({ rd32 (block, base), rd32 (block, base + 20) });
                }
            }
            else if (block.getSize() >= 4 && std::memcmp (block.getData(), "adtl", 4) == 0)
            {
                size_t pos = 4;
                const auto* bytes = reinterpret_cast<const char*> (block.getData());
                while (pos + 8 <= block.getSize())
                {
                    const auto subSize = (size_t) rd32 (block, pos + 4);
                    const auto body = pos + 8;
                    if (body + subSize > block.getSize())
                        break;

                    if (std::memcmp (bytes + pos, "labl", 4) == 0 && subSize >= 4)
                        labels[rd32 (block, body)] = decodeText (bytes + body + 4, subSize - 4);
                    else if (std::memcmp (bytes + pos, "note", 4) == 0 && subSize >= 4 && labels.count (rd32 (block, body)) == 0)
                        labels[rd32 (block, body)] = decodeText (bytes + body + 4, subSize - 4);
                    else if (std::memcmp (bytes + pos, "ltxt", 4) == 0 && subSize >= 8)
                    {
                        lengths[rd32 (block, body)] = rd32 (block, body + 4);
                        if (subSize > 20 && labels.count (rd32 (block, body)) == 0)
                            labels[rd32 (block, body)] = decodeText (bytes + body + 20, subSize - 20);
                    }

                    pos = body + subSize + (subSize & 1);
                }
            }
        }

        in.setPosition (next);
    }

    for (const auto& p : points)
    {
        RawMarker m;
        m.start = p.sampleOffset;
        m.length = lengths.count (p.id) != 0 ? (juce::int64) lengths[p.id] : 0;
        m.label = labels[p.id];
        result.push_back (m);
    }

    std::stable_sort (result.begin(), result.end(), [] (const RawMarker& a, const RawMarker& b) { return a.start < b.start; });
    return result;
}
} // namespace wt
