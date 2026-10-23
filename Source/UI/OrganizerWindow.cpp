#include "OrganizerWindow.h"
#include "CategoryPicker.h"
#include "Core/AudioData.h"
#include "Core/Persistence.h"
#include "Core/Xlsx.h"
#include "Theme.h"
#include "Util/Text.h"

namespace wt
{
namespace
{
const char* const audioPatterns = "*.wav;*.flac;*.aif;*.aiff;*.mp3;*.ogg";

juce::Colour groupColour (int g)
{
    static const juce::uint32 colours[] = { 0xff4fa3e0, 0xffe0894f, 0xff6cc070, 0xffc46bd1, 0xffe0cf4f, 0xff4fd1c4, 0xffe05a6c, 0xff9a8be0 };
    return juce::Colour (colours[(size_t) g % std::size (colours)]);
}

juce::String formatSeconds (double s) { return juce::String (s, s < 10.0 ? 2 : 1) + " s"; }
} // namespace

/** A tab body whose children are placed by a lambda (keeps the layout code next to the widgets it concerns). */
class OrganizerPanel::LayoutTab : public juce::Component
{
public:
    std::function<void()> layout;
    void paint (juce::Graphics& g) override { g.fillAll (theme::panel); }
    void resized() override { if (layout) layout(); }
};

class OrganizerPanel::GroupsModel : public juce::ListBoxModel
{
public:
    explicit GroupsModel (OrganizerPanel& o) : owner (o) {}

    int getNumRows() override { return owner.groupCount; }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (row < 0 || row >= owner.groupCount)
            return;

        g.fillAll (selected ? theme::accent.withAlpha (0.32f) : (row % 2 == 0 ? theme::background : theme::background.brighter (0.03f)));
        g.setColour (groupColour (row));
        g.fillRect (0, 3, 5, height - 6);

        // what the members have been assigned so far
        std::map<juce::String, int> tally;
        int assigned = 0;
        const auto roots = owner.hooks.getRoots();
        for (auto i : owner.groupMembers[(size_t) row])
            if (const auto* e = owner.db.find (ReclassDb::makeKey (roots, owner.files[(size_t) i])))
            {
                ++tally[e->category + " / " + e->type];
                ++assigned;
            }

        juce::String label;
        auto colour = theme::textDim;
        if (! tally.empty())
        {
            const auto best = std::max_element (tally.begin(), tally.end(), [] (const auto& a, const auto& b) { return a.second < b.second; });
            label = best->first + (assigned < (int) owner.groupMembers[(size_t) row].size() ? "  (" + juce::String (assigned) + "/" + juce::String ((int) owner.groupMembers[(size_t) row].size()) + ")" : juce::String());
            colour = theme::text;
        }
        else
        {
            label = WT_U8 ("未割り当て");
        }

        g.setColour (theme::text);
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawText ("G" + juce::String (row + 1) + "  (" + juce::String ((int) owner.groupMembers[(size_t) row].size()) + ")", 14, 0, 110, height, juce::Justification::centredLeft);
        g.setColour (colour);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (label, 124, 0, width - 130, height, juce::Justification::centredLeft, true);
    }

    void selectedRowsChanged (int row) override { owner.selectGroup (row); }

private:
    OrganizerPanel& owner;
};

class OrganizerPanel::FilesModel : public juce::TableListBoxModel
{
public:
    explicit FilesModel (OrganizerPanel& o) : owner (o) {}

    int getNumRows() override
    {
        return owner.currentGroup >= 0 && owner.currentGroup < owner.groupCount ? (int) owner.groupMembers[(size_t) owner.currentGroup].size() : 0;
    }

    void paintRowBackground (juce::Graphics& g, int row, int, int, bool selected) override
    {
        g.fillAll (selected ? theme::accent.withAlpha (0.32f) : (row % 2 == 0 ? theme::background : theme::background.brighter (0.03f)));
    }

    void paintCell (juce::Graphics& g, int row, int column, int width, int height, bool) override
    {
        if (row >= getNumRows())
            return;

        const auto fileIndex = owner.groupMembers[(size_t) owner.currentGroup][(size_t) row];
        const auto& file = owner.files[(size_t) fileIndex];
        juce::String text;
        auto colour = theme::text;

        switch (column)
        {
            case 1: text = file.getFileName(); break;
            case 2:
                if (const auto* r = owner.store.find (file))
                    text = formatSeconds (std::pow (10.0, (double) r->v[45]) - 0.01);
                colour = theme::textDim;
                break;
            case 3:
                if (const auto* e = owner.db.find (ReclassDb::makeKey (owner.hooks.getRoots(), file)))
                    text = e->category + " / " + e->type;
                colour = theme::accent.brighter (0.4f);
                break;
            case 4: text = juce::String (owner.distance[(size_t) fileIndex], 2); colour = theme::textDim; break;
            default: return;
        }

        g.setColour (colour);
        g.setFont (juce::FontOptions ("Meiryo UI", 13.0f, juce::Font::plain));
        g.drawText (text, 6, 0, width - 10, height, juce::Justification::centredLeft, true);
    }

    void selectedRowsChanged (int row) override
    {
        if (row >= 0 && row < getNumRows() && owner.filesTable.getNumSelectedRows() == 1)
            owner.audition (owner.files[(size_t) owner.groupMembers[(size_t) owner.currentGroup][(size_t) row]]);
    }

private:
    OrganizerPanel& owner;
};

class OrganizerPanel::CheckModel : public juce::TableListBoxModel
{
public:
    explicit CheckModel (OrganizerPanel& o) : owner (o) {}

    int getNumRows() override { return (int) owner.checkRows.size(); }

    void paintRowBackground (juce::Graphics& g, int row, int, int, bool selected) override
    {
        const auto shade = row < getNumRows() ? owner.checkRows[(size_t) row].group % 2 : 0;
        g.fillAll (selected ? theme::accent.withAlpha (0.32f) : (shade == 0 ? theme::background : theme::background.brighter (0.05f)));
    }

    void paintCell (juce::Graphics& g, int row, int column, int width, int height, bool) override
    {
        if (row >= getNumRows())
            return;

        const auto& r = owner.checkRows[(size_t) row];
        juce::String text;
        auto colour = theme::text;
        switch (column)
        {
            case 1: text = juce::String (r.group); colour = theme::textDim; break;
            case 2: text = r.kind; colour = theme::textDim; break;
            case 3: text = r.name; break;
            case 4: text = r.detail; colour = theme::textDim; break;
            case 5: text = r.location; colour = theme::textDim; break;
            default: return;
        }

        g.setColour (colour);
        g.setFont (juce::FontOptions ("Meiryo UI", 13.0f, juce::Font::plain));
        g.drawText (text, 6, 0, width - 10, height, juce::Justification::centredLeft, true);
    }

