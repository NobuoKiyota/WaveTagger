#include "PalettePanel.h"
#include "CategoryPicker.h"
#include "Theme.h"
#include "Util/Text.h"

namespace wt
{
namespace
{
constexpr int keyWidth = 26;
constexpr int countWidth = 34;
constexpr int menuWidth = 18;

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
        editor.setBounds (getLocalBounds().withTrimmedLeft (keyWidth + 4).withTrimmedRight (countWidth + menuWidth).reduced (0, 6));
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

        {
            const auto c = getLocalBounds().withTrimmedRight (countWidth).removeFromRight (menuWidth).toFloat().getCentre();
            juce::Path tri;
            tri.addTriangle (c.x - 4.0f, c.y - 2.0f, c.x + 4.0f, c.y - 2.0f, c.x, c.y + 3.0f);
            g.setColour (theme::textDim);
            g.fillPath (tri);
        }

        g.setColour (usable && count > 0 ? theme::text : theme::textDim);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (usable ? juce::String (count) : juce::String(), getLocalBounds().removeFromRight (countWidth).reduced (2, 0),
                    juce::Justification::centred);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto menuX = getWidth() - countWidth - menuWidth;
        if (e.x < keyWidth + 2)
        {
            if (owner.onSlotClicked)
                owner.onSlotClicked (index);
        }
        else if (e.x >= menuX && e.x < menuX + menuWidth + 2)
        {
            owner.showTypeMenu (index, getScreenBounds());
        }
    }

private:
    CycleModel& model;
    int index;
    PalettePanel& owner;
    juce::TextEditor editor;
};

PalettePanel::PalettePanel (CycleModel& m, Catalog& c)
    : model (m), catalog (c)
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

    categoryButton.setWantsKeyboardFocus (false);
    categoryButton.setColour (juce::TextButton::buttonColourId, theme::panelLight);
    categoryButton.onClick = [this] { showCategoryPicker(); };
    addAndMakeVisible (categoryButton);

    fillButton.setButtonText (WT_U8 ("Type 一括"));
    fillButton.setWantsKeyboardFocus (false);
    fillButton.onClick = [this] { fillSlotsFromCategory(); };
    addAndMakeVisible (fillButton);

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
    catalog.addChangeListener (this);
    refresh();
}

PalettePanel::~PalettePanel()
{
    model.removeChangeListener (this);
    catalog.removeChangeListener (this);
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

    const auto& cat = model.getCategory();
    juce::String jp;
    for (const auto& c : catalog.getCategories())
        if (c.name == cat)
            jp = c.jp;
    categoryButton.setButtonText ("Category:  " + (cat.isEmpty() ? WT_U8 ("(なし)") : cat + (jp.isNotEmpty() ? "  " + jp : juce::String()))
                                  + WT_U8 ("  ▾"));

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

//==============================================================================
void PalettePanel::showCategoryPicker()
{
    auto picker = std::make_unique<CategoryPicker> (
        catalog, model.getCategory(),
        [this] (const juce::String& name) {
            model.setCategory (name);
            catalog.noteUsed (name);
        },
        [this] { promptAddCategory(); });
    picker->setSize (400, 560);

    juce::CallOutBox::launchAsynchronously (std::move (picker), categoryButton.getScreenBounds(), nullptr);
}

void PalettePanel::showTypeMenu (int slot, juce::Rectangle<int> screenArea)
{
    const auto types = catalog.getTypes (model.getCategory());
    const auto specific = catalog.getTypes (model.getCategory(), false);
    const auto current = model.getType (slot).name;

    juce::PopupMenu menu;
    menu.addSectionHeader (model.getCategory().isEmpty() ? WT_U8 ("共通のタイプ (Category 未選択)")
                                                         : WT_U8 ("Type 候補: ") + model.getCategory());

    bool commonHeaderAdded = model.getCategory().isEmpty();
    for (size_t i = 0; i < types.size(); ++i)
    {
        const auto isCommon = std::none_of (specific.begin(), specific.end(),
                                            [&] (const TypeInfo& t) { return t.name.equalsIgnoreCase (types[i].name); });
        if (isCommon && ! commonHeaderAdded)
        {
            menu.addSeparator();
            menu.addSectionHeader (WT_U8 ("共通"));
            commonHeaderAdded = true;
        }
        menu.addItem (100 + (int) i, types[i].name + "      " + types[i].jp, true, types[i].name.equalsIgnoreCase (current));
    }

    menu.addSeparator();
    menu.addItem (1, WT_U8 ("＋ 新しいタイプを追加..."));
    menu.addItem (2, WT_U8 ("(この枠を空にする)"));

    juce::Component::SafePointer<PalettePanel> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (screenArea),
                        [safe, slot, types] (int result) {
                            if (safe == nullptr || result == 0)
                                return;
                            if (result == 1)
                                safe->promptAddType (slot);
                            else if (result == 2)
                                safe->model.setTypeName (slot, {});
                            else
                                safe->model.setTypeName (slot, types[(size_t) result - 100].name);
                        });
}

