#include "NewCategoryDialog.h"
#include "Theme.h"
#include "Util/Text.h"

namespace wt
{
namespace
{
constexpr int margin = 16;
constexpr int rowHeight = 26;

void styleEditor (juce::TextEditor& e)
{
    e.setColour (juce::TextEditor::backgroundColourId, theme::panelLight);
    e.setColour (juce::TextEditor::textColourId, theme::text);
    e.setColour (juce::TextEditor::outlineColourId, theme::outline);
}
} // namespace

NewCategoryDialog::NewCategoryDialog (const Catalog& c, Accept accepted)
    : catalog (c), onAccept (std::move (accepted))
{
    const auto label = [this] (juce::Label& l, const juce::String& text) {
        l.setText (text, juce::dontSendNotification);
        l.setColour (juce::Label::textColourId, theme::textDim);
        addAndMakeVisible (l);
    };
    label (nameLabel, "Category (EN)");
    label (japaneseLabel, WT_U8 ("日本語"));
    label (similarLabel, WT_U8 ("似たカテゴリ"));
    label (typesLabel, WT_U8 ("Type (1〜9 がキー 1〜9 に割り当たります。10 は予備)"));
    status.setColour (juce::Label::textColourId, theme::textDim);
    addAndMakeVisible (status);

    for (auto* e : { &name, &japanese })
    {
        styleEditor (*e);
        addAndMakeVisible (e);
    }
    name.onTextChange = [this] { rerank(); };
    japanese.onTextChange = [this] { rerank(); };

    similar.setWantsKeyboardFocus (false);
    similar.onChange = [this] {
        similarTouched = true;
        const auto id = similar.getSelectedId();
        if (id == 1)
        {
            chosen = "*";
            showTypes (commonTypes (catalog));
        }
        else if (id >= 2 && id - 2 < (int) ranked.size())
        {
            chosen = ranked[(size_t) (id - 2)].name;
            showTypes (ownTypesOf (catalog, chosen));
        }
    };
    addAndMakeVisible (similar);

    copyPromptButton.setButtonText (WT_U8 ("AI に聞く (質問文をコピー)"));
    readReplyButton.setButtonText (WT_U8 ("AI の返事を読み込む (クリップボード)"));
    addButton.setButtonText (WT_U8 ("追加"));
    cancelButton.setButtonText (WT_U8 ("キャンセル"));
    for (auto* b : { &copyPromptButton, &readReplyButton, &addButton, &cancelButton })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }

    copyPromptButton.onClick = [this] {
        if (name.getText().trim().isEmpty())
        {
            say (WT_U8 ("先に Category の名前を入力してください"), true);
            return;
        }
        juce::SystemClipboard::copyTextToClipboard (buildAiPrompt (catalog, name.getText(), japanese.getText()));
        say (WT_U8 ("質問文をコピーしました。Gemini や Claude などに貼り付け、返事をコピーして、右のボタンで読み込んでください"));
    };
    readReplyButton.onClick = [this] { applyAiReply (juce::SystemClipboard::getTextFromClipboard()); };
    addButton.onClick = [this] { accept(); };
    cancelButton.onClick = [this] { close(); };

    for (int i = 0; i < rows; ++i)
    {
        numberLabels[i].setText (juce::String (i + 1), juce::dontSendNotification);
        numberLabels[i].setColour (juce::Label::textColourId, i < 9 ? theme::text : theme::textDim);
        numberLabels[i].setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (numberLabels[i]);

        for (auto* e : { &english[i], &japaneseTypes[i] })
        {
            styleEditor (*e);
            addAndMakeVisible (e);
        }
        upButtons[i].setButtonText (WT_U8 ("↑"));
        upButtons[i].setEnabled (i > 0);
        upButtons[i].setWantsKeyboardFocus (false);
        upButtons[i].onClick = [this, i] { moveUp (i); };
        addAndMakeVisible (upButtons[i]);
    }

    setSize (640, 560);
    rerank();
    name.grabKeyboardFocus();
}

void NewCategoryDialog::say (const juce::String& text, bool problem)
{
    status.setColour (juce::Label::textColourId, problem ? juce::Colour (0xffe05a6c) : theme::textDim);
    status.setText (text, juce::dontSendNotification);
}

void NewCategoryDialog::setNames (const juce::String& n, const juce::String& j)
{
    name.setText (n, false);
    japanese.setText (j, false);
    rerank();
}

void NewCategoryDialog::rerank()
{
    ranked = rankSimilarCategories (catalog, name.getText(), japanese.getText());

    // the list: "(none: the common Types)", then every Category that has Types, the best match first
    similar.clear (juce::dontSendNotification);
    similar.addItem (WT_U8 ("(なし: 共通の Type)"), 1);
    for (int i = 0; i < (int) ranked.size(); ++i)
        similar.addItem (ranked[(size_t) i].name + (ranked[(size_t) i].japanese.isNotEmpty() ? "   " + ranked[(size_t) i].japanese : juce::String())
                             + (ranked[(size_t) i].score > 0 ? WT_U8 ("   ← 似ています") : juce::String()),
                         i + 2);

    if (! similarTouched)
    {
        // until the user chooses, the best match (or the common Types if nothing resembles the name) is what is shown
        const auto best = ! ranked.empty() && ranked.front().score > 0 ? 2 : 1;
        similar.setSelectedId (best, juce::dontSendNotification);
        showTypes (best == 2 ? ownTypesOf (catalog, ranked.front().name) : commonTypes (catalog));
    }
    else
    {
        // the list was rebuilt for the new names: keep showing what was chosen
        int id = 0;
        if (chosen == "*")
            id = 1;
        for (int i = 0; i < (int) ranked.size(); ++i)
            if (chosen.isNotEmpty() && ranked[(size_t) i].name == chosen)
                id = i + 2;
        similar.setSelectedId (id, juce::dontSendNotification);
    }
}

