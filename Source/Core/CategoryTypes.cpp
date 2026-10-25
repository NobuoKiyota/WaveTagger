#include "CategoryTypes.h"

#include "Dictionary.h"

namespace wt
{
namespace
{
juce::String ja (const char* utf8) { return juce::String::fromUTF8 (utf8); }

/** Upper-case letters and digits as words ("TOOL_BOXES" -> TOOL, BOXES), each without a plural S ("BOXES" -> BOX, "TOOLS" -> TOOL). */
juce::StringArray stems (const juce::String& name)
{
    juce::StringArray words;
    juce::String word;
    const auto flush = [&] {
        if (word.length() > 3 && word.endsWithChar ('S'))
            word = word.dropLastCharacters (word.endsWith ("ES") && word.length() > 4 ? 2 : 1);
        if (word.length() >= 3)
            words.add (word);
        word = {};
    };

    for (auto c : name.toUpperCase())
    {
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
            word += c;
        else
            flush();
    }
    flush();
    return words;
}

int commonPrefix (const juce::String& a, const juce::String& b)
{
    int n = 0;
    while (n < a.length() && n < b.length() && a[n] == b[n])
        ++n;
    return n;
}

int nameScore (const juce::StringArray& wanted, const juce::StringArray& other)
{
    int score = 0;
    for (const auto& a : wanted)
        for (const auto& b : other)
        {
            if (a == b)
                score += 10;
            else if (a.length() >= 4 && b.length() >= 4 && (a.contains (b) || b.contains (a)))
                score += 6;
            else if (commonPrefix (a, b) >= 4)
                score += 3;
        }
    return score;
}

juce::String compact (const juce::String& s)
{
    juce::String out;
    for (auto c : s)
        if (! juce::CharacterFunctions::isWhitespace (c) && c != 0x3000)
            out += c;
    return out;
}

int japaneseScore (const juce::String& wantedRaw, const juce::String& otherRaw)
{
    const auto wanted = compact (wantedRaw), other = compact (otherRaw);
    if (wanted.isEmpty() || other.isEmpty())
        return 0;

    int score = 0;
    if (other.contains (wanted) || wanted.contains (other))
        score += 8;

    // two-character pieces in common (one point of 2 each, once)
    juce::StringArray seen;
    for (int i = 0; i + 1 < wanted.length(); ++i)
    {
        const auto piece = wanted.substring (i, i + 2);
        if (other.contains (piece) && ! seen.contains (piece))
        {
            seen.add (piece);
            score += 2;
        }
    }
    return score;
}

/** Every Type name in the catalogue (the spelling that is used most), with how many Categories have it. */
struct Vocabulary
{
    std::vector<std::pair<juce::String, int>> byUse; // spelling, count: most used first
    std::map<juce::String, juce::String> spelling;   // lower case -> spelling
};

Vocabulary vocabularyOf (const Catalog& catalog)
{
    std::map<juce::String, std::map<juce::String, int>> spellings; // lower -> spelling -> count
    const auto count = [&] (const juce::String& category) {
        for (const auto& t : catalog.getTypes (category, false))
            ++spellings[t.name.toLowerCase()][t.name];
    };
    count ({}); // the common Types (getTypes ({}, false); asking for "*" itself gives nothing)
    for (const auto& c : catalog.getCategories())
        count (c.name);

    Vocabulary v;
    for (const auto& [lower, forms] : spellings)
    {
        int total = 0;
        juce::String best;
        int bestCount = -1;
        for (const auto& [form, n] : forms)
        {
            total += n;
            if (n > bestCount)
            {
                best = form;
                bestCount = n;
            }
        }
        v.spelling[lower] = best;
        v.byUse.push_back ({ best, total });
    }
    std::stable_sort (v.byUse.begin(), v.byUse.end(), [] (const auto& a, const auto& b) { return a.second > b.second; });
    return v;
}

juce::String listOf (const std::vector<TypeProposal>& types)
{
    juce::StringArray parts;
    for (const auto& t : types)
        parts.add (t.japanese.isNotEmpty() ? t.english + "=" + t.japanese : t.english);
    return parts.joinIntoString (", ");
}

bool isAsciiWord (const juce::String& s)
{
    if (s.isEmpty())
        return false;
    for (auto c : s)
        if (! ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == ' '))
            return false;
    return juce::CharacterFunctions::isLetter (s[0]);
}

/** "1. Hit - 叩く", "- Hit=叩く", "Hit (叩く)", "Hit,叩く", "Hit：叩く" ... -> English / Japanese. */
bool pairFromText (juce::String text, TypeProposal& out, bool strict)
{
    text = text.trim();

    // bullets and numbers
    while (text.isNotEmpty())
    {
        const auto before = text;
        text = text.trimCharactersAtStart (ja ("-*・•>#\" \t"));
        auto i = 0;
        while (i < text.length() && text[i] >= '0' && text[i] <= '9')
            ++i;
        if (i > 0 && i < text.length() && (text[i] == '.' || text[i] == ')' || text[i] == ':' || text[i] == 0x3001))
            text = text.substring (i + 1).trimStart();
        if (text == before)
            break;
    }

    int cut = -1;
    for (int i = 0; i < text.length(); ++i)
    {
        const auto c = text[i];
        if (c == '=' || c == ',' || c == ':' || c == '(' || c == 0xFF0C || c == 0xFF1A || c == 0xFF08 || c == '\t' || c == 0x2192
            || c == 0x2013 || c == 0x2014 || c == '|' || (c == '-' && i > 0 && text[i - 1] == ' '))
        {
            cut = i;
            break;
        }
        if (Dictionary::isJapanese (juce::String::charToString (c)))
        {
            cut = i;
            break;
        }
    }

    auto english = (cut >= 0 ? text.substring (0, cut) : text).trim().trimCharactersAtEnd ("\"'");
    auto japanese = cut >= 0 ? text.substring (cut) : juce::String();
    japanese = japanese.trimCharactersAtStart ("=,:(\t-|\"'")
                       .trimCharactersAtStart (juce::String::charToString (0xFF0C) + juce::String::charToString (0xFF1A) + juce::String::charToString (0xFF08)
                                               + juce::String::charToString (0x2192) + juce::String::charToString (0x2013) + juce::String::charToString (0x2014) + " ")
                       .trimCharactersAtEnd (")\"'," + juce::String::charToString (0xFF09) + " ")
                       .trim();

    if (! isAsciiWord (english))
        return false;

    // a line of an answer (not an item of a JSON list) is only a Type if it looks like one: a single word, or a word followed by Japanese.
    // "no idea" and "Sure, here are the types:" are sentences.
    if (strict)
    {
        if (cut < 0 && english.containsChar (' '))
            return false;
        if (japanese.isNotEmpty() && ! Dictionary::isJapanese (japanese))
            return false;
    }

    out = { english, japanese };
    return true;
}

juce::String firstOf (const juce::var& item, std::initializer_list<const char*> keys)
{
    for (const auto* k : keys)
        if (const auto v = item.getProperty (k, {}).toString().trim(); v.isNotEmpty())
            return v;
    return {};
}
} // namespace