void PalettePanel::fillSlotsFromCategory()
{
    const auto types = catalog.getTypes (model.getCategory());
    if (types.empty())
        return;

    const auto apply = [this, types] {
        for (int i = 0; i < CycleModel::numSlots; ++i)
            model.setTypeName (i, i < (int) types.size() ? types[(size_t) i].name : juce::String());
    };

    if (model.size() == 0)
    {
        apply();
        return;
    }

    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, WT_U8 ("Type を一括で入れ替え"),
                                        WT_U8 ("1〜9 の Type 名を、この Category の候補で置き換えます。\n"
                                               "すでに打ったサイクルの出力名も変わります。よろしいですか?"),
                                        WT_U8 ("置き換える"), WT_U8 ("キャンセル"), this,
                                        juce::ModalCallbackFunction::create ([apply] (int r) { if (r == 1) apply(); }));
}

void PalettePanel::reportWriteFailure (const juce::File& file)
{
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, WT_U8 ("Category / Type の表に書き込めませんでした"),
                                            file.getFullPathName() + WT_U8 ("\n\nExcel で開いたままになっていないか確認してください(閉じてからもう一度お試しください)。"));
}

void PalettePanel::promptAddCategory()
{
    auto* w = new juce::AlertWindow (WT_U8 ("Category を追加"),
                                     WT_U8 ("英語名(フォルダ名・ファイル名に使います)と日本語名を入力してください。\ncatalog.xlsx の Categories シートに追記されます。"),
                                     juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("name", "", "Category (EN)");
    w->addTextEditor ("jp", "", WT_U8 ("日本語"));
    w->addButton (WT_U8 ("追加"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton (WT_U8 ("キャンセル"), 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<PalettePanel> safe (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int result) {
        if (result != 1 || safe == nullptr)
            return;

        const auto name = w->getTextEditorContents ("name").trim().toUpperCase();
        if (name.isEmpty())
            return;

        if (safe->catalog.addCategory (name, w->getTextEditorContents ("jp")))
        {
            safe->model.setCategory (name);
            safe->catalog.noteUsed (name);
        }
        else
        {
            safe->reportWriteFailure (safe->catalog.getEditTarget());
        }
    }), true);
}

void PalettePanel::promptAddType (int slot)
{
    const auto cat = model.getCategory();
    auto* w = new juce::AlertWindow (WT_U8 ("Type を追加"),
                                     (cat.isEmpty() ? WT_U8 ("共通のタイプとして") : cat + WT_U8 (" に")) + WT_U8 (" catalog.xlsx の Types シートへ追記し、この枠に入れます。"),
                                     juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("name", "", "Type (EN)");
    w->addTextEditor ("jp", "", WT_U8 ("日本語"));
    w->addButton (WT_U8 ("追加"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton (WT_U8 ("キャンセル"), 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<PalettePanel> safe (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w, slot, cat] (int result) {
        if (result != 1 || safe == nullptr)
            return;

        const auto name = w->getTextEditorContents ("name").trim();
        if (name.isEmpty())
            return;

        if (safe->catalog.addType (cat, name, w->getTextEditorContents ("jp")))
            safe->model.setTypeName (slot, name);
        else
            safe->reportWriteFailure (safe->catalog.getEditTarget());
    }), true);
}

//==============================================================================
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
    prefixEditor.setBounds (top.removeFromLeft (100));
    top.removeFromLeft (8);
    patternLabel.setBounds (top.removeFromLeft (40));
    patternEditor.setBounds (top.removeFromLeft (190));
    top.removeFromLeft (10);
    categoryButton.setBounds (top.removeFromLeft (250));
    top.removeFromLeft (4);
    fillButton.setBounds (top.removeFromLeft (84));
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
