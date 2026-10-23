#include "InboxWindow.h"
#include "Theme.h"
#include "Util/Text.h"

namespace wt
{
namespace
{
enum Column { colStatus = 1, colName, colTime, colCycles, colHint, colMessage };

juce::Colour statusColour (const juce::String& s)
{
    return s == "new" ? juce::Colour (0xff6fd06f) : s == "failed" ? juce::Colour (0xffe05a6c) : theme::textDim;
}

juce::String statusText (const juce::String& s)
{
    return s == "new" ? WT_U8 ("新規") : s == "failed" ? WT_U8 ("失敗") : WT_U8 ("確認済");
}
} // namespace

InboxPanel::InboxPanel (InboxQueue& q, Hooks h)
    : queue (q), hooks (std::move (h))
{
    inboxLabel.setText (WT_U8 ("受信フォルダ"), juce::dontSendNotification);
    rawLabel.setText (WT_U8 ("Raw フォルダ"), juce::dontSendNotification);
    for (auto* l : { &inboxLabel, &rawLabel })
    {
        l->setColour (juce::Label::textColourId, theme::textDim);
        addAndMakeVisible (l);
    }

    for (auto* l : { &inboxPath, &rawPath })
    {
        l->setColour (juce::Label::textColourId, theme::text);
        l->setColour (juce::Label::backgroundColourId, theme::panelLight);
        l->setBorderSize ({ 3, 8, 3, 8 });
        addAndMakeVisible (l);
    }

    chooseInboxButton.setButtonText (WT_U8 ("選択..."));
    chooseRawButton.setButtonText (WT_U8 ("選択..."));
    scanButton.setButtonText (WT_U8 ("今すぐ確認"));
    chooseInboxButton.onClick = [this] { if (hooks.chooseInbox) hooks.chooseInbox(); };
    chooseRawButton.onClick = [this] { if (hooks.chooseRaw) hooks.chooseRaw(); };
    scanButton.onClick = [this] { if (hooks.scanNow) hooks.scanNow(); };
    for (auto* b : { &chooseInboxButton, &chooseRawButton, &scanButton })
        addAndMakeVisible (b);

    watchToggle.setButtonText (WT_U8 ("受信フォルダを監視して自動で取り込む"));
    watchToggle.onClick = [this] { if (hooks.setWatching) hooks.setWatching (watchToggle.getToggleState()); };
    addAndMakeVisible (watchToggle);

    statusLabel.setColour (juce::Label::textColourId, theme::textDim);
    addAndMakeVisible (statusLabel);

    table.setRowHeight (24);
    table.setColour (juce::ListBox::backgroundColourId, theme::background);
    table.setColour (juce::ListBox::outlineColourId, theme::outline);
    table.setOutlineThickness (1);
    auto& header = table.getHeader();
    header.setColour (juce::TableHeaderComponent::backgroundColourId, theme::panel);
    header.setColour (juce::TableHeaderComponent::textColourId, theme::textDim);
    header.addColumn (WT_U8 ("状態"), colStatus, 64, 50, 100, juce::TableHeaderComponent::notSortable);
    header.addColumn (WT_U8 ("ファイル"), colName, 300, 120, 700, juce::TableHeaderComponent::notSortable);
    header.addColumn (WT_U8 ("取り込み時刻"), colTime, 110, 90, 160, juce::TableHeaderComponent::notSortable);
    header.addColumn (WT_U8 ("サイクル"), colCycles, 70, 50, 100, juce::TableHeaderComponent::notSortable);
    header.addColumn (WT_U8 ("ラベル"), colHint, 150, 80, 300, juce::TableHeaderComponent::notSortable);
    header.addColumn (WT_U8 ("メッセージ"), colMessage, 320, 100, 900, juce::TableHeaderComponent::notSortable);
    header.setStretchToFitActive (true);
    addAndMakeVisible (table);

    openButton.setButtonText (WT_U8 ("エディタで開く"));
    reviewedButton.setButtonText (WT_U8 ("確認済みにする"));
    retryButton.setButtonText (WT_U8 ("失敗を再試行"));
    removeButton.setButtonText (WT_U8 ("一覧から外す"));
    revealButton.setButtonText (WT_U8 ("場所を開く"));
    for (auto* b : { &openButton, &reviewedButton, &retryButton, &removeButton, &revealButton })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }

    openButton.onClick = [this] {
        if (const auto* item = selectedItem(); item != nullptr && hooks.openItem)
            hooks.openItem (*item);
    };
    reviewedButton.onClick = [this] {
        const auto row = table.getSelectedRow();
        if (row >= 0)
        {
            queue.setStatus (row, "reviewed");
            if (hooks.queueChanged)
                hooks.queueChanged();
            refresh();
        }
    };
    retryButton.onClick = [this] {
        if (const auto* item = selectedItem(); item != nullptr && item->status == "failed" && hooks.retryItem)
            hooks.retryItem (*item);
    };
    removeButton.onClick = [this] {
        const auto row = table.getSelectedRow();
        if (row >= 0)
        {
            queue.remove (row);
            if (hooks.queueChanged)
                hooks.queueChanged();
            refresh();
        }
    };
    revealButton.onClick = [this] {
        if (const auto* item = selectedItem())
            juce::File (item->raw.isNotEmpty() ? item->raw : item->original).revealToUser();
    };

    refresh();
    startTimerHz (2);
}

InboxPanel::~InboxPanel()
{
    stopTimer();
    table.setModel (nullptr);
}

