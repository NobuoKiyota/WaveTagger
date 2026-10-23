#include "DictionaryWindow.h"
#include "Theme.h"
#include "Util/Text.h"

namespace wt
{
namespace
{
enum Column { colEnglish = 1, colJapanese, colOrigin };
constexpr int margin = 14;

void styleEditor (juce::TextEditor& e)
{
    e.setColour (juce::TextEditor::backgroundColourId, theme::panelLight);
    e.setColour (juce::TextEditor::textColourId, theme::text);
    e.setColour (juce::TextEditor::outlineColourId, theme::outline);
}
} // namespace

DictionaryPanel::DictionaryPanel (Dictionary& d, Hooks h)
    : dictionary (d), hooks (std::move (h))
{
    const auto label = [this] (juce::Label& l, const juce::String& text) {
        l.setText (text, juce::dontSendNotification);
        l.setColour (juce::Label::textColourId, theme::textDim);
        addAndMakeVisible (l);
    };
    label (filterLabel, WT_U8 ("絞り込み"));
    label (englishLabel, "English");
    label (japaneseLabel, WT_U8 ("日本語"));
    status.setColour (juce::Label::textColourId, theme::textDim);
    addAndMakeVisible (status);

    for (auto* e : { &filter, &english, &japanese })
    {
        styleEditor (*e);
        addAndMakeVisible (e);
    }
    filter.onTextChange = [this] { rebuildList(); };
    english.onReturnKey = [this] { japanese.grabKeyboardFocus(); };
    japanese.onReturnKey = [this] { save(); };

    saveButton.setButtonText (WT_U8 ("追加 / 更新"));
    removeButton.setButtonText (WT_U8 ("ユーザーの訳を削除"));
    missingButton.setButtonText (WT_U8 ("この録音の未訳語..."));
    for (auto* b : { &saveButton, &removeButton, &missingButton })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }
    saveButton.onClick = [this] { save(); };
    removeButton.onClick = [this] { removeSelected(); };
    missingButton.onClick = [this] { showUntranslated(); };

    table.setRowHeight (24);
    table.setColour (juce::ListBox::backgroundColourId, theme::background);
    table.setColour (juce::ListBox::outlineColourId, theme::outline);
    table.setOutlineThickness (1);
    auto& header = table.getHeader();
    header.setColour (juce::TableHeaderComponent::backgroundColourId, theme::panel);
    header.setColour (juce::TableHeaderComponent::textColourId, theme::textDim);
    header.addColumn ("English", colEnglish, 220, 120, 400, juce::TableHeaderComponent::notSortable);
    header.addColumn (WT_U8 ("日本語"), colJapanese, 220, 120, 400, juce::TableHeaderComponent::notSortable);
    header.addColumn (WT_U8 ("由来"), colOrigin, 80, 60, 120, juce::TableHeaderComponent::notSortable);
    header.setStretchToFitActive (true);
    addAndMakeVisible (table);

    rebuildList();
    setSize (660, 560);
}

void DictionaryPanel::refresh()
{
    rebuildList();
}

void DictionaryPanel::say (const juce::String& text, bool problem)
{
    status.setColour (juce::Label::textColourId, problem ? juce::Colour (0xffe05a6c) : theme::textDim);
    status.setText (text, juce::dontSendNotification);
}

void DictionaryPanel::rebuildList()
{
    const auto query = filter.getText().trim();
    shown.clear();
    const auto& all = dictionary.entries();
    for (int i = 0; i < (int) all.size(); ++i)
        if (query.isEmpty() || all[(size_t) i].english.containsIgnoreCase (query) || all[(size_t) i].japanese.containsIgnoreCase (query))
            shown.push_back (i);

    table.updateContent();
    table.repaint();
}

void DictionaryPanel::prepare (const juce::String& word)
{
    const auto japaneseWord = Dictionary::isJapanese (word);
    english.setText (japaneseWord ? juce::String() : word, false);
    japanese.setText (japaneseWord ? word : juce::String(), false);
    (japaneseWord ? english : japanese).grabKeyboardFocus();
    say (japaneseWord ? WT_U8 ("英語の訳を入力して Enter") : WT_U8 ("日本語の訳を入力して Enter"));
}

void DictionaryPanel::selectedRowsChanged (int row)
{
    if (row < 0 || row >= (int) shown.size())
        return;

    const auto& e = dictionary.entries()[(size_t) shown[(size_t) row]];
    english.setText (e.english, false);
    japanese.setText (e.japanese, false);
    say (e.user ? WT_U8 ("ユーザーの訳です。変更して「追加 / 更新」で書き換えられます") : WT_U8 ("内蔵の訳です。別の訳を入れて「追加 / 更新」すると、ユーザーの訳が優先されます"));
}

