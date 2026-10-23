#pragma once

#include "Core/LibraryIndex.h"
#include "Core/PlaybackEngine.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace wt
{
/** Search across every indexed root (legacy + new library), audition a hit by selecting it. */
class LibraryPanel : public juce::Component,
                     private juce::TableListBoxModel,
                     private juce::ChangeListener,
                     private juce::Timer
{
public:
    explicit LibraryPanel (LibraryIndex& index);
    ~LibraryPanel() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void visibilityChanged() override;

    /** Roots changed from the panel ("Add folder"). */
    std::function<void (const juce::StringArray&)> onRootsChanged;

private:
    int getNumRows() override { return (int) hits.size(); }
    void paintRowBackground (juce::Graphics&, int row, int width, int height, bool selected) override;
    void paintCell (juce::Graphics&, int row, int column, int width, int height, bool selected) override;
    void selectedRowsChanged (int lastRow) override;
    void cellDoubleClicked (int row, int column, const juce::MouseEvent&) override;
    void deleteKeyPressed (int) override {}

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    void runSearch();
    void updateStatus();
    void audition (const juce::File& file);
    void addFolder();

    LibraryIndex& index;
    IndexDataPtr data;
    std::vector<int> hits;

    juce::TextEditor searchEditor;
    juce::TextButton rescanButton { "Rescan" }, addButton { "Add folder..." }, revealButton { "Show in Explorer" };
    juce::Label statusLabel;
    juce::TableListBox table { "library", this };

    juce::AudioFormatManager formats;
    juce::ThreadPool loader { juce::ThreadPoolOptions {}.withNumberOfThreads (1) };
    std::unique_ptr<PlaybackEngine> player;
    std::atomic<int> auditionGeneration { 0 };
    std::unique_ptr<juce::FileChooser> chooser;
    int pendingSearchTicks = 0;
    int lastGeneration = -1;
    bool truncated = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LibraryPanel)
};

class LibraryWindow : public juce::DocumentWindow
{
public:
    explicit LibraryWindow (LibraryIndex& index);
    LibraryPanel& getPanel() { return *panel; }
    void closeButtonPressed() override { setVisible (false); }

private:
    LibraryPanel* panel = nullptr;
};
} // namespace wt