    void cellDoubleClicked (int row, int, const juce::MouseEvent&) override
    {
        if (row >= 0 && row < getNumRows() && owner.checkRows[(size_t) row].file != juce::File())
            owner.checkRows[(size_t) row].file.revealToUser();
    }

private:
    OrganizerPanel& owner;
};

//==============================================================================
OrganizerPanel::OrganizerPanel (Catalog& c, FeatureStore& s, juce::AudioFormatManager& f, OrganizerHooks h)
    : catalog (c), store (s), formats (f), hooks (std::move (h))
{
    db.load (catalog.getDirectory().getChildFile ("reclass.json"));

    groupsModel = std::make_unique<GroupsModel> (*this);
    filesModel = std::make_unique<FilesModel> (*this);
    groupsList.setModel (groupsModel.get());
    groupsList.setRowHeight (28);
    groupsList.setColour (juce::ListBox::backgroundColourId, theme::background);
    groupsList.setColour (juce::ListBox::outlineColourId, theme::outline);
    groupsList.setOutlineThickness (1);

    filesTable.setModel (filesModel.get());
    filesTable.setMultipleSelectionEnabled (true);
    filesTable.setRowHeight (24);
    filesTable.setColour (juce::ListBox::backgroundColourId, theme::background);
    filesTable.setColour (juce::ListBox::outlineColourId, theme::outline);
    filesTable.setOutlineThickness (1);
    auto& header = filesTable.getHeader();
    header.setColour (juce::TableHeaderComponent::backgroundColourId, theme::panel);
    header.setColour (juce::TableHeaderComponent::textColourId, theme::textDim);
    header.addColumn (WT_U8 ("ファイル名"), 1, 260, 120, 600, juce::TableHeaderComponent::notSortable);
    header.addColumn (WT_U8 ("長さ"), 2, 70, 50, 120, juce::TableHeaderComponent::notSortable);
    header.addColumn (WT_U8 ("割り当て済み"), 3, 180, 90, 400, juce::TableHeaderComponent::notSortable);
    header.addColumn (WT_U8 ("典型度(小さいほど代表的)"), 4, 90, 60, 200, juce::TableHeaderComponent::notSortable);
    header.setStretchToFitActive (true);

    groupingTab = std::make_unique<LayoutTab>();
    outputTab = std::make_unique<LayoutTab>();
    checkTab = std::make_unique<LayoutTab>();
    hashStore.load();

    // ---- grouping tab widgets
    folderButton.setButtonText (WT_U8 ("フォルダを選ぶ..."));
    analyzeButton.setButtonText (WT_U8 ("① 音を解析"));
    autoKButton.setButtonText (WT_U8 ("おすすめ"));
    groupButton.setButtonText (WT_U8 ("② グループ分け"));
    categoryButton.setButtonText ("Category: (" + juce::String (WT_U8 ("未選択")) + ") " + WT_U8 ("▾"));
    typeMenuButton.setButtonText (WT_U8 ("Type 候補 ▾"));
    assignGroupButton.setButtonText (WT_U8 ("このグループ全体に割り当て"));
    assignSelectedButton.setButtonText (WT_U8 ("選んだファイルだけに割り当て"));
    unassignButton.setButtonText (WT_U8 ("選んだファイルの割り当てを解除"));
    moveButton.setButtonText (WT_U8 ("選んだファイルを別グループへ ▾"));

    folderLabel.setColour (juce::Label::textColourId, theme::text);
    statusLabel.setColour (juce::Label::textColourId, theme::textDim);
    kLabel.setText (WT_U8 ("グループ数"), juce::dontSendNotification);
    kLabel.setColour (juce::Label::textColourId, theme::textDim);
    categoryLabel.setText ("Type:", juce::dontSendNotification);
    categoryLabel.setColour (juce::Label::textColourId, theme::textDim);

    kSlider.setRange (2.0, 30.0, 1.0);
    kSlider.setValue (3.0, juce::dontSendNotification);
    kSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    kSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 40, 22);

    typeEditor.setTextToShowWhenEmpty (WT_U8 ("Type 名 (英語、ファイル名に使います)"), theme::textDim);
    typeEditor.setColour (juce::TextEditor::backgroundColourId, theme::panelLight);
    typeEditor.setColour (juce::TextEditor::textColourId, theme::text);
    typeEditor.setColour (juce::TextEditor::outlineColourId, theme::outline);

    for (auto* comp : std::initializer_list<juce::Component*> { &folderLabel, &statusLabel, &kLabel, &categoryLabel, &folderButton, &analyzeButton,
                                                                &autoKButton, &groupButton, &categoryButton, &typeMenuButton, &assignGroupButton,
                                                                &assignSelectedButton, &unassignButton, &moveButton, &kSlider, &typeEditor, &groupsList, &filesTable })
        groupingTab->addAndMakeVisible (comp);

    folderButton.onClick = [this] { chooseFolder(); };
    analyzeButton.onClick = [this] { startAnalysis(); };
    autoKButton.onClick = [this] { suggestGroups(); };
    groupButton.onClick = [this] { runGrouping(); };
    categoryButton.onClick = [this] { pickCategory(); };
    typeMenuButton.onClick = [this] { pickTypeFromMenu(); };
    assignGroupButton.onClick = [this] {
        if (currentGroup >= 0 && currentGroup < groupCount)
        {
            juce::Array<int> all;
            for (auto i : groupMembers[(size_t) currentGroup])
                all.add (i);
            assign (all);
        }
    };
    assignSelectedButton.onClick = [this] { assign (selectedFileIndices()); };
    unassignButton.onClick = [this] { unassign (selectedFileIndices()); };
    moveButton.onClick = [this] {
        const auto selected = selectedFileIndices();
        if (selected.isEmpty() || groupCount < 2)
            return;

        juce::PopupMenu menu;
        for (int g = 0; g < groupCount; ++g)
            menu.addItem (g + 1, "G" + juce::String (g + 1) + "  (" + juce::String ((int) groupMembers[(size_t) g].size()) + ")", g != currentGroup);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&moveButton), [this, selected] (int r) {
            if (r > 0)
                moveToGroup (selected, r - 1);
        });
    };

    groupingTab->layout = [this] {
        auto r = groupingTab->getLocalBounds().reduced (10, 8);

        auto top = r.removeFromTop (30);
        folderButton.setBounds (top.removeFromLeft (130));
        top.removeFromLeft (8);
        analyzeButton.setBounds (top.removeFromLeft (110));
        top.removeFromLeft (8);
        kLabel.setBounds (top.removeFromLeft (70));
        kSlider.setBounds (top.removeFromLeft (190));
        autoKButton.setBounds (top.removeFromLeft (80));
        top.removeFromLeft (8);
        groupButton.setBounds (top.removeFromLeft (130));

        r.removeFromTop (4);
        folderLabel.setBounds (r.removeFromTop (22));
        statusLabel.setBounds (r.removeFromTop (22));
        r.removeFromTop (4);

        auto bottom = r.removeFromBottom (68);
        auto row1 = bottom.removeFromTop (30);
        categoryButton.setBounds (row1.removeFromLeft (230));
        row1.removeFromLeft (8);
        categoryLabel.setBounds (row1.removeFromLeft (44));
        typeEditor.setBounds (row1.removeFromLeft (220));
        row1.removeFromLeft (4);
        typeMenuButton.setBounds (row1.removeFromLeft (110));
        bottom.removeFromTop (6);
        auto row2 = bottom.removeFromTop (30);
        assignGroupButton.setBounds (row2.removeFromLeft (190));
        row2.removeFromLeft (6);
        assignSelectedButton.setBounds (row2.removeFromLeft (200));
        row2.removeFromLeft (6);
        unassignButton.setBounds (row2.removeFromLeft (230));
        row2.removeFromLeft (6);
        moveButton.setBounds (row2.removeFromLeft (250));

        r.removeFromBottom (6);
        groupsList.setBounds (r.removeFromLeft (juce::jmin (330, r.getWidth() / 3)));
        r.removeFromLeft (8);
        filesTable.setBounds (r);
    };

    // ---- output tab widgets
    outputButton.setButtonText (WT_U8 ("出力先を変更..."));
    previewButton.setButtonText (WT_U8 ("プレビュー"));
    createButton.setButtonText (WT_U8 ("整理ビューを作成 (ハードリンク)"));
    undoButton.setButtonText (WT_U8 ("作成したリンクを取り消す"));
    excelButton.setButtonText (WT_U8 ("割り当てを Excel に書き出し"));
    outputPath.setColour (juce::Label::textColourId, theme::text);
    outputPath.setColour (juce::Label::backgroundColourId, theme::panelLight);
    outputPath.setBorderSize ({ 4, 8, 4, 8 });
    outputSummary.setColour (juce::Label::textColourId, theme::textDim);
    outputSummary.setJustificationType (juce::Justification::topLeft);
    planText.setMultiLine (true);
    planText.setReadOnly (true);
    planText.setFont (juce::FontOptions ("Consolas", 13.0f, juce::Font::plain));
    planText.setColour (juce::TextEditor::backgroundColourId, theme::background);
    planText.setColour (juce::TextEditor::textColourId, theme::text);
    planText.setColour (juce::TextEditor::outlineColourId, theme::outline);

    for (auto* comp : std::initializer_list<juce::Component*> { &outputPath, &outputSummary, &outputButton, &previewButton, &createButton, &undoButton, &excelButton, &planText })
        outputTab->addAndMakeVisible (comp);

    outputButton.onClick = [this] { chooseOutput(); };
    previewButton.onClick = [this] { previewPlan(); };
    createButton.onClick = [this] { createView(); };
    undoButton.onClick = [this] { undoView(); };
    excelButton.onClick = [this] { exportExcel(); };

    outputTab->layout = [this] {
        auto r = outputTab->getLocalBounds().reduced (12, 10);
        outputSummary.setBounds (r.removeFromTop (64));
        r.removeFromTop (4);
        auto row = r.removeFromTop (30);
        outputButton.setBounds (row.removeFromRight (150));
        row.removeFromRight (6);
        outputPath.setBounds (row);
        r.removeFromTop (8);
        row = r.removeFromTop (30);
        previewButton.setBounds (row.removeFromLeft (110));
        row.removeFromLeft (6);
        createButton.setBounds (row.removeFromLeft (250));
        row.removeFromLeft (6);
        undoButton.setBounds (row.removeFromLeft (220));
        row.removeFromLeft (6);
        excelButton.setBounds (row.removeFromLeft (220));
        r.removeFromTop (8);
        planText.setBounds (r);
    };


    // ---- checks tab
    checkModel = std::make_unique<CheckModel> (*this);
    checkTable.setModel (checkModel.get());
    checkTable.setRowHeight (24);
    checkTable.setColour (juce::ListBox::backgroundColourId, theme::background);
    checkTable.setColour (juce::ListBox::outlineColourId, theme::outline);
    checkTable.setOutlineThickness (1);
    auto& checkHeader = checkTable.getHeader();
    checkHeader.setColour (juce::TableHeaderComponent::backgroundColourId, theme::panel);
    checkHeader.setColour (juce::TableHeaderComponent::textColourId, theme::textDim);
    checkHeader.addColumn (WT_U8 ("組"), 1, 44, 36, 70, juce::TableHeaderComponent::notSortable);
    checkHeader.addColumn (WT_U8 ("種類"), 2, 90, 70, 150, juce::TableHeaderComponent::notSortable);
    checkHeader.addColumn (WT_U8 ("名前 / ファイル"), 3, 300, 120, 700, juce::TableHeaderComponent::notSortable);
    checkHeader.addColumn (WT_U8 ("件数 / サイズ"), 4, 100, 70, 200, juce::TableHeaderComponent::notSortable);
    checkHeader.addColumn (WT_U8 ("場所"), 5, 480, 150, 1200, juce::TableHeaderComponent::notSortable);
    checkHeader.setStretchToFitActive (true);

    sensitivityBox.addItem (WT_U8 ("感度: 厳しい (1 文字違い)"), 1);
    sensitivityBox.addItem (WT_U8 ("感度: 標準"), 2);
    sensitivityBox.addItem (WT_U8 ("感度: ゆるい (似た名前も)"), 3);
    sensitivityBox.setSelectedId (2, juce::dontSendNotification);
    spellingButton.setButtonText (WT_U8 ("綴り揺れを検出 (ライブラリ全体)"));
    exactButton.setButtonText (WT_U8 ("完全一致の重複を検出 (ライブラリ全体)"));
    nearButton.setButtonText (WT_U8 ("似た音の重複 (「グループ分け」で選んだフォルダ)"));
    cancelCheckButton.setButtonText (WT_U8 ("中止"));
    revealCheckButton.setButtonText (WT_U8 ("場所を開く"));
    ignoreButton.setButtonText (WT_U8 ("この組は別の名前として扱う"));
    exportCheckButton.setButtonText (WT_U8 ("Excel に書き出し"));
    checkStatus.setColour (juce::Label::textColourId, theme::textDim);
    checkStatus.setText (WT_U8 ("綴り揺れ: 似ているが違う名前を探します。重複: 内容が同じファイルを探します。元のファイルは変更しません (旧ライブラリは Cubase が参照しているため、名前の修正や削除は、ここでは行いません)。"), juce::dontSendNotification);

    for (auto* comp : std::initializer_list<juce::Component*> { &checkTable, &sensitivityBox, &spellingButton, &exactButton, &nearButton, &cancelCheckButton,
                                                                &revealCheckButton, &ignoreButton, &exportCheckButton, &checkStatus })
        checkTab->addAndMakeVisible (comp);

    spellingButton.onClick = [this] { runSpellingCheck(); };
    exactButton.onClick = [this] { runExactCheck(); };
    nearButton.onClick = [this] { runNearCheck(); };
    cancelCheckButton.onClick = [this] { cancelCheck = true; };
    revealCheckButton.onClick = [this] {
        if (const auto* r = selectedCheckRow(); r != nullptr && r->file != juce::File())
            r->file.revealToUser();
    };
    ignoreButton.onClick = [this] { ignoreSelectedSpelling(); };
    exportCheckButton.onClick = [this] { exportChecks(); };

    checkTab->layout = [this] {
        auto r = checkTab->getLocalBounds().reduced (12, 10);
        checkStatus.setBounds (r.removeFromTop (44));
        r.removeFromTop (4);

        auto row = r.removeFromTop (30);
        sensitivityBox.setBounds (row.removeFromLeft (220));
        row.removeFromLeft (8);
        spellingButton.setBounds (row.removeFromLeft (230));
        row.removeFromLeft (8);
        exactButton.setBounds (row.removeFromLeft (290));

        r.removeFromTop (6);
        row = r.removeFromTop (30);
        nearButton.setBounds (row.removeFromLeft (370));
        row.removeFromLeft (8);
        cancelCheckButton.setBounds (row.removeFromLeft (70));

        auto bottom = r.removeFromBottom (30);
        r.removeFromBottom (6);
        revealCheckButton.setBounds (bottom.removeFromLeft (110));
        bottom.removeFromLeft (6);
        ignoreButton.setBounds (bottom.removeFromLeft (230));
        bottom.removeFromLeft (6);
        exportCheckButton.setBounds (bottom.removeFromLeft (170));

        r.removeFromTop (8);
        checkTable.setBounds (r);
    };

    tabs.setTabBarDepth (32);
    tabs.addTab (WT_U8 ("グループ分け"), theme::panel, groupingTab.get(), false);
    tabs.addTab (WT_U8 ("整理ビュー (ハードリンク)"), theme::panel, outputTab.get(), false);
    tabs.addTab (WT_U8 ("チェック (綴り揺れ・重複)"), theme::panel, checkTab.get(), false);
    tabs.setColour (juce::TabbedComponent::backgroundColourId, theme::panel);
    addAndMakeVisible (tabs);

    tabs.getTabbedButtonBar().setCurrentTabIndex (0);
    refreshGroups();
    refreshOutput();
    updateStatus();
    startTimerHz (4);
}

