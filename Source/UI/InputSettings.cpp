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

InputSettingsComponent::InputSettingsComponent (WheelMap& wheel, KeyMap& keys, std::function<void()> onChanged)
    : juce::TabbedComponent (juce::TabbedButtonBar::TabsAtTop)
{
    setColour (juce::TabbedComponent::backgroundColourId, theme::panel);
    setTabBarDepth (32);
    addTab (WT_U8 ("マウスホイール"), theme::panel, new MouseSettingsComponent (wheel, onChanged), true);
    addTab (WT_U8 ("ショートカットキー"), theme::panel, new ShortcutSettingsComponent (keys, onChanged), true);
    setSize (800, 580);
}
} // namespace wt