std::vector<TypeProposal> ownTypesOf (const Catalog& catalog, const juce::String& category, int maxCount)
{
    std::vector<TypeProposal> out;
    for (const auto& t : catalog.getTypes (category, false))
    {
        if ((int) out.size() >= maxCount)
            break;
        out.push_back ({ t.name, t.jp.isNotEmpty() ? t.jp : catalog.japaneseForType (category, t.name) });
    }
    return out;
}

std::vector<TypeProposal> commonTypes (const Catalog& catalog, int maxCount)
{
    return ownTypesOf (catalog, {}, maxCount); // an empty Category means the common ones
}

std::vector<SimilarCategory> rankSimilarCategories (const Catalog& catalog, const juce::String& newName, const juce::String& newJapanese)
{
    const auto wanted = stems (newName);

    std::vector<SimilarCategory> out;
    for (const auto& c : catalog.getCategories())
    {
        const auto typeCount = (int) catalog.getTypes (c.name, false).size();
        if (typeCount == 0)
            continue; // nothing to copy

        SimilarCategory s;
        s.name = c.name;
        s.japanese = c.jp;
        s.typeCount = typeCount;
        s.score = nameScore (wanted, stems (c.name)) + japaneseScore (newJapanese, c.jp);
        out.push_back (s);
    }

    std::stable_sort (out.begin(), out.end(), [] (const SimilarCategory& a, const SimilarCategory& b) {
        if (a.score != b.score)
            return a.score > b.score;
        if (a.typeCount != b.typeCount)
            return a.typeCount > b.typeCount;
        return a.name < b.name;
    });
    return out;
}

