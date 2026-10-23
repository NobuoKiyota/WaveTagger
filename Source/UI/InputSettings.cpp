#include "InputSettings.h"
#include "Theme.h"
#include "Util/Text.h"

namespace wt
{
namespace
{
constexpr int rowHeight = 30;
}

class ShortcutSettingsComponent::CaptureButton : public juce::TextButton
{
public:
    CaptureButton() { setWantsKeyboardFocus (true); }

    std::function<void (const juce::KeyPress&)> onKey;
    std::function<void()> onCancelled;

    void startCapture()
    {
        capturing = true;
        setButtonText (WT_U8 ("キーを押してください..."));
        grabKeyboardFocus();
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (! capturing)
            return juce::TextButton::keyPressed (key);

        capturing = false;
        if (key == juce::KeyPress (juce::KeyPress::escapeKey))
        {
            if (onCancelled)
                onCancelled();
        }
        else if (onKey)
        {
            onKey (key);
        }
        return true;
    }

    void focusLost (FocusChangeType) override
    {
        if (capturing)
        {
            capturing = false;
            if (onCancelled)
                onCancelled();
        }
    }

private:
    bool capturing = false;
};

class ShortcutSettingsComponent::Rows : public juce::Component
{
public:
    explicit Rows (ShortcutSettingsComponent& o) : owner (o)
    {
        for (int a = 1; a < (int) Action::Count; ++a)
        {
            auto* label = labels.add (new juce::Label ({}, KeyMap::label ((Action) a)));
            label->setColour (juce::Label::textColourId, theme::text);
            addAndMakeVisible (label);

            auto* button = buttons.add (new CaptureButton());
            const auto action = (Action) a;
            button->onClick = [button] { button->startCapture(); };
            button->onKey = [this, action] (const juce::KeyPress& key) {
                const auto taken = owner.map.bindSingle (action, key);
                owner.message.setText (taken == Action::None ? juce::String()
                                                              : WT_U8 ("「") + KeyMap::label (taken) + WT_U8 ("」から外しました (同じキーは 1 つの動作にだけ割り当てられます)"),
                                       juce::dontSendNotification);
                owner.refreshTexts();
                if (owner.changed)
                    owner.changed();
            };
            button->onCancelled = [this] { owner.refreshTexts(); };
            addAndMakeVisible (button);

            auto* clear = clears.add (new juce::TextButton (WT_U8 ("解除")));
            clear->setWantsKeyboardFocus (false);
            clear->onClick = [this, action] {
                owner.map.clear (action);
                owner.refreshTexts();
                if (owner.changed)
                    owner.changed();
            };
            addAndMakeVisible (clear);
        }
    }

    void refresh()
    {
        for (int i = 0; i < buttons.size(); ++i)
            buttons[i]->setButtonText (owner.map.describe ((Action) (i + 1)));
    }

    int getContentHeight() const { return labels.size() * rowHeight + 8; }

