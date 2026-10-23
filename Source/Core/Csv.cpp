#include "Csv.h"
#include "TextDecode.h"

namespace wt
{
std::vector<juce::StringArray> parseCsv (juce::String text)
{
    if (text.startsWithChar (0xFEFF))
        text = text.substring (1);

    std::vector<juce::StringArray> rows;
    juce::StringArray row;
    juce::String field;
    bool quoted = false, any = false;

    const auto endField = [&] { row.add (field.trim()); field = {}; };
    const auto endRow = [&] {
        endField();
        if (any || row.size() > 1)
            rows.push_back (row);
        row.clear();
        any = false;
    };

    for (int i = 0; i < text.length(); ++i)
    {
        const auto c = text[i];
        if (quoted)
        {
            if (c == '"' && text[i + 1] == '"')
            {
                field += '"';
                ++i;
            }
            else if (c == '"')
                quoted = false;
            else
                field += c;
        }
        else if (c == '"')
        {
            quoted = true;
            any = true;
        }
        else if (c == ',')
        {
            endField();
            any = true;
        }
        else if (c == '\n' || c == '\r')
        {
            if (c == '\r' && text[i + 1] == '\n')
                ++i;
            endRow();
        }
        else
        {
            field += c;
            any = true;
        }
    }
    if (any || field.isNotEmpty())
        endRow();

    return rows;
}

juce::String csvEscape (const juce::String& s)
{
    if (s.containsAnyOf (",\"\r\n"))
        return "\"" + s.replace ("\"", "\"\"") + "\"";
    return s;
}

juce::String toCsvLine (const juce::StringArray& fields)
{
    juce::StringArray out;
    for (const auto& f : fields)
        out.add (csvEscape (f));
    return out.joinIntoString (",") + "\r\n";
}

std::vector<juce::StringArray> readCsvRows (const juce::File& file)
{
    juce::MemoryBlock bytes;
    if (! file.loadFileAsData (bytes))
        return {};
    return parseCsv (decodeTextBytes (bytes));
}

juce::MemoryBlock csvBytes (const std::vector<juce::StringArray>& rows)
{
    juce::MemoryOutputStream out;
    static const juce::uint8 bom[] = { 0xEF, 0xBB, 0xBF }; // UTF-8 BOM so Excel reads the Japanese correctly
    out.write (bom, sizeof (bom));
    const auto width = rows.empty() ? 0 : rows.front().size(); // keep every row as wide as the header
    for (auto r : rows)
    {
        while (r.size() < width)
            r.add ({});
        out << toCsvLine (r);
    }
    return out.getMemoryBlock();
}
} // namespace wt
