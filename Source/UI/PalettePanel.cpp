#include "PalettePanel.h"
#include "Core/Categories.h"
#include "Theme.h"
#include "Util/Text.h"

namespace wt
{
namespace
{
constexpr int keyWidth = 26;

void styleEditor (juce::TextEditor& e, float fontSize)
{
    e.setFont (juce::FontOptions (fontSize));
    e.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    e.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    e.setColour (juce::TextEditor::focusedOutlineColourId, theme::accent);
    e.setColour (juce::TextEditor::textColourId, theme::text);
    e.setIndents (4, 0);
    e.setBorder ({ 0, 0, 0, 0 });
}
} // namespace

class PalettePanel::Slot : public juce::Component
{
public:
    Slot (CycleModel& m, int i, PalettePanel& o)
        : model (m), index (i), owner (o)
    {
        styleEditor (editor, 14.0f);
        editor.setTextToShowWhenEmpty (WT_U8 ("(未設定)"), theme::textDim);
        editor.setSelectAllWhenFocused (true);
        editor.onTextChange = [this] { model.setTypeName (index, editor.getText().trim()); };
        editor.onReturnKey = [this] { if (owner.onEditingFinished) owner.onEditingFinished(); };
        editor.onEscapeKey = editor.onReturnKey;
        addAndMakeVisible (editor);
    }

    void refresh()
    {
        if (editor.getText() != model.getType (index).name)
            editor.setText (model.getType (index).name, false);
        editor.setAlpha (model.isTypeUsable (index) ? 1.0f : 0.8f);
        repaint();
    }

    void resized() override
    {
        editor.setBounds (getLocalBounds().withTrimmedLeft (keyWidth + 4).withTrimmedRight (34).reduced (0, 6));
    }

    void paint (juce::Graphics& g) override
    {
        const auto usable = model.isTypeUsable (index);
        const auto count = model.countOfType (index);
        const auto colour = model.getType (index).colour;
        const auto b = getLocalBounds().toFloat().reduced (1.5f);

        g.setColour (theme::panelLight.withAlpha (usable ? 1.0f : 0.45f));
        g.fillRoundedRectangle (b, 5.0f);

        const auto key = b.withWidth ((float) keyWidth);
        g.setColour (colour.withAlpha (! usable ? 0.22f : count > 0 ? 0.95f : 0.5f));
        g.fillRoundedRectangle (key, 5.0f);

        g.setColour (usable ? juce::Colours::black.withAlpha (0.8f) : theme::textDim);
        g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
        g.drawText (juce::String (index + 1), key.toNearestInt(), juce::Justification::centred);

        g.setColour (usable && count > 0 ? theme::text : theme::textDim);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (usable ? juce::String (count) : juce::String(), getLocalBounds().removeFromRight (34).reduced (2, 0), juce::Justification::centred);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.x < keyWidth + 2 && owner.onSlotClicked)
            owner.onSlotClicked (index);
    }

private:
    CycleModel& model;
    int index;
    PalettePanel& owner;
    juce::TextEditor editor;
};

