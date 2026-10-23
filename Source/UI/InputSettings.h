#pragma once

#include "InputMap.h"
#include "KeyMap.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace wt
{
/** Click a key box, press the new key. Taking a key that another action uses moves it. */
class ShortcutSettingsComponent : public juce::Component
{
public:
    ShortcutSettingsComponent (KeyMap& map, std::function<void()> onChanged);
    ~ShortcutSettingsComponent() override;

    void resized() override;
    void paint (juce::Graphics& g) override;

private:
    class CaptureButton;
    class Rows;

    void refreshTexts();

    KeyMap& map;
    std::function<void()> changed;
    std::unique_ptr<Rows> rows;
    juce::Viewport viewport;
    juce::Label message;
    juce::TextButton resetButton { "Reset to defaults" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShortcutSettingsComponent)
};

/** Tabs: mouse wheel assignments and keyboard shortcuts. */
class InputSettingsComponent : public juce::TabbedComponent
{
public:
    InputSettingsComponent (WheelMap& wheel, KeyMap& keys, std::function<void()> onChanged);
};
} // namespace wt