OrganizerPanel::~OrganizerPanel()
{
    stopTimer();
    tabs.clearTabs(); // the tab bodies are owned by this panel, so detach them before they are destroyed
    analyzer.cancel();
    cancelCheck = true;
    checkJobs.removeAllJobs (true, 30000);
    groupsList.setModel (nullptr);
    filesTable.setModel (nullptr);
    checkTable.setModel (nullptr);
    jobs.removeAllJobs (true, 20000);
}

void OrganizerPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
}

void OrganizerPanel::resized()
{
    tabs.setBounds (getLocalBounds());
}

void OrganizerPanel::timerCallback()
{
    if (analyzer.isRunning())
        updateStatus();

    if (checkRunning)
        checkStatus.setText (WT_U8 ("検査中... ") + juce::String (checkDone.load()) + " / " + juce::String (checkTotal.load()) + WT_U8 ("  (「中止」でやめられます)"), juce::dontSendNotification);
}

//==============================================================================
void OrganizerPanel::chooseFolder()
{
    const auto roots = hooks.getRoots();
    const auto start = folder.isDirectory() ? folder : (roots.isEmpty() ? juce::File() : juce::File (roots[0]));
    chooser = std::make_unique<juce::FileChooser> (WT_U8 ("区分し直したいフォルダを選択 (読み取りのみ。中のファイルは変更しません)"), start);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc) {
        const auto dir = fc.getResult();
        if (dir.isDirectory())
            loadFolder (dir);
    });
}

