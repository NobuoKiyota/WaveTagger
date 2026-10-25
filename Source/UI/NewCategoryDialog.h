#pragma once

#include "Core/CategoryTypes.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace wt
{
/**
    "Add Category": the names, and the Types it starts with (up to ten, English and Japanese, editable). The Types can be
      - copied from a similar Category (the best match is picked for you; any other can be chosen),
      - asked of an AI: the question is copied to the clipboard, the answer is read back from it,
      - typed by hand.
    Nothing is written until "追加" is pressed; the owner gets the names and the Types and writes them.
*/
class NewCategoryDialog : public juce::Component
{
public:
    using Accept = std::function<void (const juce::String& name, const juce::String& japanese, const std::vector<TypeProposal>& types)>;

    NewCategoryDialog (const Catalog& catalog, Accept onAccept);

    void paint (juce::Graphics& g) override;
    void resized() override;

    /** For the self test and the snapshot. */
    void setNames (const juce::String& name, const juce::String& japanese);
    void useSimilar (int index); // index into the ranked list
    std::vector<TypeProposal> currentTypes() const;
    void applyAiReply (const juce::String& reply);

    static void show (const Catalog& catalog, Accept onAccept);

private:
    static constexpr int rows = Catalog::maxMatrixTypes;

    void rerank();
    void showTypes (const std::vector<TypeProposal>& types);
    void say (const juce::String& text, bool problem = false);
    void accept();
    void close();
    void moveUp (int row);

    const Catalog& catalog;
    Accept onAccept;
    std::vector<SimilarCategory> ranked;
    bool similarTouched = false; // the user chose by hand: names typed later do not change the choice
    juce::String chosen;         // the Category copied from ("*": the common Types; empty: typed or from an AI)

    juce::Label nameLabel, japaneseLabel, similarLabel, typesLabel, status;
    juce::TextEditor name, japanese;
    juce::ComboBox similar;
    juce::TextButton copyPromptButton, readReplyButton, addButton, cancelButton;
    juce::Label numberLabels[rows];
    juce::TextEditor english[rows], japaneseTypes[rows];
    juce::TextButton upButtons[rows];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NewCategoryDialog)
};
} // namespace wt
