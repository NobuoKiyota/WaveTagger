#pragma once

#include "Core/Catalog.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace wt
{
/**
    Accordion list of categories grouped by initial letter (optionally merged into A-E style blocks),
    with a "frequently used" block, live search (English or Japanese) and shortcuts to the CSV files.
*/
class CategoryPicker : public juce::Component
{
public:
    CategoryPicker (Catalog& catalog, const juce::String& current,
                    std::function<void (const juce::String&)> onPick, std::function<void()> onAdd);

    ~CategoryPicker() override;

    void resized() override;
    void paint (juce::Graphics& g) override;
    void parentHierarchyChanged() override;

private:
    struct Row
    {
        bool header = false;
        juce::String label, jp, key;
        int count = 0;
        bool expanded = false;
        bool current = false;
    };

    class RowList;

    void rebuild();
    void dismiss();

    Catalog& catalog;
    juce::String currentCategory;
    std::function<void (const juce::String&)> pick;
    std::function<void()> addRequested;

    juce::TextEditor search;
    juce::Viewport viewport;
    std::unique_ptr<RowList> list;
    juce::ToggleButton mergeToggle { "A-E" };
    juce::TextButton addButton, openButton, reloadButton;
    juce::Label footer;
    std::vector<Row> rows;
    bool firstBuild = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CategoryPicker)
};
} // namespace wt
