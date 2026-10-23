#include "Attributes.h"

namespace wt
{
namespace
{
struct Builtin
{
    const char* key;
    std::initializer_list<const char*> values;
};

const Builtin builtins[] = {
    { "strength", { "weak", "medium", "strong" } },
    { "speed", { "slow", "medium", "fast" } },
    { "material", { "wood", "metal", "plastic", "paper", "cardboard", "glass", "cloth", "leather", "rubber", "stone" } },
    { "tool", {} },
    { "mic", {} },
    { "distance", { "close", "mid", "far" } },
};

bool needsQuotes (const juce::String& s)
{
    return s.containsAnyOf (" \t\"");
}

juce::String quoted (const juce::String& s)
{
    return needsQuotes (s) ? s.replace ("\"", "").quoted() : s;
}
} // namespace

std::vector<AttributeTag> parseAttributes (const juce::String& text)
{
    std::vector<AttributeTag> tags;
    const auto n = text.length();
    int i = 0;
    while (i < n)
    {
        while (i < n && juce::CharacterFunctions::isWhitespace (text[i]))
            ++i;
        if (i >= n)
            break;

        // one word: up to white space, but a quoted stretch is taken whole
        juce::String word;
        auto inQuotes = false;
        while (i < n && (inQuotes || ! juce::CharacterFunctions::isWhitespace (text[i])))
        {
            if (text[i] == '"')
                inQuotes = ! inQuotes;
            else
                word += text[i];
            ++i;
        }

        const auto eq = word.indexOfChar ('=');
        AttributeTag tag;
        if (eq > 0)
        {
            tag.key = word.substring (0, eq).trim();
            tag.value = word.substring (eq + 1).trim();
        }
        else
        {
            tag.value = word.trim();
        }

        if (tag.key.isNotEmpty() || tag.value.isNotEmpty())
            tags.push_back (tag);
    }
    return tags;
}

juce::String formatAttributes (const std::vector<AttributeTag>& tags)
{
    juce::StringArray words;
    for (const auto& t : tags)
        words.add (t.key.isEmpty() ? quoted (t.value) : t.key + "=" + quoted (t.value));
    return words.joinIntoString (" ");
}

juce::String normaliseAttributes (const juce::String& text)
{
    return formatAttributes (parseAttributes (text));
}

juce::String withAttribute (const juce::String& text, const juce::String& key, const juce::String& value)
{
    auto tags = parseAttributes (text);
    const auto cleanKey = key.trim();
    const auto cleanValue = value.trim();

    auto it = std::find_if (tags.begin(), tags.end(), [&] (const AttributeTag& t) { return t.key.equalsIgnoreCase (cleanKey); });
    if (cleanValue.isEmpty())
    {
        if (it != tags.end())
            tags.erase (it);
    }
    else if (it != tags.end())
    {
        it->value = cleanValue;
    }
    else
    {
        tags.push_back ({ cleanKey, cleanValue });
    }
    return formatAttributes (tags);
}

AttributeVocabulary::AttributeVocabulary()
{
    for (const auto& b : builtins)
    {
        const auto lower = juce::String (b.key).toLowerCase();
        builtinOrder.push_back (lower);
        shown[lower] = b.key;
        auto& e = entries[lower];
        for (const auto* v : b.values)
            e.builtin.push_back (v);
    }
}

void AttributeVocabulary::learn (const juce::String& attributeText)
{
    for (const auto& t : parseAttributes (attributeText))
    {
        if (t.key.isEmpty())
            continue;

        const auto lower = t.key.toLowerCase();
        auto& e = entries[lower];
        ++e.uses;
        ++e.values[t.value];
        shown.emplace (lower, t.key);
    }
}

juce::StringArray AttributeVocabulary::keys() const
{
    juce::StringArray out;
    for (const auto& k : builtinOrder)
        out.add (shown.at (k));

    std::vector<std::pair<juce::String, int>> learned;
    for (const auto& [k, e] : entries)
        if (std::find (builtinOrder.begin(), builtinOrder.end(), k) == builtinOrder.end())
            learned.push_back ({ k, e.uses });
    std::stable_sort (learned.begin(), learned.end(), [] (const auto& a, const auto& b) { return a.second > b.second; });
    for (const auto& [k, uses] : learned)
        out.add (shown.at (k));
    return out;
}

juce::StringArray AttributeVocabulary::valuesFor (const juce::String& key) const
{
    const auto it = entries.find (key.trim().toLowerCase());
    if (it == entries.end())
        return {};

    // values that were used before come first (most used first), then the built-in ones that were not
    std::vector<std::pair<juce::String, int>> values (it->second.values.begin(), it->second.values.end());
    std::stable_sort (values.begin(), values.end(), [] (const auto& a, const auto& b) { return a.second > b.second; });

    juce::StringArray out;
    for (const auto& [v, uses] : values)
        out.add (v);
    for (const auto& v : it->second.builtin)
        out.addIfNotAlreadyThere (v);
    return out;
}
} // namespace wt
