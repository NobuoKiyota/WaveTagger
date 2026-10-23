#pragma once

#include "Core/CycleModel.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace wt
{
/** Header strip: prefix / naming template and the 9 fixed marker-type slots (keys 1-9). */
class PalettePanel : public juce::Component,
                     private juce::ChangeListener
{
public:
    static constexpr int height = 74;

    explicit PalettePanel (CycleModel& model);
    ~PalettePanel() override;

    /** A slot's key area was clicked: apply that type like pressing its number key. */
    std::function<void (int slot)> onSlotClicked;
    /** Return / Escape pressed in one of the text fields: hand focus back to the main window. */
    std::function<void()> onEditingFinished;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    class Slot;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refresh();

    CycleModel& model;
    juce::Label prefixLabel, patternLabel, exampleLabel;
    juce::TextEditor prefixEditor, patternEditor;
    juce::OwnedArray<Slot> slots;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PalettePanel)
};
} // namespace wt
