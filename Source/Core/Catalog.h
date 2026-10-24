#pragma once

#include <juce_events/juce_events.h>

namespace wt
{
struct CategoryInfo
{
    juce::String name; // folder / file-name part, English
    juce::String jp;   // Japanese alias shown next to it
    juce::String note;
    juce::String prefix; // optional file-name prefix for this Category (empty = use the Category name in Title case)
};

struct TypeInfo
{
    juce::String name; // used in file names, English
    juce::String jp;
    juce::String note;
};

/**
    Category / Type vocabulary.

    The master is catalog.xlsx (sheets "Categories" and "Types"), which Excel opens and saves without any
    encoding trouble. categories.csv / types.csv (UTF-8 with BOM) are generated from it automatically for
    other tools and AIs. If a CSV is edited directly instead, its content is merged back into the workbook
    (the old workbook is kept as catalog.xlsx.bak-<time>). Excel's plain "CSV" (Shift-JIS) is read correctly too.

      Categories : Category, Japanese, Note, Prefix, TYPE01 ... TYPE10   (the Types of a Category go on its row; blanks and repeats are fine)
      Types      : Category, Type, Japanese, Note        (the Japanese for each Type; Category "*" = common to every category)
    A Type written in the matrix that has no row in Types yet gets one added (Japanese left empty to be filled in).
*/
class Catalog : public juce::ChangeBroadcaster
{
public:
    static constexpr const char* commonCategory = "*";

    /** Folder suggestion when none is saved: the shared Drive folder if there is one, else G:\SFX_Library\_config, else Documents. */
    static juce::File defaultDirectory();
    /** D:\マイドライブ\WaveTagger\config (Google Drive mirror, same path on every PC); empty if no Drive folder exists. */
    static juce::File sharedDirectory();
    /** Copies catalog.xlsx / CSVs / preferences.json that the target does not already have. Never overwrites. Returns the count. */
    static int copyConfigFiles (const juce::File& from, const juce::File& to);

    void setDirectory (const juce::File& dir);
    const juce::File& getDirectory() const { return directory; }
    juce::File getWorkbookFile() const { return directory.getChildFile ("catalog.xlsx"); }
    juce::File getCategoriesFile() const { return directory.getChildFile ("categories.csv"); }
    juce::File getTypesFile() const { return directory.getChildFile ("types.csv"); }
    /** Settings shared between PCs (keys, wheel, colours, naming); lives next to the workbook so a Drive mirror carries it. */
    juce::File getPreferencesFile() const { return directory.getChildFile ("preferences.json"); }
    /** The file a human should edit (the workbook once it exists). */
    juce::File getEditTarget() const { return getWorkbookFile().existsAsFile() ? getWorkbookFile() : getCategoriesFile(); }

    /** Cheap modification-time check; syncs, reloads and notifies if any of the files changed. */
    bool reloadIfChanged();
    void reload();

    const std::vector<CategoryInfo>& getCategories() const { return categories; }
    /** Types registered for this category (then the common ones if includeCommon). */
    std::vector<TypeInfo> getTypes (const juce::String& category, bool includeCommon = true) const;
    /** The Japanese for a Type name: this Category's own entry first, then any other (empty if none was written yet). */
    juce::String japaneseForType (const juce::String& category, const juce::String& name) const;
    juce::StringArray getWarnings() const { return warnings; }
    /** What the last sync did (e.g. "types.csv was regenerated"); informational. */
    juce::StringArray getNotices() const { return notices; }

    /** Adds a row to the workbook (or the CSV if there is no workbook) and reloads. False if it could not be written. */
    bool addCategory (const juce::String& name, const juce::String& jp);
    bool addType (const juce::String& category, const juce::String& name, const juce::String& jp);

    void noteUsed (const juce::String& category);
    /** Indices into getCategories(), most used first. */
    std::vector<int> frequentIndices (int maxCount) const;
    juce::var usageToVar() const;
    void usageFromVar (const juce::var& v);

private:
    void syncAndLoad (std::vector<juce::StringArray>& categoryRows, std::vector<juce::StringArray>& typeRows);
    bool appendRow (bool toTypes, const juce::StringArray& fields);
    juce::String currentStamp() const;
    void pruneBackups() const;
    void detectConflictCopies();

    juce::File directory;
    std::vector<CategoryInfo> categories;
    std::vector<std::pair<juce::String, TypeInfo>> types; // category -> type
    juce::StringArray warnings, notices;
    std::map<juce::String, int> usage;
    bool fillingMissing = false;
    juce::String loadedStamp, categorySheetName { "Categories" }, typeSheetName { "Types" };
};
} // namespace wt
