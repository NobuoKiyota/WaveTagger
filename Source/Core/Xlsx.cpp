#include "Xlsx.h"

namespace wt::xlsx
{
namespace
{
juce::String localName (const juce::String& tag)
{
    const auto i = tag.lastIndexOfChar (':');
    return i < 0 ? tag : tag.substring (i + 1);
}

bool isTag (const juce::XmlElement& e, const char* name)
{
    return localName (e.getTagName()) == name;
}

juce::String attributeByLocalName (const juce::XmlElement& e, const char* name)
{
    for (int i = 0; i < e.getNumAttributes(); ++i)
        if (localName (e.getAttributeName (i)) == name)
            return e.getAttributeValue (i);
    return {};
}

juce::String readEntry (juce::ZipFile& zip, const juce::String& name)
{
    const auto idx = zip.getIndexOfFileName (name);
    if (idx < 0)
        return {};
    std::unique_ptr<juce::InputStream> stream (zip.createStreamForEntry (idx));
    return stream != nullptr ? stream->readEntireStreamAsString() : juce::String();
}

int columnIndex (const juce::String& cellRef)
{
    int col = 0, n = 0;
    for (auto c : cellRef)
    {
        if (c >= 'A' && c <= 'Z')
            col = col * 26 + (c - 'A' + 1);
        else if (c >= 'a' && c <= 'z')
            col = col * 26 + (c - 'a' + 1);
        else
            break;
        ++n;
    }
    return n == 0 ? -1 : col - 1;
}

juce::String columnLetters (int index)
{
    juce::String s;
    for (int n = index + 1; n > 0; n = (n - 1) / 26)
        s = juce::String::charToString ((juce::juce_wchar) ('A' + (n - 1) % 26)) + s;
    return s;
}

/** Concatenated text of a shared-string / inline-string element (rich-text runs joined, phonetic hints skipped). */
juce::String collectText (const juce::XmlElement& e)
{
    juce::String out;
    for (auto* child : e.getChildIterator())
    {
        if (isTag (*child, "t"))
            out += child->getAllSubText();
        else if (isTag (*child, "r"))
            out += collectText (*child);
    }
    return out;
}

juce::String xmlEscape (const juce::String& s)
{
    juce::String out;
    for (auto c : s)
    {
        if (c == '&')       out += "&amp;";
        else if (c == '<')  out += "&lt;";
        else if (c == '>')  out += "&gt;";
        else if (c == '"')  out += "&quot;";
        else if (c < 32 && c != '\t' && c != '\n' && c != '\r')  continue;
        else                out += c;
    }
    return out;
}

struct SheetRef
{
    juce::String name, path;
};

bool listSheets (juce::ZipFile& zip, std::vector<SheetRef>& out)
{
    const auto workbook = juce::XmlDocument::parse (readEntry (zip, "xl/workbook.xml"));
    const auto rels = juce::XmlDocument::parse (readEntry (zip, "xl/_rels/workbook.xml.rels"));
    if (workbook == nullptr || rels == nullptr)
        return false;

    std::map<juce::String, juce::String> targets;
    for (auto* r : rels->getChildIterator())
    {
        auto target = r->getStringAttribute ("Target");
        target = target.startsWithChar ('/') ? target.substring (1) : "xl/" + target;
        targets[r->getStringAttribute ("Id")] = target;
    }

    for (auto* sheets : workbook->getChildIterator())
        if (isTag (*sheets, "sheets"))
            for (auto* sheet : sheets->getChildIterator())
                if (isTag (*sheet, "sheet"))
                {
                    const auto it = targets.find (attributeByLocalName (*sheet, "id"));
                    if (it != targets.end())
                        out.push_back ({ sheet->getStringAttribute ("name"), it->second });
                }

    return ! out.empty();
}

bool writeZip (const juce::File& target, const std::vector<std::pair<juce::String, juce::MemoryBlock>>& entries, juce::String& error)
{
    const auto tmp = target.getSiblingFile (target.getFileName() + ".tmp~");
    tmp.deleteFile();

    {
        juce::FileOutputStream out (tmp);
        if (! out.openedOk())
        {
            error = "cannot create " + tmp.getFullPathName();
            return false;
        }

        juce::ZipFile::Builder builder;
        for (const auto& [name, data] : entries)
            builder.addEntry (new juce::MemoryInputStream (data, true), 6, name, juce::Time::getCurrentTime());

        if (! builder.writeToStream (out, nullptr))
        {
            error = "zip write failed";
            tmp.deleteFile();
            return false;
        }
        out.flush();
    }

    // a just-written file can be held for a moment by antivirus / the search indexer, so retry briefly
    bool ok = false;
    for (int attempt = 0; attempt < 10 && ! ok; ++attempt)
    {
        if (attempt > 0)
            juce::Thread::sleep (100);
        ok = target.existsAsFile() ? tmp.replaceFileIn (target) : tmp.moveFileTo (target);
    }

    if (! ok)
    {
        tmp.deleteFile();
        error = "cannot replace " + target.getFullPathName() + " (is it open in Excel?)";
    }
    return ok;
}

juce::MemoryBlock utf8Block (const juce::String& s)
{
    return juce::MemoryBlock (s.toRawUTF8(), s.getNumBytesAsUTF8());
}

juce::String cellXml (int row, int col, const juce::String& text, bool header)
{
    return "<c r=\"" + columnLetters (col) + juce::String (row) + "\" t=\"inlineStr\"" + (header ? " s=\"1\"" : "")
           + "><is><t xml:space=\"preserve\">" + xmlEscape (text) + "</t></is></c>";
}
} // namespace

//==============================================================================
bool read (const juce::File& file, std::vector<Sheet>& sheets, juce::String& error)
{
    juce::ZipFile zip (file);
    if (zip.getNumEntries() == 0)
    {
        error = "not an xlsx file: " + file.getFullPathName();
        return false;
    }

    std::vector<SheetRef> refs;
    if (! listSheets (zip, refs))
    {
        error = "workbook structure not recognised";
        return false;
    }

    juce::StringArray shared;
    if (const auto sst = juce::XmlDocument::parse (readEntry (zip, "xl/sharedStrings.xml")))
        for (auto* si : sst->getChildIterator())
            if (isTag (*si, "si"))
                shared.add (collectText (*si));

    sheets.clear();
    for (const auto& ref : refs)
    {
        const auto doc = juce::XmlDocument::parse (readEntry (zip, ref.path));
        if (doc == nullptr)
            continue;

        Sheet sheet;
        sheet.name = ref.name;

        for (auto* data : doc->getChildIterator())
        {
            if (! isTag (*data, "sheetData"))
                continue;

            for (auto* row : data->getChildIterator())
            {
                if (! isTag (*row, "row"))
                    continue;

                juce::StringArray cells;
                for (auto* c : row->getChildIterator())
                {
                    if (! isTag (*c, "c"))
                        continue;

                    auto col = columnIndex (c->getStringAttribute ("r"));
                    if (col < 0)
                        col = cells.size();
                    while (cells.size() <= col)
                        cells.add ({});

                    const auto type = c->getStringAttribute ("t");
                    juce::String value;
                    if (type == "inlineStr")
                    {
                        for (auto* is : c->getChildIterator())
                            if (isTag (*is, "is"))
                                value = collectText (*is);
                    }
                    else
                    {
                        for (auto* v : c->getChildIterator())
                            if (isTag (*v, "v"))
                                value = v->getAllSubText();
                        if (type == "s")
                            value = shared[value.getIntValue()];
                    }
                    cells.set (col, value);
                }

                if (cells.joinIntoString ("").isNotEmpty())
                    sheet.rows.push_back (cells);
            }
        }
        sheets.push_back (std::move (sheet));
    }

    return true;
}

bool write (const juce::File& file, const std::vector<Sheet>& sheets, juce::String& error)
{
    const juce::String ns = "http://schemas.openxmlformats.org/spreadsheetml/2006/main";
    const juce::String rns = "http://schemas.openxmlformats.org/officeDocument/2006/relationships";
    const juce::String head = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n";

    std::vector<std::pair<juce::String, juce::MemoryBlock>> entries;

    juce::String types = head + "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
                                "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
                                "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
                                "<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
                                "<Override PartName=\"/xl/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml\"/>";
    juce::String workbook = head + "<workbook xmlns=\"" + ns + "\" xmlns:r=\"" + rns + "\"><sheets>";
    juce::String rels = head + "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">";

    for (size_t i = 0; i < sheets.size(); ++i)
    {
        const auto n = juce::String ((int) i + 1);
        types += "<Override PartName=\"/xl/worksheets/sheet" + n + ".xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>";
        workbook += "<sheet name=\"" + xmlEscape (sheets[i].name) + "\" sheetId=\"" + n + "\" r:id=\"rId" + n + "\"/>";
        rels += "<Relationship Id=\"rId" + n + "\" Type=\"" + rns + "/worksheet\" Target=\"worksheets/sheet" + n + ".xml\"/>";

        int maxCols = 1;
        for (const auto& r : sheets[i].rows)
            maxCols = juce::jmax (maxCols, r.size());

        juce::String xml = head + "<worksheet xmlns=\"" + ns + "\">"
                                  "<sheetViews><sheetView workbookViewId=\"0\"><pane ySplit=\"1\" topLeftCell=\"A2\" activePane=\"bottomLeft\" state=\"frozen\"/></sheetView></sheetViews>"
                                  "<cols>";
        for (int c = 0; c < maxCols; ++c)
            xml += "<col min=\"" + juce::String (c + 1) + "\" max=\"" + juce::String (c + 1) + "\" width=\"" + (c == 0 ? "22" : c == maxCols - 1 ? "30" : "20") + "\" customWidth=\"1\"/>";
        xml += "</cols><sheetData>";

        for (size_t r = 0; r < sheets[i].rows.size(); ++r)
        {
            xml += "<row r=\"" + juce::String ((int) r + 1) + "\">";
            for (int c = 0; c < sheets[i].rows[r].size(); ++c)
                if (sheets[i].rows[r][c].isNotEmpty())
                    xml += cellXml ((int) r + 1, c, sheets[i].rows[r][c], r == 0);
            xml += "</row>";
        }
        xml += "</sheetData></worksheet>";
        entries.emplace_back ("xl/worksheets/sheet" + n + ".xml", utf8Block (xml));
    }

    types += "</Types>";
    workbook += "</sheets></workbook>";
    rels += "<Relationship Id=\"rIdStyles\" Type=\"" + rns + "/styles\" Target=\"styles.xml\"/></Relationships>";

    const auto rootRels = head + "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                                 "<Relationship Id=\"rId1\" Type=\"" + rns + "/officeDocument\" Target=\"xl/workbook.xml\"/></Relationships>";

    const auto styles = head + "<styleSheet xmlns=\"" + ns + "\">"
                               "<fonts count=\"2\"><font><sz val=\"11\"/><name val=\"Meiryo UI\"/></font><font><b/><sz val=\"11\"/><name val=\"Meiryo UI\"/></font></fonts>"
                               "<fills count=\"2\"><fill><patternFill patternType=\"none\"/></fill><fill><patternFill patternType=\"gray125\"/></fill></fills>"
                               "<borders count=\"1\"><border><left/><right/><top/><bottom/><diagonal/></border></borders>"
                               "<cellStyleXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/></cellStyleXfs>"
                               "<cellXfs count=\"2\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\"/>"
                               "<xf numFmtId=\"0\" fontId=\"1\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyFont=\"1\"/></cellXfs>"
                               "<cellStyles count=\"1\"><cellStyle name=\"Normal\" xfId=\"0\" builtinId=\"0\"/></cellStyles>"
                               "</styleSheet>";

    std::vector<std::pair<juce::String, juce::MemoryBlock>> ordered;
    ordered.emplace_back ("[Content_Types].xml", utf8Block (types));
    ordered.emplace_back ("_rels/.rels", utf8Block (rootRels));
    ordered.emplace_back ("xl/workbook.xml", utf8Block (workbook));
    ordered.emplace_back ("xl/_rels/workbook.xml.rels", utf8Block (rels));
    ordered.emplace_back ("xl/styles.xml", utf8Block (styles));
    for (auto& e : entries)
        ordered.push_back (std::move (e));

    return writeZip (file, ordered, error);
}

bool appendRow (const juce::File& file, const juce::String& sheetName, const juce::StringArray& fields, juce::String& error)
{
    std::unique_ptr<juce::ZipFile> zipHolder;
    std::vector<SheetRef> refs;
    for (int attempt = 0; attempt < 10; ++attempt)
    {
        if (attempt > 0)
            juce::Thread::sleep (100);
        zipHolder = std::make_unique<juce::ZipFile> (file);
        refs.clear();
        if (zipHolder->getNumEntries() > 0 && listSheets (*zipHolder, refs))
            break;
        zipHolder.reset();
    }

    if (zipHolder == nullptr)
    {
        error = "cannot read " + file.getFullPathName();
        return false;
    }
    auto& zip = *zipHolder;

    juce::String sheetPath;
    for (const auto& r : refs)
        if (r.name.equalsIgnoreCase (sheetName))
            sheetPath = r.path;
    if (sheetPath.isEmpty())
    {
        error = "sheet '" + sheetName + "' not found";
        return false;
    }

    std::vector<std::pair<juce::String, juce::MemoryBlock>> entries;
    for (int i = 0; i < zip.getNumEntries(); ++i)
    {
        const auto name = zip.getEntry (i)->filename;
        if (name.endsWithChar ('/'))
            continue;

        std::unique_ptr<juce::InputStream> stream (zip.createStreamForEntry (i));
        juce::MemoryBlock data;
        if (stream != nullptr)
            stream->readIntoMemoryBlock (data);

        if (name == sheetPath)
        {
            auto xml = data.toString();

            // next row number = last <row r="N"> + 1
            int last = 0;
            const auto lastRow = xml.lastIndexOf ("<row ");
            if (lastRow >= 0)
            {
                const auto rpos = xml.indexOf (lastRow, "r=\"");
                if (rpos >= 0)
                    last = xml.substring (rpos + 3).getIntValue();
            }
            const auto rowNumber = last + 1;

            juce::String row = "<row r=\"" + juce::String (rowNumber) + "\">";
            for (int c = 0; c < fields.size(); ++c)
                if (fields[c].isNotEmpty())
                    row += cellXml (rowNumber, c, fields[c], false);
            row += "</row>";

            if (xml.contains ("</sheetData>"))
                xml = xml.replace ("</sheetData>", row + "</sheetData>");
            else if (xml.contains ("<sheetData/>"))
                xml = xml.replace ("<sheetData/>", "<sheetData>" + row + "</sheetData>");
            else
            {
                error = "unexpected sheet layout";
                return false;
            }

            const auto dpos = xml.indexOf ("<dimension ref=\"");
            if (dpos >= 0)
            {
                const auto start = dpos + 16;
                const auto end = xml.indexOfChar (start, '"');
                const auto ref = xml.substring (start, end);
                const auto endRef = ref.contains (":") ? ref.fromFirstOccurrenceOf (":", false, false) : ref;
                const auto lastCol = juce::jmax (columnIndex (endRef), fields.size() - 1);
                xml = xml.replaceSection (start, end - start, ref.upToFirstOccurrenceOf (":", false, false) + ":" + columnLetters (lastCol) + juce::String (rowNumber));
            }

            data = utf8Block (xml);
        }

        entries.emplace_back (name, std::move (data));
    }

    return writeZip (file, entries, error);
}
} // namespace wt::xlsx