void OrganizerPanel::loadFolder (const juce::File& dir)
{
    folder = dir;
    files.clear();
    for (const auto& item : juce::RangedDirectoryIterator (dir, true, audioPatterns, juce::File::findFiles))
        files.push_back (item.getFile());
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b) { return a.getFullPathName().compareIgnoreCase (b.getFullPathName()) < 0; });

    assignment.assign (files.size(), -1);
    distance.assign (files.size(), 0.0f);
    groupMembers.clear();
    groupCount = 0;
    currentGroup = -1;

    int analysed = 0;
    for (const auto& f : files)
        analysed += store.isCurrent (f) ? 1 : 0;
    kSlider.setValue (juce::jlimit (2.0, 30.0, std::round (std::sqrt ((double) files.size() / 2.0))), juce::dontSendNotification);

    folderLabel.setText (dir.getFullPathName() + "   (" + juce::String ((int) files.size()) + WT_U8 (" ファイル, 解析済み ") + juce::String (analysed) + ")", juce::dontSendNotification);
    refreshGroups();
    updateStatus();
}

void OrganizerPanel::startAnalysis()
{
    if (files.empty() || analyzer.isRunning())
        return;

    analyzer.onFinished = [this] {
        updateStatus();
        loadFolder (folder);
    };
    analyzer.start (files);
    updateStatus();
}

