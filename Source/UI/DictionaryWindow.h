#pragma once

#include "Core/Dictionary.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace wt
{
/** The translation dictionary: every word (the user's first, then the built-in ones), a form to add or replace a translation,
    and the words of the open recording that still have none. Built-in words are never changed: a different translation entered
    for one becomes the user's own entry, which wins. */
class DictionaryPanel : public juce::Component,
                        private juce::TableListBoxModel
{
public:
    struct Hooks
    {
        std::function<juce::StringArray()> untranslated; // words of the open recording without a translation
        std::function<void()> changed;                   // a translation was saved or removed
    };

    DictionaryPanel (Dictionary& dictionary, Hooks hooks);

    void refresh();
    /** Puts a word into the form: the known side filled in, the other one left for typing. */
    void prepare (const juce::String& word);

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    int getNumRows() override { return (int) shown.size(); }
    void paintRowBackground (juce::Graphics&, int row, int width, int height, bool selected) override;
    void paintCell (juce::Graphics&, int row, int column, int width, int height, bool selected) override;
    void selectedRowsChanged (int lastRow) override;

    void rebuildList();
    void save();
    void removeSelected();
    void showUntranslated();
    void say (const juce::String& text, bool problem = false);

    Dictionary& dictionary;
    Hooks hooks;
    std::vector<int> shown; // indices into dictionary.entries() that match the filter

    juce::Label filterLabel, englishLabel, japaneseLabel, status;
    juce::TextEditor filter, english, japanese;
    juce::TextButton saveButton, removeButton, missingButton;
    juce::TableListBox table { "dictionary", this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DictionaryPanel)
};

class DictionaryWindow : public juce::DocumentWindow
{
public:
    DictionaryWindow (Dictionary& dictionary, DictionaryPanel::Hooks hooks);
    DictionaryPanel& getPanel() { return *panel; }
    void closeButtonPressed() override { setVisible (false); }

private:
    DictionaryPanel* panel = nullptr;
};
} // namespace wt
