#pragma once

#include <juce_core/juce_core.h>

namespace wt
{
struct ReclassEntry
{
    juce::String key; // "<library root folder name>/<path below it>", so it also works on a PC where the drive letter differs
    juce::String category, type, group;
};

/**
    Which Category / Type each legacy file belongs to, decided by the user while going through the groups.
    This is the ONLY place that knowledge lives: the legacy files themselves are never renamed or moved, because
    Cubase projects point at them.
*/
class ReclassDb
{
public:
    static juce::String makeKey (const juce::StringArray& roots, const juce::File& file);
    /** The file on THIS PC that a key refers to (empty File if its root is not indexed here or the file is gone). */
    static juce::File resolve (const juce::StringArray& roots, const juce::String& key);

    void assign (const juce::String& key, const juce::String& category, const juce::String& type, const juce::String& group);
    void unassign (const juce::String& key) { entries.erase (key); }
    const ReclassEntry* find (const juce::String& key) const;
    const std::map<juce::String, ReclassEntry>& all() const { return entries; }
    size_t size() const { return entries.size(); }

    bool save (const juce::File& file) const;
    void load (const juce::File& file);

    /** Header + one row per entry, for an Excel sheet. */
    std::vector<juce::StringArray> toRows() const;

private:
    std::map<juce::String, ReclassEntry> entries;
};
} // namespace wt