void OrganizerPanel::updateStatus()
{
    if (analyzer.isRunning())
        statusLabel.setText (WT_U8 ("解析中... ") + juce::String (analyzer.getDone()) + " / " + juce::String (analyzer.getTotal()), juce::dontSendNotification);
    else if (files.empty())
        statusLabel.setText (WT_U8 ("フォルダを選んでください。選んだフォルダの音を、似ているもの同士でグループにまとめます。"), juce::dontSendNotification);
    else if (groupCount == 0)
        statusLabel.setText (WT_U8 ("① 音を解析 → ② グループ分け の順に進めます。解析結果は保存され、途中で止めても続きから再開できます。"), juce::dontSendNotification);
    else
        statusLabel.setText (juce::String (groupCount) + WT_U8 (" グループ。グループを選んで試聴し、Category / Type を決めて割り当てます。"), juce::dontSendNotification);
}

void OrganizerPanel::suggestGroups()
{
    std::vector<FeatureVector> vectors;
    for (const auto& f : files)
        if (const auto* r = store.find (f))
            vectors.push_back (r->v);

    if (vectors.size() < 4)
        return;
    kSlider.setValue (suggestK (normalise (vectors), 20), juce::dontSendNotification);
}

void OrganizerPanel::runGrouping()
{
    std::vector<int> index;
    std::vector<FeatureVector> vectors;
    for (size_t i = 0; i < files.size(); ++i)
        if (const auto* r = store.find (files[i]))
        {
            index.push_back ((int) i);
            vectors.push_back (r->v);
        }

    if (vectors.size() < 2)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, WT_U8 ("グループ分け"), WT_U8 ("先に「① 音を解析」を実行してください。"));
        return;
    }

    const auto result = kmeans (normalise (vectors), (int) kSlider.getValue());

    // number the groups by size so that G1 is the biggest
    std::vector<std::vector<int>> raw ((size_t) result.k);
    for (size_t j = 0; j < index.size(); ++j)
    {
        raw[(size_t) result.assignment[j]].push_back (index[j]);
        distance[(size_t) index[j]] = result.distance[j];
    }
    std::sort (raw.begin(), raw.end(), [] (const auto& a, const auto& b) { return a.size() > b.size(); });
    raw.erase (std::remove_if (raw.begin(), raw.end(), [] (const auto& g) { return g.empty(); }), raw.end());

    groupMembers = raw;
    groupCount = (int) groupMembers.size();
    for (auto& members : groupMembers)
        std::sort (members.begin(), members.end(), [this] (int a, int b) { return distance[(size_t) a] < distance[(size_t) b]; });
    for (int g = 0; g < groupCount; ++g)
        for (auto i : groupMembers[(size_t) g])
            assignment[(size_t) i] = g;

    currentGroup = groupCount > 0 ? 0 : -1;
    refreshGroups();
    groupsList.selectRow (0);
    updateStatus();
}

void OrganizerPanel::refreshGroups()
{
    groupsList.updateContent();
    groupsList.repaint();
    filesTable.updateContent();
    filesTable.repaint();
}

void OrganizerPanel::selectGroup (int group)
{
    currentGroup = group;
    filesTable.updateContent();
    filesTable.deselectAllRows();
    filesTable.repaint();
}

juce::Array<int> OrganizerPanel::selectedFileIndices() const
{
    juce::Array<int> out;
    if (currentGroup < 0 || currentGroup >= groupCount)
        return out;

    const auto sel = filesTable.getSelectedRows();
    for (int i = 0; i < sel.size(); ++i)
        if (sel[i] < (int) groupMembers[(size_t) currentGroup].size())
            out.add (groupMembers[(size_t) currentGroup][(size_t) sel[i]]);
    return out;
}

void OrganizerPanel::pickCategory()
{
    auto picker = std::make_unique<CategoryPicker> (catalog, category, [this] (const juce::String& name) {
        category = name;
        catalog.noteUsed (name);
        categoryButton.setButtonText ("Category: " + (name.isEmpty() ? WT_U8 ("(未選択)") : name) + " " + WT_U8 ("▾"));
    }, [] {});
    picker->setSize (400, 560);
    juce::CallOutBox::launchAsynchronously (std::move (picker), categoryButton.getScreenBounds(), nullptr);
}

void OrganizerPanel::pickTypeFromMenu()
{
    const auto types = catalog.getTypes (category);
    juce::PopupMenu menu;
    for (size_t i = 0; i < types.size(); ++i)
        menu.addItem ((int) i + 1, types[i].name + "      " + types[i].jp);
    if (types.empty())
        menu.addItem (-1, WT_U8 ("(Category を選ぶと候補が出ます)"), false);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&typeMenuButton), [this, types] (int r) {
        if (r > 0)
            typeEditor.setText (types[(size_t) r - 1].name);
    });
}

void OrganizerPanel::assign (const juce::Array<int>& fileIndices)
{
    if (fileIndices.isEmpty())
        return;

    const auto type = typeEditor.getText().trim();
    if (category.isEmpty() || type.isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, WT_U8 ("割り当て"), WT_U8 ("Category と Type の両方を決めてから割り当ててください。"));
        return;
    }

    const auto roots = hooks.getRoots();
    for (auto i : fileIndices)
        db.assign (ReclassDb::makeKey (roots, files[(size_t) i]), category, type, "G" + juce::String (assignment[(size_t) i] + 1) + " " + folder.getFileName());

    db.save (catalog.getDirectory().getChildFile ("reclass.json"));
    refreshGroups();
    refreshOutput();
}

void OrganizerPanel::unassign (const juce::Array<int>& fileIndices)
{
    const auto roots = hooks.getRoots();
    for (auto i : fileIndices)
        db.unassign (ReclassDb::makeKey (roots, files[(size_t) i]));
    db.save (catalog.getDirectory().getChildFile ("reclass.json"));
    refreshGroups();
    refreshOutput();
}

void OrganizerPanel::moveToGroup (const juce::Array<int>& fileIndices, int group)
{
    if (group < 0 || group >= groupCount)
        return;

    for (auto i : fileIndices)
    {
        auto& from = groupMembers[(size_t) assignment[(size_t) i]];
        from.erase (std::remove (from.begin(), from.end(), i), from.end());
        groupMembers[(size_t) group].push_back (i);
        assignment[(size_t) i] = group;
    }
    std::sort (groupMembers[(size_t) group].begin(), groupMembers[(size_t) group].end(), [this] (int a, int b) { return distance[(size_t) a] < distance[(size_t) b]; });
    refreshGroups();
}

