#include "LibraryPanel.h"
#include "Core/AudioData.h"
#include "Theme.h"
#include "Util/Text.h"

namespace wt
{
namespace
{
constexpr int maxHits = 3000;
enum Column { colName = 1, colFolder, colSize };

juce::String formatSize (juce::int64 bytes)
{
    if (bytes >= 1024 * 1024)
        return juce::String ((double) bytes / (1024.0 * 1024.0), 1) + " MB";
    return juce::String ((bytes + 1023) / 1024) + " KB";
}
} // namespace

LibraryPanel::LibraryPanel (LibraryIndex& idx)
    : index (idx)
{
    formats.registerBasicFormats();

    searchEditor.setTextToShowWhenEmpty (WT_U8 ("検索 (スペース区切りで AND)  例: tape 03 / masking"), theme::textDim);
    searchEditor.setFont (juce::FontOptions (15.0f));
    searchEditor.setColour (juce::TextEditor::backgroundColourId, theme::panelLight);
    searchEditor.setColour (juce::TextEditor::outlineColourId, theme::outline);
    searchEditor.setColour (juce::TextEditor::textColourId, theme::text);
    searchEditor.onTextChange = [this] { pendingSearchTicks = 1; startTimerHz (20); };
    addAndMakeVisible (searchEditor);

    for (auto* b : { &rescanButton, &addButton, &revealButton })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }
    rescanButton.onClick = [this] { index.rescan(); updateStatus(); };
    addButton.onClick = [this] { addFolder(); };
    revealButton.onClick = [this] {
        const auto row = table.getSelectedRow();
        if (row >= 0 && row < (int) hits.size())
            juce::File (data->entries[(size_t) hits[(size_t) row]].path).revealToUser();
    };

    statusLabel.setColour (juce::Label::textColourId, theme::textDim);
    addAndMakeVisible (statusLabel);

    table.setRowHeight (22);
    table.setColour (juce::ListBox::backgroundColourId, theme::background);
    table.setColour (juce::ListBox::outlineColourId, theme::outline);
    table.setOutlineThickness (1);
    auto& header = table.getHeader();
    header.setColour (juce::TableHeaderComponent::backgroundColourId, theme::panel);
    header.setColour (juce::TableHeaderComponent::textColourId, theme::textDim);
    header.addColumn ("Name", colName, 300, 120, 700, juce::TableHeaderComponent::notSortable);
    header.addColumn ("Folder", colFolder, 520, 120, 1500, juce::TableHeaderComponent::notSortable);
    header.addColumn ("Size", colSize, 80, 60, 140, juce::TableHeaderComponent::notSortable);
    header.setStretchToFitActive (true);
    addAndMakeVisible (table);

    index.addChangeListener (this);
    data = index.getData();
    updateStatus();
    startTimerHz (4);
}

LibraryPanel::~LibraryPanel()
{
    stopTimer();
    index.removeChangeListener (this);
    loader.removeAllJobs (true, 5000);
    table.setModel (nullptr);
}

void LibraryPanel::visibilityChanged()
{
    if (isShowing())
        searchEditor.grabKeyboardFocus();
    else if (player != nullptr)
        player->stop();
}

void LibraryPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
}

void LibraryPanel::resized()
{
    auto r = getLocalBounds().reduced (10, 8);
    auto top = r.removeFromTop (28);
    revealButton.setBounds (top.removeFromRight (130));
    top.removeFromRight (6);
    addButton.setBounds (top.removeFromRight (110));
    top.removeFromRight (6);
    rescanButton.setBounds (top.removeFromRight (80));
    top.removeFromRight (10);
    searchEditor.setBounds (top);

    r.removeFromTop (4);
    statusLabel.setBounds (r.removeFromTop (22));
    table.setBounds (r);
}

void LibraryPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    data = index.getData();
    runSearch();
    updateStatus();
}

void LibraryPanel::timerCallback()
{
    if (pendingSearchTicks > 0 && --pendingSearchTicks == 0)
    {
        runSearch();
        startTimerHz (4);
    }
    updateStatus();
}

