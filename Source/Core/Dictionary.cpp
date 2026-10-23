#include "Dictionary.h"
#include "Csv.h"

namespace wt
{
namespace
{
struct Seed
{
    const char* en;
    const char* ja;
};

// What attributes usually say. Extend freely in dictionary.csv.
const Seed seeds[] = {
    // keys
    { "strength", "強さ" }, { "speed", "速さ" }, { "material", "素材" }, { "tool", "道具" }, { "mic", "マイク" }, { "distance", "距離" },
    { "location", "場所" }, { "surface", "表面" }, { "size", "大きさ" }, { "condition", "状態" }, { "direction", "方向" }, { "weight", "重さ" },
    { "hardness", "硬さ" }, { "count", "回数" }, { "position", "位置" },

    // strength / speed / distance
    { "weak", "弱い" }, { "medium", "普通" }, { "strong", "強い" }, { "light", "軽い" }, { "heavy", "重い" }, { "soft", "柔らかい" },
    { "hard", "硬い" }, { "gentle", "そっと" }, { "slow", "遅い" }, { "fast", "速い" }, { "close", "近い" }, { "mid", "中間" }, { "far", "遠い" },

    // materials
    { "wood", "木" }, { "metal", "金属" }, { "plastic", "プラスチック" }, { "paper", "紙" }, { "cardboard", "段ボール" }, { "cardboard", "ダンボール" },
    { "glass", "ガラス" }, { "cloth", "布" }, { "fabric", "布" }, { "leather", "革" }, { "rubber", "ゴム" }, { "stone", "石" }, { "concrete", "コンクリート" },
    { "ceramic", "陶器" }, { "styrofoam", "発泡スチロール" }, { "foam", "フォーム" }, { "water", "水" }, { "sand", "砂" }, { "gravel", "砂利" },
    { "dirt", "土" }, { "grass", "草" }, { "snow", "雪" }, { "ice", "氷" }, { "carpet", "カーペット" }, { "tile", "タイル" }, { "marble", "大理石" },
    { "iron", "鉄" }, { "steel", "鋼" }, { "aluminum", "アルミ" }, { "copper", "銅" }, { "brass", "真鍮" }, { "wire", "ワイヤー" }, { "rope", "ロープ" },
    { "chain", "鎖" }, { "cable", "ケーブル" }, { "tape", "テープ" }, { "vinyl", "ビニール" }, { "foil", "ホイル" }, { "plywood", "合板" },

    // tools and things that make the sound
    { "hand", "手" }, { "finger", "指" }, { "fingernail", "爪" }, { "hammer", "ハンマー" }, { "mallet", "木槌" }, { "stick", "棒" }, { "knife", "ナイフ" },
    { "scissors", "はさみ" }, { "screwdriver", "ドライバー" }, { "wrench", "レンチ" }, { "key", "鍵" }, { "coin", "コイン" }, { "spoon", "スプーン" },
    { "fork", "フォーク" }, { "cup", "カップ" }, { "bottle", "ボトル" }, { "brush", "ブラシ" }, { "pen", "ペン" }, { "pencil", "鉛筆" }, { "box", "箱" },

    // microphones and recording
    { "contact", "コンタクト" }, { "stereo", "ステレオ" }, { "mono", "モノラル" }, { "shotgun", "ガンマイク" }, { "lavalier", "ピンマイク" },
    { "binaural", "バイノーラル" }, { "recorder", "レコーダー" }, { "smartphone", "スマートフォン" },

    // places, states, directions
    { "indoor", "屋内" }, { "outdoor", "屋外" }, { "quiet", "静か" }, { "loud", "大きい" }, { "noisy", "うるさい" }, { "dry", "乾いた" }, { "wet", "湿った" },
    { "hollow", "空洞" }, { "short", "短い" }, { "long", "長い" }, { "single", "単発" }, { "multiple", "複数" }, { "loop", "ループ" },
    { "left", "左" }, { "right", "右" }, { "up", "上" }, { "down", "下" }, { "front", "前" }, { "back", "後ろ" },
};

/** The key two words are compared by: no outer white space, one space inside, half-width letters, lower case. */
juce::String normalise (const juce::String& text)
{
    juce::String out;
    bool space = false;
    for (auto c : text.trim())
    {
        if (c >= 0xFF01 && c <= 0xFF5E)
            c = (juce::juce_wchar) (c - 0xFEE0); // full-width ASCII -> ASCII
        if (c == 0x3000 || juce::CharacterFunctions::isWhitespace (c))
        {
            space = true;
            continue;
        }
        if (space && out.isNotEmpty())
            out += ' ';
        space = false;
        out += juce::CharacterFunctions::toLowerCase (c);
    }
    return out;
}

bool isKana (juce::juce_wchar c) { return (c >= 0x3041 && c <= 0x30FF) || (c >= 0xFF66 && c <= 0xFF9F); }
bool isKanji (juce::juce_wchar c) { return (c >= 0x4E00 && c <= 0x9FFF) || (c >= 0x3400 && c <= 0x4DBF); }
} // namespace

Dictionary::Dictionary()
{
    for (const auto& s : seeds)
        builtin.push_back ({ juce::String::fromUTF8 (s.en), juce::String::fromUTF8 (s.ja), {}, false });
    rebuild();
}

bool Dictionary::isJapanese (const juce::String& text)
{
    for (auto c : text)
        if (isKana (c) || isKanji (c))
            return true;
    return false;
}

bool Dictionary::isNeutral (const juce::String& text)
{
    bool letters = false;
    for (auto c : text)
    {
        if (isKana (c) || isKanji (c))
            return false;
        letters = letters || juce::CharacterFunctions::isLetter (c);
    }

    // "12", "-", and quantities that start with a number ("3m", "10cm", "-3.5dB") say the same in every language
    const auto t = text.trim().trimCharactersAtStart ("+-.~");
    return ! letters || (t.isNotEmpty() && juce::CharacterFunctions::isDigit (t[0]));
}

void Dictionary::rebuild()
{
    all.clear();
    toJa.clear();
    toEn.clear();
    numUser = (int) userEntries.size();

    for (const auto* list : { &userEntries, &builtin })
        for (const auto& e : *list)
        {
            all.push_back (e);
            // user entries come first, so for the same word they win; among rows with one English word the first Japanese is preferred
            toJa.emplace (normalise (e.english), e.japanese);
            toEn.emplace (normalise (e.japanese), e.english);
        }
}

void Dictionary::setFile (const juce::File& f)
{
    file = f;
    reload();
}

void Dictionary::reload()
{
    userEntries.clear();
    loadedTime = {};
    loadedSize = -1;

    if (file.existsAsFile())
    {
        loadedTime = file.getLastModificationTime();
        loadedSize = file.getSize();

        for (const auto& row : readCsvRows (file))
        {
            if (row.size() < 2 || row[0].isEmpty() || row[1].isEmpty())
                continue;
            if (row[0].equalsIgnoreCase ("English") && row[1].equalsIgnoreCase ("Japanese"))
                continue; // the header
            userEntries.push_back ({ row[0], row[1], row.size() > 2 ? row[2] : juce::String(), true });
        }
    }
    rebuild();
}

bool Dictionary::reloadIfChanged()
{
    if (file == juce::File())
        return false;

    const auto exists = file.existsAsFile();
    if (exists ? (file.getLastModificationTime() == loadedTime && file.getSize() == loadedSize) : (loadedSize < 0))
        return false;

    reload();
    return true;
}

std::optional<juce::String> Dictionary::toJapanese (const juce::String& word) const
{
    if (word.trim().isEmpty())
        return std::nullopt;
    if (isJapanese (word))
        return word;

    const auto it = toJa.find (normalise (word));
    if (it == toJa.end())
        return std::nullopt;
    return it->second;
}

std::optional<juce::String> Dictionary::toEnglish (const juce::String& word) const
{
    if (word.trim().isEmpty())
        return std::nullopt;
    if (! isJapanese (word))
        return word;

    const auto it = toEn.find (normalise (word));
    if (it == toEn.end())
        return std::nullopt;
    return it->second;
}

bool Dictionary::save()
{
    if (file == juce::File())
        return false;

    std::vector<juce::StringArray> rows { { "English", "Japanese", "Note" } };
    for (const auto& e : userEntries)
        rows.push_back ({ e.english, e.japanese, e.note });

    // next to the real file and swapped in, so a failure (Excel has it open, Drive is syncing) leaves the old one alone
    file.getParentDirectory().createDirectory();
    juce::TemporaryFile temp (file);
    const auto data = csvBytes (rows);
    if (! temp.getFile().replaceWithData (data.getData(), data.getSize()) || ! temp.overwriteTargetFileWithTemporary())
        return false;

    loadedTime = file.getLastModificationTime();
    loadedSize = file.getSize();
    return true;
}

bool Dictionary::setUserEntry (const juce::String& english, const juce::String& japanese, const juce::String& note)
{
    const auto en = english.trim(), ja = japanese.trim();
    if (en.isEmpty() || ja.isEmpty())
        return false;

    const auto before = userEntries;
    const auto key = normalise (en);
    auto it = std::find_if (userEntries.begin(), userEntries.end(), [&] (const DictionaryEntry& e) { return normalise (e.english) == key; });
    if (it != userEntries.end())
    {
        it->english = en;
        it->japanese = ja;
        if (note.isNotEmpty())
            it->note = note;
    }
    else
    {
        userEntries.push_back ({ en, ja, note, true });
    }

    if (! save())
    {
        userEntries = before;
        return false;
    }
    rebuild();
    return true;
}

bool Dictionary::removeUserEntry (const juce::String& english)
{
    const auto key = normalise (english);
    const auto before = userEntries;
    userEntries.erase (std::remove_if (userEntries.begin(), userEntries.end(), [&] (const DictionaryEntry& e) { return normalise (e.english) == key; }),
                       userEntries.end());
    if (userEntries.size() == before.size())
        return false;

    if (! save())
    {
        userEntries = before;
        return false;
    }
    rebuild();
    return true;
}
} // namespace wt