void DictionaryPanel::save()
{
    if (english.getText().trim().isEmpty() || japanese.getText().trim().isEmpty())
    {
        say (WT_U8 ("English と日本語の両方を入力してください"), true);
        return;
    }

    if (dictionary.setUserEntry (english.getText(), japanese.getText()))
    {
        say (WT_U8 ("保存しました: ") + english.getText().trim() + "  =  " + japanese.getText().trim());
        english.clear();
        japanese.clear();
        rebuildList();
        if (hooks.changed)
            hooks.changed();
    }
    else
    {
        say (WT_U8 ("保存できませんでした (dictionary.csv を Excel で開いていませんか?)"), true);
    }
}

void DictionaryPanel::removeSelected()
{
    const auto row = table.getSelectedRow();
    if (row < 0 || row >= (int) shown.size())
        return;

    const auto& e = dictionary.entries()[(size_t) shown[(size_t) row]];
    if (! e.user)
    {
        say (WT_U8 ("内蔵の訳は削除できません (別の訳を入れて上書きできます)"), true);
        return;
    }

    if (dictionary.removeUserEntry (e.english))
    {
        say (WT_U8 ("削除しました"));
        rebuildList();
        if (hooks.changed)
            hooks.changed();
    }
    else
    {
        say (WT_U8 ("削除できませんでした (dictionary.csv を Excel で開いていませんか?)"), true);
    }
}

void DictionaryPanel::showUntranslated()
{
    const auto words = hooks.untranslated ? hooks.untranslated() : juce::StringArray();
    if (words.isEmpty())
    {
        say (WT_U8 ("この録音の属性に、訳の無い語はありません"));
        return;
    }

    juce::PopupMenu menu;
    for (int i = 0; i < words.size(); ++i)
        menu.addItem (i + 1, words[i]);

    juce::Component::SafePointer<DictionaryPanel> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&missingButton), [safe, words] (int r) {
        if (safe != nullptr && r > 0)
            safe->prepare (words[r - 1]);
    });
}

void DictionaryPanel::paintRowBackground (juce::Graphics& g, int row, int, int, bool selected)
{
    g.fillAll (selected ? theme::accent.withAlpha (0.32f) : (row % 2 == 0 ? theme::background : theme::background.brighter (0.03f)));
}

void DictionaryPanel::paintCell (juce::Graphics& g, int row, int column, int width, int height, bool)
{
    if (row >= (int) shown.size())
        return;

    const auto& e = dictionary.entries()[(size_t) shown[(size_t) row]];
    const auto text = column == colEnglish ? e.english : column == colJapanese ? e.japanese : (e.user ? WT_U8 ("ユーザー") : WT_U8 ("内蔵"));

    g.setColour (column == colOrigin ? (e.user ? theme::accent : theme::textDim) : (e.user ? theme::text : theme::textDim.brighter (0.2f)));
    g.setFont (juce::FontOptions ("Meiryo UI", 13.0f, juce::Font::plain));
    g.drawText (text, 6, 0, width - 10, height, juce::Justification::centredLeft, true);
}

void DictionaryPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
}

void DictionaryPanel::resized()
{
    auto r = getLocalBounds().reduced (margin);

    auto top = r.removeFromTop (28);
    filterLabel.setBounds (top.removeFromLeft (70));
    filter.setBounds (top.removeFromLeft (260));
    top.removeFromLeft (14);
    missingButton.setBounds (top.removeFromLeft (150));

    r.removeFromTop (8);
    auto bottom = r.removeFromBottom (112);
    table.setBounds (r);

    bottom.removeFromTop (10);
    auto row = bottom.removeFromTop (28);
    englishLabel.setBounds (row.removeFromLeft (70));
    english.setBounds (row.removeFromLeft (230));
    row.removeFromLeft (14);
    japaneseLabel.setBounds (row.removeFromLeft (60));
    japanese.setBounds (row.removeFromLeft (230));

    bottom.removeFromTop (8);
    row = bottom.removeFromTop (28);
    row.removeFromLeft (70);
    saveButton.setBounds (row.removeFromLeft (120));
    row.removeFromLeft (8);
    removeButton.setBounds (row.removeFromLeft (170));

    bottom.removeFromTop (6);
    status.setBounds (bottom);
}

DictionaryWindow::DictionaryWindow (Dictionary& dictionary, DictionaryPanel::Hooks hooks)
    : DocumentWindow (WT_U8 ("翻訳辞書"), theme::background, DocumentWindow::closeButton | DocumentWindow::minimiseButton)
{
    setUsingNativeTitleBar (true);
    panel = new DictionaryPanel (dictionary, std::move (hooks));
    setContentOwned (panel, true);
    setResizable (false, false);
    centreWithSize (getWidth(), getHeight());
}
} // namespace wt
