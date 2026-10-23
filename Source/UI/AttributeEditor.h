#pragma once

#include "Core/Attributes.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace wt
{
/** The attribute text of one or several cycles: free text plus a key / value picker that adds "key=value" with the words
    used so far on offer. Nothing is enforced; any key and value can be typed. */
class AttributeEditor : public juce::Component
{
public:
    /** `count` is how many cycles the text will be applied to (shown as a reminder when more than one). */
    AttributeEditor (const AttributeVocabulary& vocabulary, const juce::String& initial, int count,
                     std::function<void (const juce::String&)> onAccept);

    void paint (juce::Graphics& g) override;
    void resized() override;

    /** Opens it in a window of its own; `onAccept` gets the normalised text if OK is pressed. */
    static void show (const AttributeVocabulary& vocabulary, const juce::String& initial, int count,
                      std::function<void (const juce::String&)> onAccept);

private:
    void accept();
    void close();
    void fillValues();

    AttributeVocabulary vocab;
    std::function<void (const juce::String&)> onAccept;

    juce::Label textLabel, pickLabel, equalsLabel, hintLabel;
    juce::TextEditor text;
    juce::ComboBox keyBox, valueBox;
    juce::TextButton addButton, okButton, cancelButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AttributeEditor)
};
} // namespace wt
