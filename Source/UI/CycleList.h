#pragma once

#include "Core/CycleModel.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace wt
{
/** Cycle table: type dropdown per row, start / length, resulting file name. Selecting a row previews it. */
class CycleList : public juce::Component,
                  private juce::TableListBoxModel,
                  private juce::ChangeListener
{
public:
    explicit CycleList (CycleModel& model);
    ~CycleList() override;

    void setSampleRate (double rate) { sampleRate = rate; table.repaint(); }

    juce::Array<int> getSelectedRows() const;
    void selectRows (const juce::Array<int>& rows);
    void selectRow (int row);

    /** A single row was selected or clicked: preview it. */
    std::function<void (int row)> onPreview;
    std::function<void (const juce::Array<int>& rows, int type)> onTypeChosen;
    std::function<void()> onDeleteKey;
    std::function<void (int row)> onReturnKey;
    std::function<void()> onSelectionChanged;

    void resized() override { table.setBounds (getLocalBounds()); }

private:
    class Table;
    class TypeCell;

    int getNumRows() override { return model.size(); }
    void paintRowBackground (juce::Graphics&, int row, int width, int height, bool selected) override;
    void paintCell (juce::Graphics&, int row, int column, int width, int height, bool selected) override;
    juce::Component* refreshComponentForCell (int row, int column, bool selected, juce::Component* existing) override;
    void cellClicked (int row, int column, const juce::MouseEvent&) override;
    void selectedRowsChanged (int lastRow) override;
    void deleteKeyPressed (int) override { if (onDeleteKey) onDeleteKey(); }
    void returnKeyPressed (int row) override { if (onReturnKey) onReturnKey (row); }

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void firePreview (int row);

    CycleModel& model;
    std::unique_ptr<Table> tableOwner;
    juce::TableListBox& table;
    double sampleRate = 48000.0;
    bool suppressCallbacks = false;
    juce::uint32 lastPreviewTick = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CycleList)
};
} // namespace wt