void OrganizerPanel::audition (const juce::File& f)
{
    if (player == nullptr)
        player = std::make_unique<PlaybackEngine>();
    player->stop();

    if (f.getSize() > 120 * 1024 * 1024)
        return;

    const auto generation = ++auditionGeneration;
    juce::Component::SafePointer<OrganizerPanel> safe (this);
    jobs.addJob ([this, safe, f, generation] {
        if (generation != auditionGeneration)
            return;
        juce::String error;
        auto audio = loadAudioFile (formats, f, error);
        juce::MessageManager::callAsync ([safe, audio, generation] {
            if (safe == nullptr || audio == nullptr || generation != safe->auditionGeneration)
                return;
            safe->player->setAudio (audio);
            safe->player->play (0, -1, false);
        });
    });
}

//==============================================================================
juce::File OrganizerPanel::manifestFile() const
{
    return hooks.getOutputDir().getChildFile ("_links_manifest.json");
}

std::vector<AssignedFile> OrganizerPanel::resolveAssigned() const
{
    std::vector<AssignedFile> out;
    const auto roots = hooks.getRoots();
    for (const auto& [key, e] : db.all())
    {
        const auto f = ReclassDb::resolve (roots, key);
        if (f != juce::File())
            out.push_back ({ f, e.category, e.type });
    }
    return out;
}

void OrganizerPanel::refreshOutput()
{
    const auto dir = hooks.getOutputDir();
    outputPath.setText (dir.getFullPathName().isEmpty() ? WT_U8 ("(未設定)") : dir.getFullPathName(), juce::dontSendNotification);

    std::set<juce::String> kinds;
    for (const auto& [k, e] : db.all())
        kinds.insert (e.category + "/" + e.type);

    outputSummary.setText (WT_U8 ("割り当て済み: ") + juce::String ((int) db.size()) + WT_U8 (" ファイル / ") + juce::String ((int) kinds.size()) + WT_U8 (" 種類 (Category / Type)\n"
                                  "整理ビューは、元のファイルへのハードリンクとして作ります。元の場所・名前・Cubase の参照は変わりません (出力先は元と同じドライブが必要です)。"),
                           juce::dontSendNotification);
}

void OrganizerPanel::previewPlan()
{
    const auto out = hooks.getOutputDir();
    if (out.getFullPathName().isEmpty())
    {
        planText.setText (WT_U8 ("出力先が未設定です。「出力先を変更...」で、元のファイルと同じドライブのフォルダを選んでください。"));
        return;
    }

    const auto plan = planLinks (out, resolveAssigned());
    juce::String text = juce::String ((int) plan.items.size()) + WT_U8 (" 個のリンクを作ります\n出力先: ") + out.getFullPathName() + "\n\n";
    for (const auto& p : plan.problems)
        text += WT_U8 ("! ") + p + "\n";
    if (! plan.problems.isEmpty())
        text += "\n";

    int shown = 0;
    for (const auto& item : plan.items)
    {
        if (++shown > 60)
        {
            text += "...\n";
            break;
        }
        text += item.target.getRelativePathFrom (out) + "   <-   " + item.source.getFullPathName() + "\n";
    }
    planText.setText (text);
}

void OrganizerPanel::createView()
{
    const auto out = hooks.getOutputDir();
    const auto plan = planLinks (out, resolveAssigned());
    if (out.getFullPathName().isEmpty() || plan.items.empty() || ! plan.problems.isEmpty())
    {
        previewPlan();
        return;
    }

    juce::Component::SafePointer<OrganizerPanel> safe (this);
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, WT_U8 ("整理ビューを作成"),
                                        juce::String ((int) plan.items.size()) + WT_U8 (" 個のハードリンクを作ります。\n\n") + out.getFullPathName()
                                            + WT_U8 ("\n\n元のファイルは変更されません。あとで「作成したリンクを取り消す」で元に戻せます。"),
                                        WT_U8 ("作成する"), WT_U8 ("キャンセル"), this,
                                        juce::ModalCallbackFunction::create ([safe, plan] (int result) {
                                            if (safe == nullptr || result != 1)
                                                return;
                                            safe->outputSummary.setText (WT_U8 ("作成中..."), juce::dontSendNotification);
                                            safe->jobs.addJob ([safe, plan] {
                                                if (safe == nullptr)
                                                    return;
                                                const auto manifest = safe->manifestFile();
                                                const auto run = createLinks (plan, manifest);
                                                juce::MessageManager::callAsync ([safe, run] {
                                                    if (safe == nullptr)
                                                        return;
                                                    safe->refreshOutput();
                                                    safe->planText.setText (juce::String (run.created) + WT_U8 (" 個を作成、") + juce::String (run.alreadyThere) + WT_U8 (" 個はすでにありました。\n")
                                                                            + (run.errors.isEmpty() ? juce::String() : WT_U8 ("\n注意:\n") + run.errors.joinIntoString ("\n")));
                                                });
                                            });
                                        }));
}

void OrganizerPanel::undoView()
{
    juce::Component::SafePointer<OrganizerPanel> safe (this);
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, WT_U8 ("リンクを取り消す"),
                                        WT_U8 ("このツールが作ったハードリンクだけを削除します。元のファイルは残ります。よろしいですか?"),
                                        WT_U8 ("取り消す"), WT_U8 ("キャンセル"), this, juce::ModalCallbackFunction::create ([safe] (int result) {
                                            if (safe == nullptr || result != 1)
                                                return;
                                            const auto r = undoLinks (safe->manifestFile());
                                            safe->planText.setText (juce::String (r.removed) + WT_U8 (" 個のリンクを取り消しました。\n") + (r.kept > 0 ? juce::String (r.kept) + WT_U8 (" 個は、リンクではなくなっていたため触れていません:\n") + r.notes.joinIntoString ("\n") : juce::String()));
                                        }));
}

void OrganizerPanel::exportExcel()
{
    const auto file = catalog.getDirectory().getChildFile ("reclass.xlsx");
    juce::String error;
    if (xlsx::write (file, { { "Reclass", db.toRows() } }, error))
    {
        planText.setText (WT_U8 ("書き出しました:\n") + file.getFullPathName() + WT_U8 ("\n\n(これは確認用の書き出しです。正本は reclass.json です)"));
        file.revealToUser();
    }
    else
    {
        planText.setText (WT_U8 ("書き出せませんでした: ") + error);
    }
}

