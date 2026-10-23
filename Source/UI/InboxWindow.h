#pragma once

#include "Core/InboxQueue.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace wt
{
/** What the inbox brought in, newest first, with the controls that start / stop the watcher. */
class InboxPanel : public juce::Component,
                   private juce::TableListBoxModel,
                   private juce::Timer
{
public:
    struct Hooks
    {
        std::function<juce::File()> getInbox, getRaw;
        std::function<bool()> isWatching;
        std::function<juce::String()> getStatus;
        std::function<void()> chooseInbox, chooseRaw, scanNow;
        std::function<void (bool)> setWatching;
        std::function<void (const QueueItem&)> openItem, retryItem;
        std::function<void()> queueChanged; // after "reviewed" / "remove": save and update the toolbar badge
    };

    InboxPanel (InboxQueue& queue, Hooks hooks);
    ~InboxPanel() override;

    void refresh();

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    int getNumRows() override { return (int) displayOrder.size(); }
    void paintRowBackground (juce::Graphics&, int row, int width, int height, bool selected) override;
    void paintCell (juce::Graphics&, int row, int column, int width, int height, bool selected) override;
    void cellDoubleClicked (int row, int column, const juce::MouseEvent&) override;
    void timerCallback() override { refresh(); }

    const QueueItem* selectedItem() const;
    int selectedIndex() const; // index into the queue (the table may show a different order)

    InboxQueue& queue;
    Hooks hooks;

    juce::Label inboxLabel, rawLabel, statusLabel, inboxPath, rawPath;
    juce::TextButton chooseInboxButton, chooseRawButton, scanButton;
    juce::ToggleButton watchToggle, orderToggle;
    std::vector<int> displayOrder;
    juce::TextButton openButton, reviewedButton, retryButton, removeButton, revealButton;
    juce::TableListBox table { "inbox", this };
    int lastSize = -1, lastNew = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InboxPanel)
};

class InboxWindow : public juce::DocumentWindow
{
public:
    InboxWindow (InboxQueue& queue, InboxPanel::Hooks hooks);
    InboxPanel& getPanel() { return *panel; }
    void closeButtonPressed() override { setVisible (false); }

private:
    InboxPanel* panel = nullptr;
};
} // namespace wt
