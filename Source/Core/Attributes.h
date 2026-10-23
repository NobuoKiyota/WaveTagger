#pragma once

#include <juce_core/juce_core.h>

namespace wt
{
/** One word of a cycle's attribute text: "strength=strong" (key + value) or a bare word such as "outdoor" (key empty). */
struct AttributeTag
{
    juce::String key, value;

    bool operator== (const AttributeTag&) const = default;
};

/** Words are separated by white space; a value may be quoted ("material=\"cardboard box\"") to hold spaces. */
std::vector<AttributeTag> parseAttributes (const juce::String& text);
juce::String formatAttributes (const std::vector<AttributeTag>& tags);

/** Same words, one space between them, quoting only where needed. */
juce::String normaliseAttributes (const juce::String& text);

/** Sets key=value (replacing an existing value of that key, keeping its place); an empty value removes the key. */
juce::String withAttribute (const juce::String& text, const juce::String& key, const juce::String& value);

/**
    The words that can be offered while typing: a few built-in keys with typical values, plus everything that has
    been used before (most used first). Nothing is enforced: any key and value can be typed.
*/
class AttributeVocabulary
{
public:
    AttributeVocabulary();

    void learn (const juce::String& attributeText);

    /** Built-in keys first, then the ones that were learned, each group by use. */
    juce::StringArray keys() const;
    juce::StringArray valuesFor (const juce::String& key) const;

private:
    struct Entry
    {
        int uses = 0;
        std::vector<juce::String> builtin;      // typical values, in their natural order (weak, medium, strong)
        std::map<juce::String, int> values;     // values that were used, with how often
    };

    std::vector<juce::String> builtinOrder;
    std::map<juce::String, Entry> entries; // key (lower case) -> uses
    std::map<juce::String, juce::String> shown; // key (lower case) -> spelling to show
};
} // namespace wt
