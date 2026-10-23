#include "ConditioningPanel.h"
#include "Theme.h"
#include "Util/Text.h"

namespace wt
{
namespace
{
constexpr int margin = 14;
constexpr int rowHeight = 28;

void styleEditor (juce::TextEditor& e)
{
    e.setColour (juce::TextEditor::backgroundColourId, theme::panelLight);
    e.setColour (juce::TextEditor::textColourId, theme::text);
    e.setColour (juce::TextEditor::outlineColourId, theme::outline);
    e.setJustification (juce::Justification::centredRight);
    e.setInputRestrictions (6, "0123456789.-");
}

juce::String numberText (double v)
{
    return juce::String (v, 1).trimCharactersAtEnd ("0").trimCharactersAtEnd (".");
}
} // namespace

ConditioningPanel::ConditioningPanel (const ConditionSettings& initial, Hooks h)
    : settings (initial), hooks (std::move (h))
{
    const auto label = [this] (juce::Label& l, const juce::String& text) {
        l.setText (text, juce::dontSendNotification);
        l.setColour (juce::Label::textColourId, theme::textDim);
        addAndMakeVisible (l);
    };
    label (presetLabel, WT_U8 ("プリセット"));
    label (humLabel, WT_U8 ("ハム除去"));
    label (normLabel, WT_U8 ("ノーマライズ"));
    label (targetLabel, WT_U8 ("目標"));
    label (ceilingLabel, WT_U8 ("ピークの上限 (dBFS)"));
    label (cutHzLabel, "Hz");

    presetBox.addItem (WT_U8 ("オフ (何もしない)"), 1);
    presetBox.addItem (WT_U8 ("SFX 標準"), 2);
    presetBox.addItem (WT_U8 ("カスタム"), 3);
    humBox.addItem (WT_U8 ("オフ"), 1);
    humBox.addItem (WT_U8 ("自動 (50 / 60 Hz を判定)"), 2);
    humBox.addItem (WT_U8 ("50 Hz 固定"), 3);
    humBox.addItem (WT_U8 ("60 Hz 固定"), 4);
    cutOrderBox.addItem ("12 dB/oct", 1);
    cutOrderBox.addItem ("24 dB/oct", 2);
    normBox.addItem (WT_U8 ("オフ"), 1);
    normBox.addItem (WT_U8 ("Peak (dBFS)"), 2);
    normBox.addItem (WT_U8 ("LUFS (ラウドネス)"), 3);
    for (auto* b : { &presetBox, &humBox, &cutOrderBox, &normBox })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }

    dcToggle.setButtonText (WT_U8 ("DC オフセットを除去"));
    cutToggle.setButtonText (WT_U8 ("ローカット"));
    ditherToggle.setButtonText (WT_U8 ("16bit 書き出し時にディザを掛ける"));
    gainToggle.setButtonText (WT_U8 ("A/B 試聴にノーマライズの音量補正も含める"));
    for (auto* t : { &dcToggle, &cutToggle, &ditherToggle, &gainToggle })
    {
        t->setWantsKeyboardFocus (false);
        addAndMakeVisible (t);
    }

    for (auto* e : { &cutHz, &target, &ceiling })
    {
        styleEditor (*e);
        addAndMakeVisible (e);
    }

    humStatusLabel.setColour (juce::Label::textColourId, theme::accent);
    selectionLabel.setColour (juce::Label::textColourId, theme::text);
    noteLabel.setColour (juce::Label::textColourId, theme::textDim);
    noteLabel.setFont (juce::FontOptions (12.0f));
    noteLabel.setMinimumHorizontalScale (1.0f);
    noteLabel.setText (WT_U8 ("A/B 試聴は B キー (ツールバーの「B: 整音後」)。元の録音は変更せず、書き出すファイルにだけ掛けます。"),
                       juce::dontSendNotification);
    for (auto* l : { &humStatusLabel, &selectionLabel, &noteLabel })
        addAndMakeVisible (l);

    presetBox.onChange = [this] {
        if (! updating)
            applyPreset();
    };
    for (auto* b : { &humBox, &cutOrderBox, &normBox })
        b->onChange = [this] { edited(); };
    for (auto* t : { &dcToggle, &cutToggle, &ditherToggle, &gainToggle })
        t->onClick = [this] { edited(); };
    for (auto* e : { &cutHz, &target, &ceiling })
    {
        e->onReturnKey = [this] { edited(); };
        e->onFocusLost = [this] { edited(); };
    }

    showSettings();
    setSize (520, 420);
}

void ConditioningPanel::setSettings (const ConditionSettings& s)
{
    settings = s;
    showSettings();
}

void ConditioningPanel::showSettings()
{
    const juce::ScopedValueSetter<bool> guard (updating, true);
    const auto& o = settings.options;

    presetBox.setSelectedId ((int) settings.preset + 1, juce::dontSendNotification);
    dcToggle.setToggleState (o.dcRemove, juce::dontSendNotification);
    humBox.setSelectedId ((int) o.humMode + 1, juce::dontSendNotification);
    cutToggle.setToggleState (o.lowCutHz > 0.0f, juce::dontSendNotification);
    cutHz.setText (numberText (o.lowCutHz > 0.0f ? o.lowCutHz : 20.0), false);
    cutOrderBox.setSelectedId (o.lowCutOrder >= 4 ? 2 : 1, juce::dontSendNotification);
    normBox.setSelectedId ((int) o.normalize + 1, juce::dontSendNotification);
    target.setText (numberText (o.targetDb), false);
    ceiling.setText (numberText (o.ceilingDb), false);
    ditherToggle.setToggleState (o.dither, juce::dontSendNotification);
    gainToggle.setToggleState (settings.previewGain, juce::dontSendNotification);

    const auto normOn = o.normalize != NormalizeMode::off;
    targetLabel.setText (o.normalize == NormalizeMode::lufs ? WT_U8 ("目標 (LUFS)") : WT_U8 ("目標 (dBFS)"), juce::dontSendNotification);
    target.setEnabled (normOn);
    ceiling.setEnabled (normOn);
    gainToggle.setEnabled (normOn);
    cutHz.setEnabled (o.lowCutHz > 0.0f);
    cutOrderBox.setEnabled (o.lowCutHz > 0.0f);
    updateReadouts();
}

