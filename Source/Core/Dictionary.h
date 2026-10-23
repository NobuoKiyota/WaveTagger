#pragma once

#include <juce_core/juce_core.h>
#include <optional>

namespace wt
{
struct DictionaryEntry
{
    juce::String english, japanese, note;
    bool user = false; // false: built in
};

/**
    A small offline English <-> Japanese dictionary for the words people put into attributes. A few built-in words, plus the
    translations the user has confirmed: those live in dictionary.csv (English,Japanese,Note) in the shared config folder, which
    Excel can edit, and take precedence. Words are matched whole (ignoring case, white space and full-width letters); nothing is
    ever guessed from parts of a phrase.
*/
class Dictionary
{
public:
    Dictionary();
    // copyable on purpose: a copy is a snapshot a worker thread can read while the original is edited

    void setFile (const juce::File& file);
    const juce::File& getFile() const { return file; }

    /** Cheap modification-time check; re-reads dictionary.csv when it changed. True if it did. */
    bool reloadIfChanged();
    void reload();

    /** Empty optional: no translation known. A word that is already in the target language is returned as it is. */
    std::optional<juce::String> toJapanese (const juce::String& word) const;
    std::optional<juce::String> toEnglish (const juce::String& word) const;

    /** Kana or kanji anywhere in the text. */
    static bool isJapanese (const juce::String& text);
    /** Nothing to translate: no letters, kana or kanji (numbers, "3m", symbols). */
    static bool isNeutral (const juce::String& text);

    /** User entries first, then the built-in ones. */
    const std::vector<DictionaryEntry>& entries() const { return all; }
    int userCount() const { return numUser; }

    /** Adds the translation or replaces the user's one for the same English word, and saves dictionary.csv. False if it could not be written. */
    bool setUserEntry (const juce::String& english, const juce::String& japanese, const juce::String& note = {});
    /** Removes the user's entry for this English word (built-in words cannot be removed) and saves. */
    bool removeUserEntry (const juce::String& english);

private:
    void rebuild();
    bool save();

    juce::File file;
    juce::Time loadedTime;
    juce::int64 loadedSize = -1;
    std::vector<DictionaryEntry> builtin, userEntries, all;
    int numUser = 0;
    std::map<juce::String, juce::String> toJa, toEn; // normalised word -> the other language
};
} // namespace wt