juce::String buildAiPrompt (const Catalog& catalog, const juce::String& newName, const juce::String& newJapanese)
{
    const auto label = newName.trim().toUpperCase() + (newJapanese.trim().isNotEmpty() ? " (" + newJapanese.trim() + ")" : juce::String());

    juce::String p;
    p += ja ("あなたは効果音ライブラリ(SFX)のカテゴリ設計の手伝いをします。\n");
    p += ja ("新しいカテゴリ「") + label + ja ("」に付ける「Type」(録音する音の種類・動作・状態)を、10 個提案してください。\n\n");

    p += ja ("条件:\n");
    p += ja ("- English は 1 語の PascalCase(例: Hit, PushIn, Rustle)。スペース・記号・日本語は使わない。20 文字以内\n");
    p += ja ("- 現場録音でよく出る順に並べる。最初の 9 個がキーボードの 1〜9 に割り当たる(10 個目は予備)\n");
    p += ja ("- 下の「既存の Type」にある語は、同じ綴りをそのまま使う。新しい語は、本当に必要なときだけ\n");
    p += ja ("- Japanese は短い日本語(例: 叩く)\n");
    p += ja ("- 重複させない\n\n");

    // similar categories as examples
    auto similar = rankSimilarCategories (catalog, newName, newJapanese);
    juce::StringArray examples;
    for (const auto& s : similar)
    {
        // the ones that resemble it; if none does, the three with the most Types (the list is sorted that way)
        if (examples.size() >= 3 || (similar.front().score > 0 && s.score == 0))
            break;
        examples.add (s.name + (s.japanese.isNotEmpty() ? " (" + s.japanese + ")" : juce::String()) + ": " + listOf (ownTypesOf (catalog, s.name)));
    }
    if (! examples.isEmpty())
    {
        p += ja ("参考(すでにあるカテゴリの Type):\n");
        for (const auto& e : examples)
            p += e + "\n";
        p += "\n";
    }

    const auto vocabulary = vocabularyOf (catalog);
    juce::StringArray names;
    for (int i = 0; i < (int) vocabulary.byUse.size() && i < 60; ++i)
        names.add (vocabulary.byUse[(size_t) i].first);
    if (! names.isEmpty())
        p += ja ("既存の Type(よく使われる順): ") + names.joinIntoString (", ") + "\n\n";

    p += ja ("出力は、次の形式の JSON だけにしてください(説明やコードブロックは付けない):\n");
    p += "{\"types\":[{\"en\":\"Rustle\",\"ja\":" + ja ("\"擦れる\"") + "},{\"en\":\"Flap\",\"ja\":" + ja ("\"はためく\"") + "}]}\n";
    return p;
}

bool parseAiReply (const juce::String& reply, std::vector<TypeProposal>& out, juce::String& error)
{
    out.clear();

    // JSON: from the first { or [ to the last } or ]
    auto text = reply;
    const auto open = text.indexOfAnyOf ("{[");
    if (open >= 0)
    {
        const auto closing = text[open] == '{' ? '}' : ']';
        const auto close = text.lastIndexOfChar (closing);
        if (close > open)
        {
            juce::var parsed;
            if (juce::JSON::parse (text.substring (open, close + 1), parsed).wasOk())
            {
                const juce::Array<juce::var>* items = parsed.getArray();
                if (items == nullptr && parsed.isObject())
                    for (const auto* key : { "types", "Types", "items", "list", "data" })
                        if (const auto* a = parsed.getProperty (key, {}).getArray())
                        {
                            items = a;
                            break;
                        }

                if (items != nullptr)
                    for (const auto& item : *items)
                    {
                        TypeProposal t;
                        if (item.isObject())
                        {
                            t.english = firstOf (item, { "en", "english", "English", "EN", "name", "type", "Type" });
                            t.japanese = firstOf (item, { "ja", "japanese", "Japanese", "JA", "jp", "JP", ja ("日本語").toRawUTF8() });
                        }
                        else if (! pairFromText (item.toString(), t, false))
                        {
                            continue;
                        }
                        if (t.english.isNotEmpty())
                            out.push_back (t);
                    }
            }
        }
    }

    // plain lines
    if (out.empty())
    {
        juce::StringArray lines;
        lines.addLines (reply);
        for (const auto& line : lines)
        {
            TypeProposal t;
            if (line.trim().isNotEmpty() && pairFromText (line, t, true))
                out.push_back (t);
        }
    }

    if (out.empty())
    {
        error = ja ("返事から Type を読み取れませんでした。JSON({\"types\":[{\"en\":\"Hit\",\"ja\":\"叩く\"}]})か、1 行に 1 つ「Hit = 叩く」の形で貼ってください。");
        return false;
    }
    return true;
}

std::vector<TypeProposal> tidyProposals (const std::vector<TypeProposal>& proposals, const Catalog& catalog, int maxCount)
{
    const auto vocabulary = vocabularyOf (catalog);

    std::vector<TypeProposal> out;
    juce::StringArray seen;
    for (const auto& p : proposals)
    {
        if ((int) out.size() >= maxCount)
            break;

        // one PascalCase word of letters and digits
        juce::String english;
        juce::String word;
        const auto flush = [&] {
            if (word.isEmpty())
                return;
            if (word.length() > 3 && word == word.toUpperCase())
                word = word.toLowerCase(); // "RUSTLE" -> "Rustle"
            english += word.substring (0, 1).toUpperCase() + word.substring (1);
            word = {};
        };
        for (auto c : p.english)
        {
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
                word += c;
            else
                flush();
        }
        flush();

        if (english.isEmpty() || english.length() > 24 || ! juce::CharacterFunctions::isLetter (english[0]) || Dictionary::isJapanese (p.english))
            continue;

        // spelled like the Type of that name that exists already
        if (const auto it = vocabulary.spelling.find (english.toLowerCase()); it != vocabulary.spelling.end())
            english = it->second;

        if (seen.contains (english, true))
            continue;
        seen.add (english);

        auto japanese = p.japanese.trim().trimCharactersAtStart ("\"'(").trimCharactersAtEnd ("\"')");
        if (japanese.isEmpty())
            japanese = catalog.japaneseForType ({}, english);
        out.push_back ({ english, japanese });
    }
    return out;
}
} // namespace wt