void ConditioningPanel::applyPreset()
{
    const auto preset = (ConditionPreset) (presetBox.getSelectedId() - 1);
    if (preset == ConditionPreset::custom)
    {
        settings.preset = preset;
        return;
    }

    settings.preset = preset;
    settings.options = presetOptions (preset);
    showSettings();
    if (hooks.changed)
        hooks.changed (settings);
}

void ConditioningPanel::edited()
{
    if (updating)
        return;

    auto& o = settings.options;
    o.dcRemove = dcToggle.getToggleState();
    o.humMode = (HumMode) (humBox.getSelectedId() - 1);
    o.lowCutHz = cutToggle.getToggleState() ? (float) juce::jlimit (10.0, 500.0, cutHz.getText().getDoubleValue()) : 0.0f;
    o.lowCutOrder = cutOrderBox.getSelectedId() == 2 ? 4 : 2;
    o.normalize = (NormalizeMode) (normBox.getSelectedId() - 1);
    o.targetDb = juce::jlimit (-60.0, 0.0, target.getText().getDoubleValue());
    o.ceilingDb = juce::jlimit (-24.0, 0.0, ceiling.getText().getDoubleValue());
    o.dither = ditherToggle.getToggleState();
    settings.previewGain = gainToggle.getToggleState();

    // editing by hand turns a preset into "custom"; coming back to a preset's exact values names it again
    settings.preset = o == presetOptions (ConditionPreset::off)        ? ConditionPreset::off
                      : o == presetOptions (ConditionPreset::standard) ? ConditionPreset::standard
                                                                       : ConditionPreset::custom;
    showSettings();
    if (hooks.changed)
        hooks.changed (settings);
}

void ConditioningPanel::updateReadouts()
{
    humStatusLabel.setText (settings.options.humMode == HumMode::off ? juce::String() : WT_U8 ("この録音のハム: ") + (hooks.humStatus ? hooks.humStatus() : juce::String()),
                            juce::dontSendNotification);
    selectionLabel.setText (hooks.selectionInfo ? hooks.selectionInfo() : juce::String(), juce::dontSendNotification);
}

void ConditioningPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    g.setColour (theme::outline);
    const auto line = [&] (int y) { g.drawHorizontalLine (y, (float) margin, (float) getWidth() - margin); };
    line (presetBox.getBottom() + 10);
    line (selectionLabel.getY() - 8);
}

void ConditioningPanel::resized()
{
    auto r = getLocalBounds().reduced (margin);
    const auto labelWidth = 104;
    const auto row = [&] { auto a = r.removeFromTop (rowHeight); r.removeFromTop (6); return a; };

    auto a = row();
    presetLabel.setBounds (a.removeFromLeft (labelWidth));
    presetBox.setBounds (a.removeFromLeft (200));
    r.removeFromTop (12);

    dcToggle.setBounds (row());

    a = row();
    humLabel.setBounds (a.removeFromLeft (labelWidth));
    humBox.setBounds (a.removeFromLeft (200));
    a = row();
    a.removeFromLeft (labelWidth);
    humStatusLabel.setBounds (a);

    a = row();
    cutToggle.setBounds (a.removeFromLeft (labelWidth));
    cutHz.setBounds (a.removeFromLeft (60));
    cutHzLabel.setBounds (a.removeFromLeft (30).withTrimmedLeft (6));
    a.removeFromLeft (10);
    cutOrderBox.setBounds (a.removeFromLeft (110));

    a = row();
    normLabel.setBounds (a.removeFromLeft (labelWidth));
    normBox.setBounds (a.removeFromLeft (170));
    a = row();
    targetLabel.setBounds (a.removeFromLeft (labelWidth));
    target.setBounds (a.removeFromLeft (60));
    a.removeFromLeft (16);
    ceilingLabel.setBounds (a.removeFromLeft (130));
    ceiling.setBounds (a.removeFromLeft (60));

    ditherToggle.setBounds (row());
    gainToggle.setBounds (row());

    r.removeFromTop (14);
    selectionLabel.setBounds (r.removeFromTop (rowHeight));
    noteLabel.setBounds (r.removeFromTop (36));
}

ConditioningWindow::ConditioningWindow (const ConditionSettings& initial, ConditioningPanel::Hooks hooks)
    : DocumentWindow (WT_U8 ("整音"), theme::background, DocumentWindow::closeButton | DocumentWindow::minimiseButton)
{
    setUsingNativeTitleBar (true);
    panel = new ConditioningPanel (initial, std::move (hooks));
    setContentOwned (panel, true);
    setResizable (false, false);
    setAlwaysOnTop (true);
    centreWithSize (getWidth(), getHeight());
}
} // namespace wt