const QueueItem* InboxPanel::selectedItem() const
{
    const auto row = table.getSelectedRow();
    return row >= 0 && row < queue.size() ? &queue.getItems()[(size_t) row] : nullptr;
}

void InboxPanel::refresh()
{
    if (hooks.getInbox)
        inboxPath.setText (hooks.getInbox().getFullPathName().isEmpty() ? WT_U8 ("(未設定)") : hooks.getInbox().getFullPathName(), juce::dontSendNotification);
    if (hooks.getRaw)
        rawPath.setText (hooks.getRaw().getFullPathName().isEmpty() ? WT_U8 ("(未設定)") : hooks.getRaw().getFullPathName(), juce::dontSendNotification);
    if (hooks.isWatching)
        watchToggle.setToggleState (hooks.isWatching(), juce::dontSendNotification);

    if (hooks.getStatus)
    {
        const auto s = hooks.getStatus();
        statusLabel.setText (s == "watching"     ? WT_U8 ("監視中 — 新しい録音を待っています")
                             : s == "stopped"    ? WT_U8 ("停止中")
                             : s.startsWith ("importing") ? WT_U8 ("取り込み中: ") + s.fromFirstOccurrenceOf ("importing ", false, false)
                             : s == "inbox folder not found" ? WT_U8 ("受信フォルダが見つかりません")
                                                          : s,
                             juce::dontSendNotification);
    }

    if (queue.size() != lastSize || queue.countNew() != lastNew)
    {
        lastSize = queue.size();
        lastNew = queue.countNew();
        table.updateContent();
    }
    table.repaint();
}

void InboxPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
}

void InboxPanel::resized()
{
    auto r = getLocalBounds().reduced (12, 10);

    auto row = r.removeFromTop (28);
    inboxLabel.setBounds (row.removeFromLeft (100));
    chooseInboxButton.setBounds (row.removeFromRight (80));
    row.removeFromRight (6);
    inboxPath.setBounds (row);

    r.removeFromTop (6);
    row = r.removeFromTop (28);
    rawLabel.setBounds (row.removeFromLeft (100));
    chooseRawButton.setBounds (row.removeFromRight (80));
    row.removeFromRight (6);
    rawPath.setBounds (row);

    r.removeFromTop (8);
    row = r.removeFromTop (28);
    scanButton.setBounds (row.removeFromRight (110));
    row.removeFromRight (10);
    statusLabel.setBounds (row.removeFromRight (juce::jmin (360, row.getWidth() / 2)));
    watchToggle.setBounds (row);

    r.removeFromTop (8);
    auto bottom = r.removeFromBottom (30);
    r.removeFromBottom (6);
    openButton.setBounds (bottom.removeFromLeft (120));
    bottom.removeFromLeft (6);
    reviewedButton.setBounds (bottom.removeFromLeft (120));
    bottom.removeFromLeft (6);
    retryButton.setBounds (bottom.removeFromLeft (110));
    bottom.removeFromLeft (6);
    removeButton.setBounds (bottom.removeFromLeft (110));
    bottom.removeFromLeft (6);
    revealButton.setBounds (bottom.removeFromLeft (100));

    table.setBounds (r);
}

void InboxPanel::paintRowBackground (juce::Graphics& g, int row, int, int, bool selected)
{
    g.fillAll (selected ? theme::accent.withAlpha (0.32f) : (row % 2 == 0 ? theme::background : theme::background.brighter (0.03f)));
}

void InboxPanel::paintCell (juce::Graphics& g, int row, int column, int width, int height, bool)
{
    if (row >= queue.size())
        return;

    const auto& q = queue.getItems()[(size_t) row];
    juce::String text;
    auto colour = theme::text;

    switch (column)
    {
        case colStatus:  text = statusText (q.status); colour = statusColour (q.status); break;
        case colName:    text = juce::File (q.raw.isNotEmpty() ? q.raw : q.original).getFileName(); break;
        case colTime:    text = juce::Time (q.importedAt).formatted ("%m/%d %H:%M"); colour = theme::textDim; break;
        case colCycles:  text = q.status == "failed" ? juce::String() : juce::String (q.cycles); colour = theme::textDim; break;
        case colHint:    text = q.category + (q.type.isNotEmpty() ? " / " + q.type : juce::String()); colour = theme::textDim; break;
        case colMessage: text = q.error; colour = q.status == "failed" ? juce::Colour (0xffe05a6c) : theme::textDim; break;
        default:         return;
    }

    g.setColour (colour);
    g.setFont (juce::FontOptions ("Meiryo UI", 13.0f, juce::Font::plain));
    g.drawText (text, 8, 0, width - 12, height, juce::Justification::centredLeft, true);
}

void InboxPanel::cellDoubleClicked (int row, int, const juce::MouseEvent&)
{
    if (row >= 0 && row < queue.size() && hooks.openItem)
        hooks.openItem (queue.getItems()[(size_t) row]);
}

//==============================================================================
InboxWindow::InboxWindow (InboxQueue& queue, InboxPanel::Hooks hooks)
    : DocumentWindow (WT_U8 ("Inbox — 受信フォルダの取り込み"), theme::background, DocumentWindow::closeButton | DocumentWindow::minimiseButton)
{
    setUsingNativeTitleBar (true);
    panel = new InboxPanel (queue, std::move (hooks));
    setContentOwned (panel, false);
    setResizable (true, false);
    setResizeLimits (760, 420, 3000, 2000);
    centreWithSize (1000, 560);
}
} // namespace wt