    void resized() override
    {
        auto r = getLocalBounds().reduced (6, 4);
        for (int i = 0; i < labels.size(); ++i)
        {
            auto row = r.removeFromTop (rowHeight);
            labels[i]->setBounds (row.removeFromLeft (210));
            clears[i]->setBounds (row.removeFromRight (60).reduced (0, 2));
            row.removeFromRight (6);
            buttons[i]->setBounds (row.reduced (0, 2));
        }
    }

private:
    ShortcutSettingsComponent& owner;
    juce::OwnedArray<juce::Label> labels;
    juce::OwnedArray<CaptureButton> buttons;
    juce::OwnedArray<juce::TextButton> clears;
};

ShortcutSettingsComponent::ShortcutSettingsComponent (KeyMap& m, std::function<void()> onChanged)
    : map (m), changed (std::move (onChanged))
{
    rows = std::make_unique<Rows> (*this);
    viewport.setViewedComponent (rows.get(), false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    message.setColour (juce::Label::textColourId, juce::Colour (0xffffc533));
    message.setText (WT_U8 ("キー欄をクリックして、割り当てたいキー(Ctrl などの組み合わせも可)を押します。Esc で取消。"), juce::dontSendNotification);
    addAndMakeVisible (message);

    resetButton.setButtonText (WT_U8 ("初期設定に戻す"));
    resetButton.setWantsKeyboardFocus (false);
    resetButton.onClick = [this] {
        map.reset();
        refreshTexts();
        if (changed)
            changed();
    };
    addAndMakeVisible (resetButton);

    refreshTexts();
}

ShortcutSettingsComponent::~ShortcutSettingsComponent() = default;

void ShortcutSettingsComponent::refreshTexts()
{
    rows->refresh();
}

void ShortcutSettingsComponent::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
}

void ShortcutSettingsComponent::resized()
{
    auto r = getLocalBounds().reduced (10, 8);
    message.setBounds (r.removeFromTop (24));
    r.removeFromTop (4);
    resetButton.setBounds (r.removeFromBottom (28).removeFromLeft (180));
    r.removeFromBottom (6);

    viewport.setBounds (r);
    rows->setSize (viewport.getMaximumVisibleWidth(), rows->getContentHeight());
}

FolderSettingsComponent::FolderSettingsComponent (std::function<juce::File()> get, std::function<juce::String (const juce::File&)> choose)
    : getFolder (std::move (get)), onChoose (std::move (choose))
{
    intro.setText (WT_U8 ("Category / Type の表 (catalog.xlsx)、ショートカットキー、マウスホイール、色、命名の設定は、次のフォルダに保存されます。\n"
                          "Google Drive のミラー上にあるフォルダを選べば、どの PC でも同じ設定になります。\n"
                          "ライブラリの場所と書き出し先は、PC ごとに別々に覚えます。"),
                   juce::dontSendNotification);
    intro.setJustificationType (juce::Justification::topLeft);
    intro.setColour (juce::Label::textColourId, theme::textDim);
    addAndMakeVisible (intro);

    pathLabel.setText (getFolder().getFullPathName(), juce::dontSendNotification);
    pathLabel.setColour (juce::Label::textColourId, theme::text);
    pathLabel.setColour (juce::Label::backgroundColourId, theme::panelLight);
    pathLabel.setFont (juce::FontOptions (14.0f));
    pathLabel.setBorderSize ({ 4, 8, 4, 8 });
    addAndMakeVisible (pathLabel);

    status.setColour (juce::Label::textColourId, juce::Colour (0xffffc533));
    addAndMakeVisible (status);

    changeButton.setButtonText (WT_U8 ("フォルダを変更..."));
    changeButton.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser> (WT_U8 ("設定フォルダを選択 (中のファイルは新しい場所へコピーされます)"), getFolder());
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [this] (const juce::FileChooser& fc) {
                                  const auto dir = fc.getResult();
                                  if (! dir.isDirectory() && ! dir.getParentDirectory().isDirectory())
                                      return;
                                  status.setText (onChoose (dir), juce::dontSendNotification);
                                  pathLabel.setText (getFolder().getFullPathName(), juce::dontSendNotification);
                              });
    };
    addAndMakeVisible (changeButton);

    openButton.setButtonText (WT_U8 ("エクスプローラーで開く"));
    openButton.onClick = [this] { getFolder().revealToUser(); };
    addAndMakeVisible (openButton);
}

void FolderSettingsComponent::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
}

void FolderSettingsComponent::resized()
{
    auto r = getLocalBounds().reduced (14, 12);
    intro.setBounds (r.removeFromTop (74));
    r.removeFromTop (8);
    pathLabel.setBounds (r.removeFromTop (30));
    r.removeFromTop (10);
    auto buttons = r.removeFromTop (30);
    changeButton.setBounds (buttons.removeFromLeft (170));
    buttons.removeFromLeft (8);
    openButton.setBounds (buttons.removeFromLeft (190));
    r.removeFromTop (12);
    status.setBounds (r.removeFromTop (60));
}

InputSettingsComponent::InputSettingsComponent (WheelMap& wheel, KeyMap& keys, juce::AudioDeviceManager& devices, std::function<void()> onChanged,
                                                std::function<juce::File()> getFolder, std::function<juce::String (const juce::File&)> onChooseFolder)
    : juce::TabbedComponent (juce::TabbedButtonBar::TabsAtTop)
{
    setColour (juce::TabbedComponent::backgroundColourId, theme::panel);
    setTabBarDepth (32);
    addTab (WT_U8 ("マウスホイール"), theme::panel, new MouseSettingsComponent (wheel, onChanged), true);
    addTab (WT_U8 ("ショートカットキー"), theme::panel, new ShortcutSettingsComponent (keys, onChanged), true);
    addTab (WT_U8 ("設定フォルダ"), theme::panel, new FolderSettingsComponent (std::move (getFolder), std::move (onChooseFolder)), true);
    auto* audioSettings = new juce::AudioDeviceSelectorComponent (devices, 0, 0, 1, 2, false, false, true, false);
    audioSettings->setSize (520, 360);
    addTab (WT_U8 ("オーディオ"), theme::panel, audioSettings, true);
    setSize (800, 580);
}
} // namespace wt