void LibraryPanel::runSearch()
{
    hits = data->search (searchEditor.getText(), maxHits);
    truncated = (int) hits.size() >= maxHits;
    table.updateContent();
    table.repaint();
    updateStatus();
}

void LibraryPanel::updateStatus()
{
    juce::String s;
    if (index.isScanning())
        s = WT_U8 ("スキャン中... ") + juce::String (index.getScannedCount()) + WT_U8 (" 件");
    else
        s = juce::String ((int) data->entries.size()) + WT_U8 (" ファイル / ") + juce::String ((int) data->bases.size()) + WT_U8 (" 種類の名前");

    if (searchEditor.getText().isNotEmpty())
        s += "   |   " + juce::String ((int) hits.size()) + WT_U8 (" 件ヒット") + (truncated ? WT_U8 (" (上限)") : juce::String());

    s += "   |   " + data->roots.joinIntoString ("  ");
    statusLabel.setText (s, juce::dontSendNotification);
}

//==============================================================================
void LibraryPanel::paintRowBackground (juce::Graphics& g, int row, int, int, bool selected)
{
    g.fillAll (selected ? theme::accent.withAlpha (0.32f) : (row % 2 == 0 ? theme::background : theme::background.brighter (0.03f)));
}

void LibraryPanel::paintCell (juce::Graphics& g, int row, int column, int width, int height, bool)
{
    if (row >= (int) hits.size())
        return;

    const auto& e = data->entries[(size_t) hits[(size_t) row]];
    juce::String text;
    switch (column)
    {
        case colName:   text = e.name; break;
        case colFolder: text = juce::File (e.path).getParentDirectory().getFullPathName(); break;
        case colSize:   text = formatSize (e.size); break;
        default:        return;
    }

    g.setColour (column == colName ? theme::text : theme::textDim.brighter (0.2f));
    g.setFont (juce::FontOptions ("Meiryo UI", 13.0f, juce::Font::plain));
    g.drawText (text, 6, 0, width - 10, height, juce::Justification::centredLeft, true);
}

void LibraryPanel::selectedRowsChanged (int row)
{
    if (row >= 0 && row < (int) hits.size())
        audition (juce::File (data->entries[(size_t) hits[(size_t) row]].path));
}

void LibraryPanel::cellDoubleClicked (int row, int, const juce::MouseEvent&)
{
    if (row >= 0 && row < (int) hits.size())
        juce::File (data->entries[(size_t) hits[(size_t) row]].path).revealToUser();
}

void LibraryPanel::audition (const juce::File& file)
{
    if (player == nullptr)
        player = std::make_unique<PlaybackEngine>();
    player->stop();

    if (file.getSize() > 120 * 1024 * 1024)
        return;

    const auto generation = ++auditionGeneration;
    juce::Component::SafePointer<LibraryPanel> safe (this);
    loader.addJob ([this, safe, file, generation] {
        if (generation != auditionGeneration)
            return;
        juce::String error;
        auto audio = loadAudioFile (formats, file, error);
        juce::MessageManager::callAsync ([safe, audio, generation] {
            if (safe == nullptr || audio == nullptr || generation != safe->auditionGeneration)
                return;
            safe->player->setAudio (audio);
            safe->player->play (0, -1, false);
        });
    });
}

void LibraryPanel::addFolder()
{
    chooser = std::make_unique<juce::FileChooser> (WT_U8 ("索引に追加するフォルダ (読み取りのみ)"));
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc) {
                              const auto dir = fc.getResult();
                              if (! dir.isDirectory())
                                  return;

                              auto roots = index.getRoots();
                              roots.addIfNotAlreadyThere (dir.getFullPathName());
                              if (onRootsChanged)
                                  onRootsChanged (roots);
                          });
}

//==============================================================================
LibraryWindow::LibraryWindow (LibraryIndex& index)
    : DocumentWindow (WT_U8 ("ライブラリ検索"), theme::background, DocumentWindow::closeButton | DocumentWindow::minimiseButton)
{
    setUsingNativeTitleBar (true);
    panel = new LibraryPanel (index);
    setContentOwned (panel, false);
    setResizable (true, false);
    setResizeLimits (640, 360, 4000, 3000);
    centreWithSize (1000, 640);
}
} // namespace wt