void NewCategoryDialog::showTypes (const std::vector<TypeProposal>& types)
{
    for (int i = 0; i < rows; ++i)
    {
        english[i].setText (i < (int) types.size() ? types[(size_t) i].english : juce::String(), false);
        japaneseTypes[i].setText (i < (int) types.size() ? types[(size_t) i].japanese : juce::String(), false);
    }
}

std::vector<TypeProposal> NewCategoryDialog::currentTypes() const
{
    std::vector<TypeProposal> typed;
    for (int i = 0; i < rows; ++i)
        if (english[i].getText().trim().isNotEmpty())
            typed.push_back ({ english[i].getText(), japaneseTypes[i].getText() });
    return tidyProposals (typed, catalog);
}

void NewCategoryDialog::moveUp (int row)
{
    if (row <= 0)
        return;

    const auto e = english[row].getText(), j = japaneseTypes[row].getText();
    english[row].setText (english[row - 1].getText(), false);
    japaneseTypes[row].setText (japaneseTypes[row - 1].getText(), false);
    english[row - 1].setText (e, false);
    japaneseTypes[row - 1].setText (j, false);
    similarTouched = true;
    chosen = {};
}

void NewCategoryDialog::applyAiReply (const juce::String& reply)
{
    std::vector<TypeProposal> raw;
    juce::String error;
    if (! parseAiReply (reply, raw, error))
    {
        say (error, true);
        return;
    }

    const auto tidy = tidyProposals (raw, catalog);
    if (tidy.empty())
    {
        say (WT_U8 ("返事は読めましたが、使える Type (英字の 1 語)がありませんでした"), true);
        return;
    }

    similarTouched = true;
    chosen = {};
    similar.setSelectedId (0, juce::dontSendNotification);
    showTypes (tidy);
    say (juce::String ((int) tidy.size()) + WT_U8 (" 個の Type を読み込みました。内容を確認して、必要なら直してから「追加」を押してください")
         + (raw.size() > tidy.size() ? WT_U8 ("(整えるときに ") + juce::String ((int) (raw.size() - tidy.size())) + WT_U8 (" 個を除きました)") : juce::String()));
}

void NewCategoryDialog::accept()
{
    const auto n = name.getText().trim().toUpperCase();
    if (n.isEmpty())
    {
        say (WT_U8 ("Category の名前を入力してください"), true);
        return;
    }

    const auto types = currentTypes();
    const auto jp = japanese.getText();
    auto done = onAccept;
    close();
    if (done)
        done (n, jp, types);
}

void NewCategoryDialog::close()
{
    if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
        window->exitModalState (0);
}

void NewCategoryDialog::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
}

void NewCategoryDialog::resized()
{
    auto r = getLocalBounds().reduced (margin);

    auto row = r.removeFromTop (rowHeight);
    nameLabel.setBounds (row.removeFromLeft (100));
    name.setBounds (row.removeFromLeft (230));
    row.removeFromLeft (14);
    japaneseLabel.setBounds (row.removeFromLeft (56));
    japanese.setBounds (row.removeFromLeft (200));

    r.removeFromTop (10);
    row = r.removeFromTop (rowHeight);
    similarLabel.setBounds (row.removeFromLeft (100));
    similar.setBounds (row.removeFromLeft (340));

    r.removeFromTop (8);
    row = r.removeFromTop (rowHeight);
    copyPromptButton.setBounds (row.removeFromLeft (230));
    row.removeFromLeft (10);
    readReplyButton.setBounds (row.removeFromLeft (280));

    r.removeFromTop (12);
    typesLabel.setBounds (r.removeFromTop (22));
    r.removeFromTop (4);

    auto buttons = r.removeFromBottom (rowHeight);
    cancelButton.setBounds (buttons.removeFromRight (110));
    buttons.removeFromRight (8);
    addButton.setBounds (buttons.removeFromRight (110));
    r.removeFromBottom (6);
    status.setBounds (r.removeFromBottom (44));

    for (int i = 0; i < rows; ++i)
    {
        auto line = r.removeFromTop (rowHeight);
        r.removeFromTop (3);
        numberLabels[i].setBounds (line.removeFromLeft (28));
        line.removeFromLeft (6);
        english[i].setBounds (line.removeFromLeft (230));
        line.removeFromLeft (10);
        japaneseTypes[i].setBounds (line.removeFromLeft (230));
        line.removeFromLeft (8);
        upButtons[i].setBounds (line.removeFromLeft (32));
    }
}

void NewCategoryDialog::show (const Catalog& catalog, Accept onAccept)
{
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (new NewCategoryDialog (catalog, std::move (onAccept)));
    options.dialogTitle = WT_U8 ("Category を追加");
    options.dialogBackgroundColour = theme::panel;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();
}
} // namespace wt