PalettePanel::PalettePanel (CycleModel& m)
    : model (m)
{
    prefixLabel.setText ("Prefix", juce::dontSendNotification);
    patternLabel.setText (WT_U8 ("命名"), juce::dontSendNotification);
    for (auto* l : { &prefixLabel, &patternLabel })
    {
        l->setColour (juce::Label::textColourId, theme::textDim);
        l->setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (l);
    }

    exampleLabel.setColour (juce::Label::textColourId, theme::textDim);
    summaryLabel.setColour (juce::Label::textColourId, theme::textDim);
    warningLabel.setColour (juce::Label::textColourId, juce::Colour (0xffffc533));
    for (auto* l : { &exampleLabel, &summaryLabel, &warningLabel })
    {
        l->setMinimumHorizontalScale (1.0f);
        addAndMakeVisible (l);
    }

    categoryLabel.setText ("Category", juce::dontSendNotification);
    categoryLabel.setColour (juce::Label::textColourId, theme::textDim);
    categoryLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (categoryLabel);

    categoryCombo.setWantsKeyboardFocus (false);
    categoryCombo.addItem (WT_U8 ("(なし)"), 1);
    for (const auto& c : getCategories())
    {
        categoryNames.push_back (c.name);
        categoryCombo.addItem (c.name + (c.jp.isNotEmpty() ? "  " + c.jp : juce::String()), (int) categoryNames.size() + 1);
    }
    categoryCombo.onChange = [this] {
        const auto id = categoryCombo.getSelectedId();
        model.setCategory (id >= 2 ? categoryNames[(size_t) id - 2] : juce::String());
    };
    addAndMakeVisible (categoryCombo);

    for (auto* e : { &prefixEditor, &patternEditor })
    {
        styleEditor (*e, 14.0f);
        e->setColour (juce::TextEditor::backgroundColourId, theme::panelLight);
        e->setIndents (6, 0);
        e->setJustification (juce::Justification::centredLeft);
        e->onReturnKey = [this] { if (onEditingFinished) onEditingFinished(); };
        e->onEscapeKey = e->onReturnKey;
        addAndMakeVisible (e);
    }
    prefixEditor.onTextChange = [this] { model.setPrefix (prefixEditor.getText()); };
    patternEditor.onTextChange = [this] { model.setPattern (patternEditor.getText()); };

    for (int i = 0; i < CycleModel::numSlots; ++i)
        addAndMakeVisible (slots.add (new Slot (model, i, *this)));

    model.addChangeListener (this);
    refresh();
}

PalettePanel::~PalettePanel()
{
    model.removeChangeListener (this);
}

void PalettePanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refresh();
}

void PalettePanel::refresh()
{
    if (prefixEditor.getText() != model.getPrefix())
        prefixEditor.setText (model.getPrefix(), false);
    if (patternEditor.getText() != model.getPattern())
        patternEditor.setText (model.getPattern(), false);

    const auto catIt = std::find (categoryNames.begin(), categoryNames.end(), model.getCategory());
    categoryCombo.setSelectedId (catIt == categoryNames.end() ? 1 : (int) (catIt - categoryNames.begin()) + 2, juce::dontSendNotification);

    int firstUsable = 0;
    while (firstUsable < CycleModel::numSlots - 1 && ! model.isTypeUsable (firstUsable))
        ++firstUsable;
    exampleLabel.setText (WT_U8 ("例: ") + model.nameFor (firstUsable, 1 + model.getNumberOffset (firstUsable)) + ".wav",
                          juce::dontSendNotification);

    for (auto* s : slots)
        s->refresh();
}

void PalettePanel::setLibraryInfo (const juce::String& summary, const juce::String& warning)
{
    summaryLabel.setText (summary, juce::dontSendNotification);
    warningLabel.setText (warning, juce::dontSendNotification);
    resized();
}

void PalettePanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    g.setColour (theme::outline);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
}

void PalettePanel::resized()
{
    auto r = getLocalBounds().reduced (8, 5);

    auto top = r.removeFromTop (26);
    prefixLabel.setBounds (top.removeFromLeft (50));
    prefixEditor.setBounds (top.removeFromLeft (110));
    top.removeFromLeft (10);
    patternLabel.setBounds (top.removeFromLeft (44));
    patternEditor.setBounds (top.removeFromLeft (230));
    top.removeFromLeft (10);
    categoryLabel.setBounds (top.removeFromLeft (64));
    categoryCombo.setBounds (top.removeFromLeft (190));
    top.removeFromLeft (12);
    const auto textWidth = [] (const juce::Label& l) {
        juce::GlyphArrangement ga;
        ga.addLineOfText (l.getFont(), l.getText(), 0.0f, 0.0f);
        return (int) std::ceil (ga.getBoundingBox (0, -1, true).getWidth()) + 14;
    };
    exampleLabel.setBounds (top.removeFromLeft (juce::jmin (textWidth (exampleLabel), top.getWidth())));
    summaryLabel.setBounds (top.removeFromLeft (juce::jmin (textWidth (summaryLabel), top.getWidth())));
    warningLabel.setBounds (top);

    r.removeFromTop (6);
    const auto w = r.getWidth() / CycleModel::numSlots;
    for (auto* s : slots)
        s->setBounds (r.removeFromLeft (w));
}
} // namespace wt
