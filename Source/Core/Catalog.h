#pragma once

#include <juce_events/juce_events.h>

namespace wt
{
struct CategoryInfo
{
    juce::String name; // folder / file-name part, English
    juce::String jp;   // Japanese alias shown next to it
    juce::String note;
};

struct TypeInfo
{
    juce::String name; // used in file names, English
    juce::String jp;
    juce::String note;
};

/**
    Category / Type vocabulary, kept in two UTF-8 CSV files that Excel (or another AI) can edit:
      categories.csv : Category, Japanese, Note
      types.csv      : Category, Type, Japanese, Note      (Category "*" = common to every category)
    Missing files are created from a built-in seed on first start; afterwards the files are the source of truth.
*/
class Catalog : public juce::ChangeBroadcaster
{
public:
    static constexpr const char* commonCategory = "*";

    /** Folder suggestion when none is saved: G:\SFX_Library\_config, else Documents\WaveTagger\config. */
    static juce::File defaultDirectory();

    void setDirectory (const juce::File& dir);
    const juce::File& getDirectory() const { return directory; }
    juce::File getCategoriesFile() const { return directory.getChildFile ("categories.csv"); }
    juce::File getTypesFile() const { return directory.getChildFile ("types.csv"); }

    /** Cheap modification-time check; reloads and notifies if either file changed. */
    bool reloadIfChanged();
    void reload();

    const std::vector<CategoryInfo>& getCategories() const { return categories; }
    /** Types registered for this category (then the common ones if includeCommon). */
    std::vector<TypeInfo> getTypes (const juce::String& category, bool includeCommon = true) const;
    juce::StringArray getWarnings() const { return warnings; }

    /** Appends a row to the CSV (UTF-8, Excel friendly) and reloads. Returns false if the file could not be written. */
    bool addCategory (const juce::String& name, const juce::String& jp);
    bool addType (const juce::String& category, const juce::String& name, const juce::String& jp);

    void noteUsed (const juce::String& category);
    /** Indices into getCategories(), most used first. */
    std::vector<int> frequentIndices (int maxCount) const;
    juce::var usageToVar() const;
    void usageFromVar (const juce::var& v);

private:
    void writeSeedIfMissing();
    bool appendLine (const juce::File& file, const juce::StringArray& fields);

    juce::File directory;
    std::vector<CategoryInfo> categories;
    std::vector<std::pair<juce::String, TypeInfo>> types; // category -> type
    juce::StringArray warnings;
    std::map<juce::String, int> usage;
    juce::Time loadedCategoriesTime, loadedTypesTime;
};
} // namespace wt
