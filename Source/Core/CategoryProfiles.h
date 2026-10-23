#pragma once

#include "Catalog.h"
#include <map>

namespace wt
{
struct CategoryProfile
{
    juce::String prefix;
    juce::StringArray types; // the 9 slot names
};

/**
    What each Category "looks like" in the palette: its Prefix and its 9 Type names.
    Whatever the user last used with a Category is remembered and comes back when that Category is chosen again;
    a Category never used before starts from the CSV Prefix (or its name in Title case) and the first nine Type candidates.
*/
class CategoryProfiles
{
public:
    static constexpr int numSlots = 9;

    void remember (const juce::String& category, const juce::String& prefix, const juce::StringArray& types)
    {
        if (category.isEmpty())
            return;

        auto padded = types;
        while (padded.size() < numSlots)
            padded.add ({});
        profiles[category] = { prefix, padded };
    }

    const CategoryProfile* find (const juce::String& category) const
    {
        const auto it = profiles.find (category);
        return it == profiles.end() ? nullptr : &it->second;
    }

    /** "TOOLS" -> "Tools", "SCI_FI" -> "SciFi". */
    static juce::String defaultPrefix (const juce::String& category)
    {
        juce::String out;
        for (const auto& part : juce::StringArray::fromTokens (category, "_- ", ""))
            if (part.isNotEmpty())
                out += part.substring (0, 1).toUpperCase() + part.substring (1).toLowerCase();
        return out;
    }

    /** What choosing `category` should put into the palette. */
    CategoryProfile resolve (const juce::String& category, const juce::String& csvPrefix, const std::vector<TypeInfo>& candidates) const
    {
        CategoryProfile out;
        const auto* known = find (category);

        out.prefix = known != nullptr && known->prefix.isNotEmpty() ? known->prefix
                     : csvPrefix.isNotEmpty()                      ? csvPrefix
                                                                   : defaultPrefix (category);

        if (known != nullptr)
        {
            out.types = known->types;
        }
        else
        {
            for (size_t i = 0; i < candidates.size() && (int) i < numSlots; ++i)
                out.types.add (candidates[i].name);
        }

        while (out.types.size() < numSlots)
            out.types.add ({});
        return out;
    }

    juce::var toVar() const
    {
        auto* root = new juce::DynamicObject();
        for (const auto& [category, p] : profiles)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("prefix", p.prefix);
            juce::Array<juce::var> names;
            for (const auto& t : p.types)
                names.add (t);
            o->setProperty ("types", names);
            root->setProperty (category, juce::var (o));
        }
        return juce::var (root);
    }

    void fromVar (const juce::var& v)
    {
        profiles.clear();
        if (const auto* o = v.getDynamicObject())
            for (const auto& prop : o->getProperties())
            {
                juce::StringArray names;
                if (const auto* arr = prop.value.getProperty ("types", {}).getArray())
                    for (const auto& n : *arr)
                        names.add (n.toString());
                remember (prop.name.toString(), prop.value.getProperty ("prefix", "").toString(), names);
            }
    }

private:
    std::map<juce::String, CategoryProfile> profiles;
};
} // namespace wt