void OrganizerPanel::chooseOutput()
{
    chooser = std::make_unique<juce::FileChooser> (WT_U8 ("整理ビューの出力先 (元のファイルと同じドライブ)"), hooks.getOutputDir());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc) {
        const auto dir = fc.getResult();
        if (dir.isDirectory() && hooks.setOutputDir)
        {
            hooks.setOutputDir (dir);
            refreshOutput();
        }
    });
}

void OrganizerPanel::runDemo (const juce::File& dir, const juce::String& cat, const juce::String& type)
{
    loadFolder (dir);
    for (const auto& f : files)
    {
        FeatureVector v {};
        if (computeFeatures (formats, f, v))
            store.put (f, v);
    }
    loadFolder (dir);
    kSlider.setValue (3.0, juce::dontSendNotification);
    runGrouping();

    category = cat;
    categoryButton.setButtonText ("Category: " + cat + " " + WT_U8 ("▾"));
    typeEditor.setText (type);
    if (groupCount > 0)
    {
        juce::Array<int> all;
        for (auto i : groupMembers[0])
            all.add (i);
        assign (all);
    }
    filesTable.selectRow (0);
    previewPlan();

    if (const auto tab = juce::SystemStats::getEnvironmentVariable ("WT_DEMO_TAB", {}); tab.getIntValue() == 2)
    {
        // sample rows, so the Checks tab can be looked at without scanning a real library
        const auto row = [] (int g, const char* kind, const char* name, const char* detail, const char* where) {
            CheckRow r;
            r.group = g;
            r.kind = juce::String::fromUTF8 (kind);
            r.name = name;
            r.detail = juce::String::fromUTF8 (detail);
            r.location = juce::String::fromUTF8 (where);
            return r;
        };
        checkRows = { row (1, "★ 多い綴り", "ImpactSound", "29 件", "G:\\SE\\Rec261020\\ImpactSound"),
                      row (1, "綴り揺れ", "ImpactSoun", "1 件", "G:\\SE\\Old\\Hits"),
                      row (2, "★ 多い綴り", "MaskingTape", "5 件", "G:\\SE\\Rec261020\\MaskingTape"),
                      row (2, "綴り揺れ", "MasicTape", "13 件", "G:\\SE\\Rec261020\\MasicTape"),
                      row (3, "完全一致", "HeavyRain.wav", "18,420 KB", "D:\\pcm録音"),
                      row (3, "完全一致", "HeavyRain1.wav", "18,420 KB", "D:\\pcm録音"),
                      row (3, "完全一致", "HeavyRain_backup.wav", "18,420 KB", "G:\\SE\\Ambience") };
        checkTable.updateContent();
        checkStatus.setText (WT_U8 ("(表示例) 2 組の綴り揺れと、1 組の完全一致の重複。★ は一番多く使われている綴り(おそらく正しい方)です。"), juce::dontSendNotification);
        tabs.setCurrentTabIndex (2);
    }
}

//==============================================================================
// Checks: spelling variants and duplicates
//==============================================================================
juce::File OrganizerPanel::ignoreFile() const
{
    return catalog.getDirectory().getChildFile ("spelling_ignore.json");
}

const OrganizerPanel::CheckRow* OrganizerPanel::selectedCheckRow() const
{
    const auto row = checkTable.getSelectedRow();
    return row >= 0 && row < (int) checkRows.size() ? &checkRows[(size_t) row] : nullptr;
}

void OrganizerPanel::finishCheck (std::vector<CheckRow> rows, const juce::String& message)
{
    checkRunning = false;
    checkRows = std::move (rows);
    checkTable.updateContent();
    checkTable.repaint();
    checkStatus.setText (message, juce::dontSendNotification);
    for (auto* b : { &spellingButton, &exactButton, &nearButton })
        b->setEnabled (true);
}

void OrganizerPanel::runSpellingCheck()
{
    const auto index = hooks.getIndex ? hooks.getIndex() : nullptr;
    if (index == nullptr || index->entries.empty() || checkRunning)
    {
        checkStatus.setText (WT_U8 ("ライブラリの索引がまだありません。先に「Library」の検索ウィンドウでスキャンを終えてください。"), juce::dontSendNotification);
        return;
    }

    juce::StringArray ignored;
    const auto json = readJson (ignoreFile()); // keep the parsed value alive while its array is used
    if (const auto* arr = json.getArray())
        for (const auto& v : *arr)
            ignored.add (v.toString());

    const auto sensitivity = sensitivityBox.getSelectedId() == 1 ? NameSensitivity::Strict : sensitivityBox.getSelectedId() == 3 ? NameSensitivity::Loose : NameSensitivity::Normal;

    for (auto* b : { &spellingButton, &exactButton, &nearButton })
        b->setEnabled (false);
    cancelCheck = false;
    checkRunning = true;
    checkDone = 0;
    checkTotal = 0;
    checkStatus.setText (WT_U8 ("綴り揺れを検出中... (ライブラリの規模により数秒〜数十秒)"), juce::dontSendNotification);

    juce::Component::SafePointer<OrganizerPanel> safe (this);
    checkJobs.addJob ([safe, index, sensitivity, ignored] {
        const auto groups = findSimilarNames (*index, sensitivity, ignored);

        juce::MessageManager::callAsync ([safe, groups] {
            if (safe == nullptr)
                return;

            std::vector<CheckRow> rows;
            int n = 0;
            for (const auto& g : groups)
            {
                ++n;
                juce::StringArray keys;
                for (const auto& v : g.variants)
                    keys.add (v.key);

                for (size_t i = 0; i < g.variants.size(); ++i)
                {
                    const auto& v = g.variants[i];
                    CheckRow r;
                    r.group = n;
                    r.kind = i == 0 ? WT_U8 ("★ 多い綴り") : WT_U8 ("綴り揺れ");
                    r.name = v.display;
                    r.detail = juce::String (v.count) + WT_U8 (" 件");
                    r.location = v.example.getParentDirectory().getFullPathName();
                    r.file = v.example;
                    r.groupKeys = keys;
                    rows.push_back (r);
                }
            }

            safe->finishCheck (std::move (rows),
                               groups.empty() ? WT_U8 ("似ているが違う名前は見つかりませんでした。")
                                              : juce::String ((int) groups.size()) + WT_U8 (" 組。上にあるほど、タイプミスの可能性が高い組です (多い綴りに対して、少数派が極端に少ない)。★ は一番多く使われている綴り。"
                                                                                                "別物なら「この組は別の名前として扱う」で、次回から出しません。"));
        });
    });
}

