#include "Catalog.h"
#include "Categories.h"
#include "Csv.h"
#include "TextDecode.h"
#include "Xlsx.h"

namespace wt
{
namespace
{
struct SeedType
{
    const char* category;
    const char* name;
    const char* jp;
};

// Common verbs plus a few worked examples; extend freely in types.csv.
const SeedType seedTypes[] = {
    { "*", "Hit", "叩く" },          { "*", "Tap", "軽く叩く" },       { "*", "Push", "押す" },        { "*", "Pull", "引く" },
    { "*", "Write", "書く" },        { "*", "Open", "開ける" },        { "*", "Close", "閉める" },     { "*", "Break", "壊す" },
    { "*", "Drop", "落とす" },       { "*", "Rattle", "ガチャガチャ" }, { "*", "Rub", "擦る" },         { "*", "Scrape", "引っかく" },
    { "*", "Turn", "回す" },         { "*", "Click", "カチッ" },       { "*", "Slide", "滑らせる" },   { "*", "Tear", "破る" },
    { "*", "Crumple", "くしゃくしゃ" }, { "*", "Pour", "注ぐ" },        { "*", "Splash", "跳ねる" },

    { "TOOLS", "Open", "開ける" },   { "TOOLS", "Rattle", "カチャカチャ" }, { "TOOLS", "PushIn", "押し込む" }, { "TOOLS", "Break", "壊す" },
    { "TOOLS", "Turn", "回す" },     { "TOOLS", "Rub", "擦る" },       { "TOOLS", "Click", "カチッ" },  { "TOOLS", "Snap", "パチン" },
    { "TOOLS", "Twist", "ひねる" },  { "TOOLS", "Drop", "落とす" },

    { "PAPER", "Tear", "破る" },     { "PAPER", "Crumple", "くしゃくしゃ" }, { "PAPER", "Flip", "めくる" }, { "PAPER", "Fold", "折る" },
    { "PAPER", "Rub", "擦る" },      { "PAPER", "Slide", "滑らせる" }, { "PAPER", "Shuffle", "シャッフル" }, { "PAPER", "Write", "書く" },
    { "PAPER", "Tape", "テープ" },

    { "METAL", "Hit", "叩く" },      { "METAL", "Scrape", "引っかく" }, { "METAL", "Rattle", "ガチャガチャ" }, { "METAL", "Drop", "落とす" },
    { "METAL", "Ring", "鳴らす" },   { "METAL", "Slide", "滑らせる" }, { "METAL", "Squeak", "きしむ" },     { "METAL", "Click", "カチッ" },

    { "WOOD", "Hit", "叩く" },       { "WOOD", "Knock", "ノック" },    { "WOOD", "Creak", "きしむ" },  { "WOOD", "Scrape", "引っかく" },
    { "WOOD", "Break", "折る" },     { "WOOD", "Drop", "落とす" },     { "WOOD", "Slide", "滑らせる" },

    { "DOORS", "Open", "開ける" },   { "DOORS", "Close", "閉める" },  { "DOORS", "Creak", "きしむ" },  { "DOORS", "Knock", "ノック" },
    { "DOORS", "Latch", "ラッチ" },  { "DOORS", "Slam", "バタン" },

    { "GLASS", "Hit", "叩く" },      { "GLASS", "Clink", "カチン" },   { "GLASS", "Break", "割る" },   { "GLASS", "Rub", "擦る" },
    { "GLASS", "Drop", "落とす" },   { "GLASS", "Slide", "滑らせる" },

    { "PLASTIC", "Hit", "叩く" },    { "PLASTIC", "Click", "カチッ" }, { "PLASTIC", "Crack", "ひび割れ" }, { "PLASTIC", "Squeeze", "握る" },
    { "PLASTIC", "Rattle", "ガチャガチャ" }, { "PLASTIC", "Drop", "落とす" },

    { "CLOTH", "Rustle", "擦れる" }, { "CLOTH", "Flap", "はためく" }, { "CLOTH", "Tear", "破る" },   { "CLOTH", "Fold", "畳む" },
    { "CLOTH", "Rub", "擦る" },      { "CLOTH", "Zip", "ジッパー" },

    { "WATER", "Pour", "注ぐ" },     { "WATER", "Splash", "跳ねる" },  { "WATER", "Drip", "滴る" },   { "WATER", "Bubble", "泡" },
    { "WATER", "Stir", "かき混ぜる" }, { "WATER", "Flow", "流れる" },

    { "FOOD", "Crunch", "サクサク" }, { "FOOD", "Chew", "噛む" },     { "FOOD", "Slice", "切る" },    { "FOOD", "Open", "開ける" },
    { "FOOD", "Pour", "注ぐ" },      { "FOOD", "Crack", "割る" },

    { "LEATHER", "Creak", "きしむ" }, { "LEATHER", "Rub", "擦る" },   { "LEATHER", "Fold", "折る" },  { "LEATHER", "Hit", "叩く" },
    { "RUBBER", "Squeak", "キュッ" }, { "RUBBER", "Stretch", "伸ばす" }, { "RUBBER", "Snap", "パチン" }, { "RUBBER", "Bounce", "弾む" },
    { "FEET", "Step", "歩く" },      { "FEET", "Run", "走る" },        { "FEET", "Scuff", "擦る" },    { "FEET", "Jump", "跳ぶ" },
    { "MACHINES", "Start", "始動" }, { "MACHINES", "Stop", "停止" },   { "MACHINES", "Loop", "動作中" }, { "MACHINES", "Click", "カチッ" },
    { "MACHINES", "Buzz", "ブーン" },
};

juce::String md5Of (const juce::File& a, const juce::File& b)
{
    // small FNV-1a content hash (only used to notice that a CSV was edited by hand)
    juce::uint64 h = 1469598103934665603ULL;
    for (const auto& f : { a, b })
    {
        juce::MemoryBlock x;
        if (f.loadFileAsData (x))
            for (size_t i = 0; i < x.getSize(); ++i)
            {
                h ^= (juce::uint64) static_cast<const juce::uint8*> (x.getData())[i];
                h *= 1099511628211ULL;
            }
        h ^= 0xFF;
        h *= 1099511628211ULL;
    }
    return juce::String::toHexString ((juce::int64) h);
}

juce::File backupName (const juce::File& f)
{
    return f.getSiblingFile (f.getFileName() + ".bak-" + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S"));
}

struct SyncState
{
    juce::String dir, csvHash;
    juce::int64 xlsxTime = 0;
};

juce::File stateFile() { return getAppDataDir().getChildFile ("catalog_state.json"); }

SyncState readState()
{
    SyncState st;
    const auto v = readJson (stateFile());
    if (v.isObject())
    {
        st.dir = v.getProperty ("dir", "").toString();
        st.csvHash = v.getProperty ("csvHash", "").toString();
        st.xlsxTime = (juce::int64) v.getProperty ("xlsxTime", 0);
    }
    return st;
}

void writeState (const SyncState& st)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("dir", st.dir);
    o->setProperty ("csvHash", st.csvHash);
    o->setProperty ("xlsxTime", st.xlsxTime);
    writeJson (stateFile(), juce::var (o));
}

void seedRows (std::vector<juce::StringArray>& categoryRows, std::vector<juce::StringArray>& typeRows)
{
    categoryRows = { { "Category", "Japanese", "Note", "Prefix" } };
    for (const auto& c : wt::getCategories())
        categoryRows.push_back ({ c.name, c.jp, "", "" });

    typeRows = { { "Category", "Type", "Japanese", "Note" } };
    for (const auto& t : seedTypes)
        typeRows.push_back ({ t.category, t.name, juce::String::fromUTF8 (t.jp), "" }); // (a plain String from the literal would read the UTF-8 bytes as Latin-1)
}
} // namespace

juce::File Catalog::sharedDirectory()
{
    if (juce::SystemStats::getEnvironmentVariable ("WT_APPDATA", {}).isNotEmpty())
        return {}; // test runs never look at the real Drive folder

    for (const auto& drive : { juce::String::fromUTF8 ("D:\\マイドライブ"), juce::String ("D:\\My Drive") })
        if (juce::File (drive).isDirectory())
            return juce::File (drive).getChildFile ("WaveTagger").getChildFile ("config");

    return {};
}

juce::File Catalog::defaultDirectory()
{
    const auto env = juce::SystemStats::getEnvironmentVariable ("WT_APPDATA", {});
    if (env.isNotEmpty())
        return juce::File (env).getChildFile ("config");

    const auto shared = sharedDirectory();
    if (shared.getFullPathName().isNotEmpty())
        return shared;

    if (juce::File ("G:\\").exists())
        return juce::File ("G:\\SFX_Library\\_config");

    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("WaveTagger").getChildFile ("config");
}

int Catalog::copyConfigFiles (const juce::File& from, const juce::File& to)
{
    to.createDirectory();

    int copied = 0;
    for (const auto* name : { "catalog.xlsx", "categories.csv", "types.csv", "preferences.json", "dictionary.csv" })
    {
        const auto source = from.getChildFile (name);
        const auto target = to.getChildFile (name);
        if (source.existsAsFile() && ! target.exists() && source.copyFileTo (target))
            ++copied;
    }
    return copied;
}

/** Keeps the newest few .bak copies of each file; Drive would otherwise sync an ever-growing pile. */
void Catalog::pruneBackups() const
{
    constexpr int keep = 5;
    for (const auto* base : { "catalog.xlsx", "categories.csv", "types.csv", "preferences.json" })
    {
        auto backups = directory.findChildFiles (juce::File::findFiles, false, juce::String (base) + ".bak-*");
        if (backups.size() <= keep)
            continue;

        std::sort (backups.begin(), backups.end(), [] (const juce::File& a, const juce::File& b) { return a.getFileName() > b.getFileName(); });
        for (int i = keep; i < backups.size(); ++i)
            backups.getReference (i).deleteFile();
    }
}

/** Google Drive makes "name (1).xlsx"-style copies when two PCs edit the same file; point them out instead of silently ignoring them. */
void Catalog::detectConflictCopies()
{
    for (const auto& f : directory.findChildFiles (juce::File::findFiles, false, "*"))
    {
        const auto name = f.getFileName();
        const auto lower = name.toLowerCase();
        const auto stem = lower.upToFirstOccurrenceOf (".", false, false);
        const auto ext = f.getFileExtension().toLowerCase();

        const auto ours = stem.startsWith ("catalog") || stem.startsWith ("categories") || stem.startsWith ("types") || stem.startsWith ("preferences");
        const auto exact = lower == "catalog.xlsx" || lower == "categories.csv" || lower == "types.csv" || lower == "preferences.json";
        const auto known = ext == ".xlsx" || ext == ".csv" || ext == ".json";
        if (ours && known && ! exact && ! lower.contains (".bak-") && ! lower.endsWith ("~"))
            warnings.add ("Possible Drive conflict copy: " + name + " (compare it with the original and delete the one you do not need)");
    }
}

void Catalog::setDirectory (const juce::File& dir)
{
    directory = dir;
    directory.createDirectory();
    reload();
}

juce::String Catalog::currentStamp() const
{
    return juce::String (getWorkbookFile().getLastModificationTime().toMilliseconds()) + "/"
         + juce::String (getCategoriesFile().getLastModificationTime().toMilliseconds()) + "/"
         + juce::String (getTypesFile().getLastModificationTime().toMilliseconds());
}

bool Catalog::reloadIfChanged()
{
    if (currentStamp() == loadedStamp)
        return false;

    reload();
    return true;
}

/**
    Decides which side is newer and brings the other one up to date:
      nothing there          -> seed workbook + CSVs
      only CSVs              -> build the workbook from them (first run after upgrading)
      workbook changed       -> regenerate the CSVs (a hand-edited CSV is backed up first)
      only a CSV was edited  -> merge it into a new workbook (the old one is backed up)
*/
void Catalog::syncAndLoad (std::vector<juce::StringArray>& catRows, std::vector<juce::StringArray>& typeRows)
{
    const auto xlsxFile = getWorkbookFile(), catCsv = getCategoriesFile(), typeCsv = getTypesFile();
    const auto hasX = xlsxFile.existsAsFile();
    const auto hasC = catCsv.existsAsFile() && typeCsv.existsAsFile();
    auto state = readState();
    const auto knownDir = state.dir == directory.getFullPathName();

    const auto readCsvTables = [&] {
        catRows = readCsvRows (catCsv);
        typeRows = readCsvRows (typeCsv);
    };

    auto syncFailed = false;
    const auto writeXlsxTables = [&] (const juce::String& why) {
        juce::String error;
        std::vector<xlsx::Sheet> sheets { { categorySheetName, catRows }, { typeSheetName, typeRows } };
        if (xlsx::write (xlsxFile, sheets, error))
        {
            notices.add (why);
        }
        else
        {
            syncFailed = true;
            warnings.add ("catalog.xlsx: " + error);
        }
    };

    const auto readXlsxTables = [&] {
        std::vector<xlsx::Sheet> sheets;
        juce::String error;
        if (! xlsx::read (xlsxFile, sheets, error) || sheets.empty())
        {
            warnings.add ("catalog.xlsx could not be read (" + error + ")");
            return false;
        }

        const auto find = [&] (const char* name, size_t fallback) -> const xlsx::Sheet* {
            for (const auto& s : sheets)
                if (s.name.equalsIgnoreCase (name))
                    return &s;
            return fallback < sheets.size() ? &sheets[fallback] : nullptr;
        };

        const auto* c = find ("Categories", 0);
        const auto* t = find ("Types", 1);
        if (c == nullptr || t == nullptr)
        {
            warnings.add ("catalog.xlsx needs two sheets: Categories and Types");
            return false;
        }

        categorySheetName = c->name;
        typeSheetName = t->name;
        catRows = c->rows;
        typeRows = t->rows;
        return true;
    };

    // CSV outputs: only touched when the content differs; a hand-edited one is preserved as a backup
    const auto writeCsvOutputs = [&] (bool backupExisting) {
        for (const auto& [file, rows] : { std::pair<juce::File, const std::vector<juce::StringArray>*> { catCsv, &catRows },
                                          std::pair<juce::File, const std::vector<juce::StringArray>*> { typeCsv, &typeRows } })
        {
            const auto data = csvBytes (*rows);
            juce::MemoryBlock existing;
            if (file.existsAsFile() && file.loadFileAsData (existing) && existing == data)
                continue;

            if (backupExisting && file.existsAsFile())
            {
                file.copyFileTo (backupName (file));
                notices.add (file.getFileName() + " had been edited directly; the old copy was kept as a .bak file");
            }
            file.replaceWithData (data.getData(), data.getSize());
        }
    };

    if (! hasX && ! hasC)
    {
        seedRows (catRows, typeRows);
        writeXlsxTables ("catalog.xlsx was created");
        writeCsvOutputs (false);
    }
    else if (! hasX)
    {
        readCsvTables();
        writeXlsxTables ("catalog.xlsx was created from the existing CSV files");
    }
    else
    {
        const auto xTime = xlsxFile.getLastModificationTime().toMilliseconds();
        const auto xChanged = ! knownDir || state.xlsxTime != xTime;
        const auto cChanged = hasC && (! knownDir || state.csvHash != md5Of (catCsv, typeCsv));

        if (! hasC || xChanged)
        {
            if (readXlsxTables())
            {
                // without saved state we cannot tell whether the CSV was hand-edited, so keep a backup if it differs
                writeCsvOutputs (hasC && cChanged);
                if (xChanged && hasC)
                    notices.add ("categories.csv / types.csv were regenerated from catalog.xlsx");
            }
            else if (hasC)
            {
                readCsvTables();
            }
        }
        else if (cChanged)
        {
            readCsvTables();
            xlsxFile.copyFileTo (backupName (xlsxFile));
            writeXlsxTables ("catalog.xlsx was updated from the edited CSV (the previous workbook was kept as a .bak file)");
            writeCsvOutputs (false); // normalise the CSV to UTF-8 (it may have been saved as Shift-JIS)
        }
        else if (! readXlsxTables() && hasC)
        {
            readCsvTables();
        }
    }

    if (syncFailed)
        return; // keep the previous state: the edit is still pending and is retried on the next change or start

    state.dir = directory.getFullPathName();
    state.xlsxTime = xlsxFile.getLastModificationTime().toMilliseconds();
    state.csvHash = md5Of (catCsv, typeCsv);
    writeState (state);
}

void Catalog::reload()
{
    categories.clear();
    types.clear();
    warnings.clear();
    notices.clear();

    std::vector<juce::StringArray> catRows, typeRows;
    syncAndLoad (catRows, typeRows);
    pruneBackups();
    detectConflictCopies();
    loadedStamp = currentStamp();

    juce::StringArray seen;
    int line = 0;
    for (const auto& r : catRows)
    {
        ++line;
        if (r.size() == 0 || r[0].trim().isEmpty() || (line == 1 && r[0].trim().equalsIgnoreCase ("Category")))
            continue;

        const auto name = r[0].trim();
        if (seen.contains (name, true))
        {
            warnings.add ("Categories: " + name + " is listed twice");
            continue;
        }

        seen.add (name);
        categories.push_back ({ name, r[1].trim(), r[2].trim(), r[3].trim() });
    }

    line = 0;
    for (const auto& r : typeRows)
    {
        ++line;
        if (r.size() < 2 || r[0].trim().isEmpty() || r[1].trim().isEmpty() || (line == 1 && r[0].trim().equalsIgnoreCase ("Category")))
            continue;

        const auto cat = r[0].trim();
        if (cat != commonCategory && ! seen.contains (cat, true))
            warnings.add ("Types: category " + cat + " is not in Categories");

        types.push_back ({ cat, { r[1].trim(), r[2].trim(), r[3].trim() } });
    }

    sendChangeMessage();
}

std::vector<TypeInfo> Catalog::getTypes (const juce::String& category, bool includeCommon) const
{
    std::vector<TypeInfo> out;
    juce::StringArray names;

    const auto add = [&] (const juce::String& cat) {
        for (const auto& [c, t] : types)
            if (c.equalsIgnoreCase (cat) && ! names.contains (t.name, true))
            {
                names.add (t.name);
                out.push_back (t);
            }
    };

    if (category.isNotEmpty() && category != commonCategory)
        add (category);
    if (includeCommon || category.isEmpty())
        add (commonCategory);
    return out;
}

bool Catalog::appendRow (bool toTypes, const juce::StringArray& fields)
{
    if (getWorkbookFile().existsAsFile())
    {
        juce::String error;
        return xlsx::appendRow (getWorkbookFile(), toTypes ? typeSheetName : categorySheetName, fields, error);
    }

    // no workbook (should not happen after sync): fall back to the CSV, normalised to UTF-8
    const auto file = toTypes ? getTypesFile() : getCategoriesFile();
    auto rows = readCsvRows (file);
    rows.push_back (fields);
    const auto data = csvBytes (rows);
    return file.replaceWithData (data.getData(), data.getSize());
}

bool Catalog::addCategory (const juce::String& name, const juce::String& jp)
{
    const auto clean = name.trim().toUpperCase();
    if (clean.isEmpty())
        return false;
    for (const auto& c : categories)
        if (c.name.equalsIgnoreCase (clean))
            return true;

    const auto ok = appendRow (false, { clean, jp.trim(), "", "" });
    reload();
    return ok;
}

bool Catalog::addType (const juce::String& category, const juce::String& name, const juce::String& jp)
{
    const auto cleanName = name.trim();
    if (cleanName.isEmpty())
        return false;

    const auto cat = category.isEmpty() ? juce::String (commonCategory) : category;
    for (const auto& [c, t] : types)
        if (c.equalsIgnoreCase (cat) && t.name.equalsIgnoreCase (cleanName))
            return true;

    const auto ok = appendRow (true, { cat, cleanName, jp.trim(), "" });
    reload();
    return ok;
}

void Catalog::noteUsed (const juce::String& category)
{
    if (category.isEmpty())
        return;
    ++usage[category];
    sendChangeMessage();
}

std::vector<int> Catalog::frequentIndices (int maxCount) const
{
    std::vector<std::pair<int, int>> scored; // count, index
    for (int i = 0; i < (int) categories.size(); ++i)
    {
        const auto it = usage.find (categories[(size_t) i].name);
        if (it != usage.end() && it->second > 0)
            scored.emplace_back (it->second, i);
    }

    std::stable_sort (scored.begin(), scored.end(), [] (const auto& a, const auto& b) { return a.first > b.first; });

    std::vector<int> out;
    for (size_t i = 0; i < scored.size() && (int) i < maxCount; ++i)
        out.push_back (scored[i].second);
    return out;
}

juce::var Catalog::usageToVar() const
{
    auto* o = new juce::DynamicObject();
    for (const auto& [k, v] : usage)
        o->setProperty (k, v);
    return juce::var (o);
}

void Catalog::usageFromVar (const juce::var& v)
{
    usage.clear();
    if (const auto* o = v.getDynamicObject())
        for (const auto& p : o->getProperties())
            usage[p.name.toString()] = (int) p.value;
}
} // namespace wt
