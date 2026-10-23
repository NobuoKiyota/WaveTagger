#include "AttributeEditor.h"
#include "Theme.h"
#include "Util/Text.h"

namespace wt
{
AttributeEditor::AttributeEditor (const AttributeVocabulary& vocabulary, const juce::String& initial, int count,
                                  std::function<void (const juce::String&)> accepted)
    : vocab (vocabulary), onAccept (std::move (accepted))
{
    const auto label = [this] (juce::Label& l, const juce::String& t) {
        l.setText (t, juce::dontSendNotification);
        l.setColour (juce::Label::textColourId, theme::textDim);
        addAndMakeVisible (l);
    };
    label (textLabel, count > 1 ? juce::String (count) + WT_U8 (" 件のサイクルに同じ属性を設定します (いまの属性は置き換わります)") : WT_U8 ("属性  (key=value を空白で区切る。自由な語も可)"));
    label (pickLabel, WT_U8 ("追加"));
    label (equalsLabel, "=");
    equalsLabel.setJustificationType (juce::Justification::centred);
    label (hintLabel, WT_U8 ("例: strength=strong material=cardboard   /   同じ key を追加すると値が置き換わり、値を空にして追加すると消えます"));
    hintLabel.setFont (juce::FontOptions (12.0f));

    text.setText (normaliseAttributes (initial), false);
    text.setColour (juce::TextEditor::backgroundColourId, theme::panelLight);
    text.setColour (juce::TextEditor::textColourId, theme::text);
    text.setColour (juce::TextEditor::outlineColourId, theme::outline);
    text.onReturnKey = [this] { accept(); };
    text.onEscapeKey = [this] { close(); };
    addAndMakeVisible (text);

    for (auto* box : { &keyBox, &valueBox })
    {
        box->setEditableText (true);
        box->setWantsKeyboardFocus (true);
        addAndMakeVisible (box);
    }
    keyBox.addItemList (vocab.keys(), 1);
    keyBox.onChange = [this] { fillValues(); };

    addButton.setButtonText (WT_U8 ("追加"));
    okButton.setButtonText ("OK");
    cancelButton.setButtonText (WT_U8 ("キャンセル"));
    for (auto* b : { &addButton, &okButton, &cancelButton })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }
    addButton.onClick = [this] {
        const auto key = keyBox.getText().trim();
        if (key.isEmpty())
            return;
        text.setText (withAttribute (text.getText(), key, valueBox.getText()), false);
        valueBox.setText ({}, juce::dontSendNotification);
    };
    okButton.onClick = [this] { accept(); };
    cancelButton.onClick = [this] { close(); };

    setSize (560, 214);
    text.grabKeyboardFocus();
}

void AttributeEditor::fillValues()
{
    valueBox.clear (juce::dontSendNotification);
    valueBox.addItemList (vocab.valuesFor (keyBox.getText()), 1);
}

void AttributeEditor::accept()
{
    const auto result = normaliseAttributes (text.getText());
    close();
    if (onAccept)
        onAccept (result);
}

void AttributeEditor::close()
{
    if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
        window->exitModalState (0);
}

void AttributeEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
}

void AttributeEditor::resized()
{
    auto r = getLocalBounds().reduced (14);
    textLabel.setBounds (r.removeFromTop (22));
    text.setBounds (r.removeFromTop (30));
    r.removeFromTop (12);

    auto row = r.removeFromTop (28);
    pickLabel.setBounds (row.removeFromLeft (40));
    keyBox.setBounds (row.removeFromLeft (160));
    equalsLabel.setBounds (row.removeFromLeft (26));
    valueBox.setBounds (row.removeFromLeft (160));
    row.removeFromLeft (10);
    addButton.setBounds (row.removeFromLeft (70));

    r.removeFromTop (8);
    hintLabel.setBounds (r.removeFromTop (36));

    auto buttons = r.removeFromBottom (28);
    cancelButton.setBounds (buttons.removeFromRight (100));
    buttons.removeFromRight (8);
    okButton.setBounds (buttons.removeFromRight (100));
}

void AttributeEditor::show (const AttributeVocabulary& vocabulary, const juce::String& initial, int count,
                            std::function<void (const juce::String&)> onAccept)
{
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (new AttributeEditor (vocabulary, initial, count, std::move (onAccept)));
    options.dialogTitle = WT_U8 ("属性を編集");
    options.dialogBackgroundColour = theme::panel;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();
}
} // namespace wt