void OrganizerPanel::runExactCheck()
{
    const auto index = hooks.getIndex ? hooks.getIndex() : nullptr;
    if (index == nullptr || index->entries.empty() || checkRunning)
    {
        checkStatus.setText (WT_U8 ("ライブラリの索引がまだありません。先に「Library」の検索ウィンドウでスキャンを終えてください。"), juce::dontSendNotification);
        return;
    }

    for (auto* b : { &spellingButton, &exactButton, &nearButton })
        b->setEnabled (false);
    cancelCheck = false;
    checkRunning = true;
    checkDone = 0;
    checkTotal = 0;

    juce::Component::SafePointer<OrganizerPanel> safe (this);
    checkJobs.addJob ([this, safe, index] {
        const auto groups = findExactDuplicates (*index, hashStore, [this] (int done, int total) {
            checkDone = done;
            checkTotal = total;
            return ! cancelCheck.load();
        });
        const auto wasCancelled = cancelCheck.load();

        juce::MessageManager::callAsync ([safe, groups, wasCancelled] {
            if (safe == nullptr)
                return;

            std::vector<CheckRow> rows;
            juce::int64 wasted = 0;
            int n = 0;
            for (const auto& g : groups)
            {
                ++n;
                wasted += g.size * (juce::int64) (g.files.size() - 1);
                for (const auto& f : g.files)
                {
                    CheckRow r;
                    r.group = n;
                    r.kind = WT_U8 ("完全一致");
                    r.name = f.getFileName();
                    r.detail = juce::String ((double) g.size / 1024.0, g.size > 1024 * 1024 ? 0 : 1) + " KB";
                    r.location = f.getParentDirectory().getFullPathName();
                    r.file = f;
                    rows.push_back (r);
                }
            }

            safe->finishCheck (std::move (rows),
                               wasCancelled ? WT_U8 ("中止しました (そこまでの結果は保存されており、次回は続きから進みます)。")
                               : groups.empty() ? WT_U8 ("内容が完全に同じファイルは見つかりませんでした。")
                                                : juce::String ((int) groups.size()) + WT_U8 (" 組の重複 (内容が完全に同じ)。重複分の合計は約 ")
                                                      + juce::String ((double) wasted / (1024.0 * 1024.0), 1) + " MB。");
        });
    });
}

void OrganizerPanel::runNearCheck()
{
    if (files.empty() || checkRunning)
    {
        checkStatus.setText (WT_U8 ("先に「グループ分け」タブでフォルダを選び、「① 音を解析」を実行してください。"), juce::dontSendNotification);
        return;
    }

    const auto groups = findNearDuplicates (files, store);
    std::vector<CheckRow> rows;
    int n = 0;
    for (const auto& g : groups)
    {
        ++n;
        for (const auto& f : g.files)
        {
            CheckRow r;
            r.group = n;
            r.kind = WT_U8 ("似た音");
            r.name = f.getFileName();
            r.detail = juce::String ((double) f.getSize() / 1024.0, 0) + " KB";
            r.location = f.getParentDirectory().getFullPathName();
            r.file = f;
            rows.push_back (r);
        }
    }

    finishCheck (std::move (rows), groups.empty() ? WT_U8 ("このフォルダに、長さと音色がほぼ同じ組は見つかりませんでした。")
                                                  : juce::String ((int) groups.size()) + WT_U8 (" 組の似た音 (形式・ビット深度・前後の無音が違うだけの可能性があります)。聴き比べて確認してください。"));
}

void OrganizerPanel::ignoreSelectedSpelling()
{
    const auto* row = selectedCheckRow();
    if (row == nullptr || row->groupKeys.size() < 2)
        return;

    juce::StringArray ignored;
    const auto json = readJson (ignoreFile());
    if (const auto* arr = json.getArray())
        for (const auto& v : *arr)
            ignored.add (v.toString());

    for (int i = 0; i < row->groupKeys.size(); ++i)
        for (int j = i + 1; j < row->groupKeys.size(); ++j)
            ignored.addIfNotAlreadyThere (ignoreKey (row->groupKeys[i], row->groupKeys[j]));

    juce::Array<juce::var> out;
    for (const auto& k : ignored)
        out.add (k);
    writeJson (ignoreFile(), out);

    const auto group = row->group;
    checkRows.erase (std::remove_if (checkRows.begin(), checkRows.end(), [group] (const CheckRow& r) { return r.group == group; }), checkRows.end());
    checkTable.updateContent();
    checkTable.repaint();
    checkStatus.setText (WT_U8 ("この組を、別の名前として記録しました。次回の検出には出ません。"), juce::dontSendNotification);
}

void OrganizerPanel::exportChecks()
{
    std::vector<juce::StringArray> rows { { "Group", "Kind", "Name", "Detail", "Location" } };
    for (const auto& r : checkRows)
        rows.push_back ({ juce::String (r.group), r.kind, r.name, r.detail, r.location });

    const auto file = catalog.getDirectory().getChildFile ("checks.xlsx");
    juce::String error;
    if (xlsx::write (file, { { "Checks", rows } }, error))
    {
        checkStatus.setText (WT_U8 ("書き出しました: ") + file.getFullPathName(), juce::dontSendNotification);
        file.revealToUser();
    }
    else
    {
        checkStatus.setText (WT_U8 ("書き出せませんでした: ") + error, juce::dontSendNotification);
    }
}

//==============================================================================
OrganizerWindow::OrganizerWindow (Catalog& catalog, FeatureStore& store, juce::AudioFormatManager& formats, OrganizerHooks hooks)
    : DocumentWindow (WT_U8 ("ライブラリの区分し直し (元のファイルは変更しません)"), theme::background, DocumentWindow::closeButton | DocumentWindow::minimiseButton)
{
    setUsingNativeTitleBar (true);
    panel = new OrganizerPanel (catalog, store, formats, std::move (hooks));
    setContentOwned (panel, false);
    setResizable (true, false);
    setResizeLimits (980, 560, 4000, 3000);
    centreWithSize (1180, 700);
}
} // namespace wt
