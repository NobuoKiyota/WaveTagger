#include "Catalog.h"
#include "Categories.h"

namespace wt
{
namespace
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

bool writeCsv (const juce::File& file, const std::vector<juce::StringArray>& rows)
{
    juce::MemoryOutputStream out;
    static const juce::uint8 bom[] = { 0xEF, 0xBB, 0xBF }; // UTF-8 BOM so Excel reads the Japanese correctly
    out.write (bom, sizeof (bom));
    for (const auto& r : rows)
        out << toCsvLine (r);
    return file.replaceWithData (out.getData(), out.getDataSize());
}

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
} // namespace

juce::File Catalog::defaultDirectory()
{
    const auto env = juce::SystemStats::getEnvironmentVariable ("WT_APPDATA", {});
    if (env.isNotEmpty())
        return juce::File (env).getChildFile ("config");

    if (juce::File ("G:\\").exists())
        return juce::File ("G:\\SFX_Library\\_config");

    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("WaveTagger").getChildFile ("config");
}

void Catalog::setDirectory (const juce::File& dir)
{
    directory = dir;
    directory.createDirectory();
    writeSeedIfMissing();
    reload();
}

void Catalog::writeSeedIfMissing()
{
    if (! getCategoriesFile().existsAsFile())
    {
        std::vector<juce::StringArray> rows { { "Category", "Japanese", "Note" } };
        for (const auto& c : wt::getCategories())
            rows.push_back ({ c.name, c.jp, "" });
        writeCsv (getCategoriesFile(), rows);
    }

    if (! getTypesFile().existsAsFile())
    {
        std::vector<juce::StringArray> rows { { "Category", "Type", "Japanese", "Note" } };
        for (const auto& t : seedTypes)
            rows.push_back ({ t.category, t.name, t.jp, "" });
        writeCsv (getTypesFile(), rows);
    }
}

bool Catalog::reloadIfChanged()
{
    if (getCategoriesFile().getLastModificationTime() == loadedCategoriesTime
        && getTypesFile().getLastModificationTime() == loadedTypesTime)
        return false;

    reload();
    return true;
}

void Catalog::reload()
{
    categories.clear();
    types.clear();
    warnings.clear();

    loadedCategoriesTime = getCategoriesFile().getLastModificationTime();
    loadedTypesTime = getTypesFile().getLastModificationTime();

    juce::StringArray seen;
    int line = 0;
    for (const auto& r : parseCsv (getCategoriesFile().loadFileAsString()))
    {
        ++line;
        if (r.size() == 0 || r[0].isEmpty() || (line == 1 && r[0].equalsIgnoreCase ("Category")))
            continue;

        if (seen.contains (r[0], true))
        {
            warnings.add ("categories.csv: " + r[0] + " is listed twice");
            continue;
        }

        seen.add (r[0]);
        categories.push_back ({ r[0], r[1], r[2] });
    }

    line = 0;
    for (const auto& r : parseCsv (getTypesFile().loadFileAsString()))
    {
        ++line;
        if (r.size() < 2 || r[0].isEmpty() || r[1].isEmpty() || (line == 1 && r[0].equalsIgnoreCase ("Category")))
            continue;

        if (r[0] != commonCategory && ! seen.contains (r[0], true))
            warnings.add ("types.csv: category " + r[0] + " is not in categories.csv");

        types.push_back ({ r[0], { r[1], r[2], r[3] } });
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

bool Catalog::appendLine (const juce::File& file, const juce::StringArray& fields)
{
    auto existing = file.loadFileAsString();
    juce::String text;
    if (existing.isNotEmpty() && ! existing.endsWithChar ('\n'))
        text = "\r\n";
    text += toCsvLine (fields);
    return file.appendText (text, false, false);
}

bool Catalog::addCategory (const juce::String& name, const juce::String& jp)
{
    const auto clean = name.trim().toUpperCase();
    if (clean.isEmpty())
        return false;
    for (const auto& c : categories)
        if (c.name.equalsIgnoreCase (clean))
            return true;

    const auto ok = appendLine (getCategoriesFile(), { clean, jp.trim(), "" });
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

    const auto ok = appendLine (getTypesFile(), { cat, cleanName, jp.trim(), "" });
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
