#pragma once

#include "Core/Catalog.h"
#include "Core/Clustering.h"
#include "Core/FeatureStore.h"
#include "Core/HardLinks.h"
#include "Core/PlaybackEngine.h"
#include "Core/ReclassDb.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace wt
{
struct OrganizerHooks
{
    std::function<juce::StringArray()> getRoots;
    std::function<juce::File()> getOutputDir;
    std::function<void (const juce::File&)> setOutputDir;
};

/**
    Sorts legacy files into groups of similar sounds, lets the user name each group (Category / Type), and records the
    answer in the re-classification database. The legacy files are never renamed or moved; an organised view can be
    built from them as hard links, and undone.
*/
class OrganizerPanel : public juce::Component,
                       private juce::Timer
{
public:
    OrganizerPanel (Catalog& catalog, FeatureStore& store, juce::AudioFormatManager& formats, OrganizerHooks hooks);
    ~OrganizerPanel() override;

    /** For screenshots and tests: analyse (synchronously), group, and assign the first group. */
    void runDemo (const juce::File& folder, const juce::String& category, const juce::String& type);

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    class GroupsModel;
    class FilesModel;
    class LayoutTab;

    // grouping tab
    void chooseFolder();
    void loadFolder (const juce::File& dir);
    void startAnalysis();
    void runGrouping();
    void suggestGroups();
    void selectGroup (int group);
    juce::Array<int> selectedFileIndices() const;
    void assign (const juce::Array<int>& fileIndices);
    void unassign (const juce::Array<int>& fileIndices);
    void moveToGroup (const juce::Array<int>& fileIndices, int group);
    void pickCategory();
    void pickTypeFromMenu();
    void audition (const juce::File& f);
    void refreshGroups();
    void updateStatus();

    // organised-view tab
    void refreshOutput();
    void previewPlan();
    void createView();
    void undoView();
    void exportExcel();
    void chooseOutput();
    juce::File manifestFile() const;
    std::vector<AssignedFile> resolveAssigned() const;

    void timerCallback() override;

    Catalog& catalog;
    FeatureStore& store;
    juce::AudioFormatManager& formats;
    OrganizerHooks hooks;
    FeatureAnalyzer analyzer { formats, store };
    ReclassDb db;

    juce::File folder;
    std::vector<juce::File> files;
    std::vector<int> assignment;      // group per file (-1 = not grouped yet)
    std::vector<float> distance;
    std::vector<std::vector<int>> groupMembers; // file indices, most typical first
    int groupCount = 0, currentGroup = -1;
    juce::String category;

    std::unique_ptr<GroupsModel> groupsModel;
    std::unique_ptr<FilesModel> filesModel;
    juce::ListBox groupsList;
    juce::TableListBox filesTable;

    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    std::unique_ptr<LayoutTab> groupingTab, outputTab;

    juce::Label folderLabel, statusLabel, kLabel, categoryLabel, outputPath, outputSummary;
    juce::TextButton folderButton, analyzeButton, autoKButton, groupButton, categoryButton, typeMenuButton,
        assignGroupButton, assignSelectedButton, unassignButton, moveButton;
    juce::TextEditor typeEditor, planText;
    juce::Slider kSlider;
    juce::TextButton outputButton, previewButton, createButton, undoButton, excelButton;

    juce::ThreadPool jobs { juce::ThreadPoolOptions {}.withNumberOfThreads (1) };
    std::unique_ptr<PlaybackEngine> player;
    std::atomic<int> auditionGeneration { 0 };
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OrganizerPanel)
};

class OrganizerWindow : public juce::DocumentWindow
{
public:
    OrganizerWindow (Catalog& catalog, FeatureStore& store, juce::AudioFormatManager& formats, OrganizerHooks hooks);
    void closeButtonPressed() override { setVisible (false); }

private:
    OrganizerPanel* panel = nullptr;
};
} // namespace wt
