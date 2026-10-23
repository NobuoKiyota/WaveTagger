#pragma once

#include "Core/Catalog.h"
#include "Core/CycleModel.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace wt
{
/** Header strip: prefix / naming template, category picker and the 9 fixed marker-type slots (keys 1-9). */
class PalettePanel : public juce::Component,
                     private juce::ChangeListener
{
public:
    static constexpr int height = 74;

    PalettePanel (CycleModel& model, Catalog& catalog);
    ~PalettePanel() override;

    /** A slot's key area was clicked: apply that type like pressing its number key. */
    std::function<void (int slot)> onSlotClicked;
    /** Return / Escape pressed in one of the text fields: hand focus back to the main window. */
    std::function<void()> onEditingFinished;

    /** A Category was picked (or created): the owner decides what that changes (Prefix, Type names ...). */
    std::function<void (const juce::String& category)> onCategoryChosen;

    /** Existing-count summary and similar-name warning coming from the library index. */
    void setLibraryInfo (const juce::String& summary, const juce::String& warning);

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    class Slot;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refresh();

    void showCategoryPicker();
    void showTypeMenu (int slot, juce::Rectangle<int> screenArea);
    void fillSlotsFromCategory();
    void promptAddCategory();
    void promptAddType (int slot);
    void reportWriteFailure (const juce::File& file);

    CycleModel& model;
    Catalog& catalog;
    juce::Label prefixLabel, patternLabel, exampleLabel, summaryLabel, warningLabel;
    juce::TextEditor prefixEditor, patternEditor;
    juce::TextButton categoryButton, fillButton;
    juce::OwnedArray<Slot> slots;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PalettePanel)
};
} // namespace wt
