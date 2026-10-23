#include "WavChunks.h"

namespace wt
{
namespace
{
constexpr juce::int64 maxRiffBytes = 0xFFFFFFFFLL - 64;
constexpr size_t blockSize = 1024 * 1024;

bool idIs (const char* id, const char* name) { return std::memcmp (id, name, 4) == 0; }

void fnv (juce::uint64& h, const char* data, size_t n)
{
    for (size_t i = 0; i < n; ++i)
    {
        h ^= (juce::uint64) (juce::uint8) data[i];
        h *= 1099511628211ULL;
    }
}

struct FileScan
{
    bool ok = false;
    std::vector<juce::String> ids;
    juce::uint64 dataHash = 1469598103934665603ULL;
    juce::int64 dataSize = -1;
    juce::MemoryBlock fmt;
    juce::String error;
};

/** Walks the chunk table; optionally hashes the data chunk. Reads bodies only for fmt / data. */
FileScan scanFile (const juce::File& file, bool hashData)
{
    FileScan scan;
    juce::FileInputStream in (file);
    if (! in.openedOk())
    {
        scan.error = "cannot open " + file.getFullPathName();
        return scan;
    }

    char head[12];
    if (in.read (head, 12) != 12 || ! idIs (head, "RIFF") || ! idIs (head + 8, "WAVE"))
    {
        scan.error = "not a RIFF/WAVE file (RF64 is not supported)";
        return scan;
    }

    const auto total = in.getTotalLength();
    std::vector<char> buffer (blockSize);

    while (in.getPosition() + 8 <= total)
    {
        char id[4];
        in.read (id, 4);
        const auto size = (juce::uint32) in.readInt();
        const auto body = in.getPosition();
        if (body + (juce::int64) size > total)
        {
            scan.error = "a chunk extends past the end of the file";
            return scan;
        }

        auto name = juce::String (juce::CharPointer_ASCII (id), 4);
        if (idIs (id, "LIST") && size >= 4)
        {
            char form[4];
            in.read (form, 4);
            name += ":" + juce::String (juce::CharPointer_ASCII (form), 4);
            in.setPosition (body);
        }
        scan.ids.push_back (name);

        if (idIs (id, "fmt "))
        {
            scan.fmt.setSize (size);
            in.read (scan.fmt.getData(), (int) size);
        }
        else if (idIs (id, "data"))
        {
            scan.dataSize = size;
            if (hashData)
            {
                auto left = (juce::int64) size;
                while (left > 0)
                {
                    const auto n = (int) juce::jmin<juce::int64> (left, (juce::int64) blockSize);
                    if (in.read (buffer.data(), n) != n)
                    {
                        scan.error = "read error in data chunk";
                        return scan;
                    }
                    fnv (scan.dataHash, buffer.data(), (size_t) n);
                    left -= n;
                }
            }
        }

        in.setPosition (body + (juce::int64) size + (size & 1));
    }

    scan.ok = scan.dataSize >= 0 && scan.fmt.getSize() >= 16;
    if (! scan.ok && scan.error.isEmpty())
        scan.error = "fmt or data chunk missing";
    return scan;
}

void writeChunkHeader (juce::OutputStream& out, const char* id, juce::uint32 size)
{
    out.write (id, 4);
    out.writeInt ((int) size);
}

juce::MemoryBlock utf8 (const juce::String& s)
{
    return juce::MemoryBlock (s.toRawUTF8(), s.getNumBytesAsUTF8());
}

void writePadded (juce::OutputStream& out, const void* data, size_t size)
{
    if (size > 0)
        out.write (data, size);
    if (size & 1)
        out.writeByte (0);
}
} // namespace

juce::StringArray listChunkIds (const juce::File& file)
{
    juce::StringArray out;
    const auto scan = scanFile (file, false);
    for (const auto& id : scan.ids)
        out.add (id);
    return out;
}

bool readWtag (const juce::File& file, juce::var& out)
{
    juce::FileInputStream in (file);
    char head[12];
    if (! in.openedOk() || in.read (head, 12) != 12 || ! idIs (head, "RIFF") || ! idIs (head + 8, "WAVE"))
        return false;

    const auto total = in.getTotalLength();
    while (in.getPosition() + 8 <= total)
    {
        char id[4];
        in.read (id, 4);
        const auto size = (juce::uint32) in.readInt();
        const auto body = in.getPosition();
        if (body + (juce::int64) size > total)
            return false;

        if (idIs (id, "wtag") && size < 32u * 1024u * 1024u)
        {
            juce::MemoryBlock block;
            in.readIntoMemoryBlock (block, (juce::ssize_t) size);
            auto parsed = juce::JSON::parse (juce::String::fromUTF8 (static_cast<const char*> (block.getData()), (int) block.getSize()));
            if (! parsed.isObject())
                return false;
            out = parsed;
            return true;
        }

        in.setPosition (body + (juce::int64) size + (size & 1));
    }
    return false;
}

EmbedResult embedMarkers (const juce::File& source, const juce::File& dest, const EmbedRequest& request)
{
    EmbedResult result;
    juce::FileInputStream in (source);
    if (! in.openedOk())
    {
        result.error = "cannot open " + source.getFullPathName();
        return result;
    }

    char head[12];
    if (in.read (head, 12) != 12 || ! idIs (head, "RIFF") || ! idIs (head + 8, "WAVE"))
    {
        result.error = "not a RIFF/WAVE file (RF64 / BW64 are not supported)";
        return result;
    }

    const auto total = in.getTotalLength();
    if (total > maxRiffBytes - 1024 * 1024)
    {
        result.error = "file is too large for a standard WAV (over 4 GB)";
        return result;
    }

    const auto tmp = dest.getSiblingFile (dest.getFileName() + ".wtmp~");
    tmp.deleteFile();

    // Declared before the output stream, so on ANY early return the stream is closed first and only then is the
    // temporary file deleted (Windows refuses to delete a file that is still open).
    struct TmpCleanup
    {
        juce::File file;
        ~TmpCleanup() { file.deleteFile(); }
    } cleanup { tmp };

    juce::uint64 sourceHash = 1469598103934665603ULL;
    juce::int64 sourceDataSize = -1;
    juce::MemoryBlock sourceFmt;
    int copiedChunks = 0;

    {
        juce::FileOutputStream out (tmp);
        if (! out.openedOk())
        {
            result.error = "cannot create " + tmp.getFullPathName();
            return result;
        }

        out.write ("RIFF", 4);
        out.writeInt (0);
        out.write ("WAVE", 4);

        std::vector<char> buffer (blockSize);
        while (in.getPosition() + 8 <= total)
        {
            char id[4];
            in.read (id, 4);
            const auto size = (juce::uint32) in.readInt();
            const auto body = in.getPosition();
            if (body + (juce::int64) size > total)
            {
                out.flush();
                tmp.deleteFile();
                result.error = "a chunk extends past the end of the file; refusing to rewrite it";
                return result;
            }

            auto skip = idIs (id, "cue ") || idIs (id, "wtag");
            if (! skip && idIs (id, "LIST") && size >= 4)
            {
                char form[4];
                in.read (form, 4);
                skip = idIs (form, "adtl");
                in.setPosition (body);
            }

            if (! skip)
            {
                writeChunkHeader (out, id, size);

                const auto isData = idIs (id, "data");
                const auto isFmt = idIs (id, "fmt ");
                if (isData)
                    sourceDataSize = size;
                if (isFmt)
                    sourceFmt.setSize (size);

                auto left = (juce::int64) size;
                juce::int64 done = 0;
                while (left > 0)
                {
                    const auto n = (int) juce::jmin<juce::int64> (left, (juce::int64) blockSize);
                    if (in.read (buffer.data(), n) != n)
                    {
                        out.flush();
                        tmp.deleteFile();
                        result.error = "read error while copying";
                        return result;
                    }
                    if (isData)
                        fnv (sourceHash, buffer.data(), (size_t) n);
                    if (isFmt)
                        sourceFmt.copyFrom (buffer.data(), (int) done, (size_t) n);
                    out.write (buffer.data(), (size_t) n);
                    left -= n;
                    done += n;
                }

                if (size & 1)
                    out.writeByte (0);
                ++copiedChunks;
            }

            in.setPosition (body + (juce::int64) size + (size & 1));
        }

        // ---- markers: cue points, then LIST/adtl with a label and a region length for each
        const auto count = (int) request.regions.size();
        if (count > 0)
        {
            writeChunkHeader (out, "cue ", (juce::uint32) (4 + 24 * count));
            out.writeInt (count);
            for (int i = 0; i < count; ++i)
            {
                const auto start = (juce::uint32) juce::jlimit<juce::int64> (0, 0xFFFFFFFFLL, request.regions[(size_t) i].start);
                out.writeInt (i + 1);       // cue point id
                out.writeInt ((int) start); // position
                out.write ("data", 4);      // fccChunk
                out.writeInt (0);           // chunkStart
                out.writeInt (0);           // blockStart
                out.writeInt ((int) start); // sampleOffset
            }

            juce::MemoryOutputStream adtl;
            adtl.write ("adtl", 4);
            for (int i = 0; i < count; ++i)
            {
                const auto& region = request.regions[(size_t) i];

                const auto text = utf8 (region.label);
                writeChunkHeader (adtl, "labl", (juce::uint32) (4 + text.getSize() + 1));
                adtl.writeInt (i + 1);
                adtl.write (text.getData(), text.getSize());
                adtl.writeByte (0);
                if ((text.getSize() + 1) & 1)
                    adtl.writeByte (0);

                writeChunkHeader (adtl, "ltxt", 20);
                adtl.writeInt (i + 1);
                adtl.writeInt ((int) juce::jlimit<juce::int64> (0, 0xFFFFFFFFLL, region.length));
                adtl.write ("rgn ", 4);
                adtl.writeShort (0); // country
                adtl.writeShort (0); // language
                adtl.writeShort (0); // dialect
                adtl.writeShort (0); // code page
            }

            writeChunkHeader (out, "LIST", (juce::uint32) adtl.getDataSize());
            writePadded (out, adtl.getData(), adtl.getDataSize());
        }

        if (request.wtagJson.isNotEmpty())
        {
            const auto json = utf8 (request.wtagJson);
            writeChunkHeader (out, "wtag", (juce::uint32) json.getSize());
            writePadded (out, json.getData(), json.getSize());
        }

        const auto written = out.getPosition();
        if (written - 8 > maxRiffBytes)
        {
            out.flush();
            tmp.deleteFile();
            result.error = "result would exceed the 4 GB WAV limit";
            return result;
        }

        out.setPosition (4);
        out.writeInt ((int) (written - 8));
        out.flush();
    }

    // ---- verify: the audio and the format must be identical to the source
    const auto check = scanFile (tmp, true);
    if (! check.ok || check.dataSize != sourceDataSize || check.dataHash != sourceHash || check.fmt != sourceFmt)
    {
        tmp.deleteFile();
        result.error = "verification failed (" + (check.error.isNotEmpty() ? check.error : juce::String ("audio data differs")) + "); the original was not changed";
        return result;
    }

    // ---- replace (retry briefly: antivirus or the search indexer may hold the file for a moment)
    bool replaced = false;
    for (int attempt = 0; attempt < 10 && ! replaced; ++attempt)
    {
        if (attempt > 0)
            juce::Thread::sleep (100);
        replaced = dest.existsAsFile() ? tmp.replaceFileIn (dest) : tmp.moveFileTo (dest);
    }

    if (! replaced)
    {
        tmp.deleteFile();
        result.error = "cannot replace " + dest.getFullPathName() + " (is another program using it?)";
        return result;
    }

    result.ok = true;
    result.regionsWritten = (int) request.regions.size();
    juce::ignoreUnused (copiedChunks);
    return result;
}
} // namespace wt
