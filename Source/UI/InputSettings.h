#pragma once

#include "InputMap.h"
#include "KeyMap.h"
#include <juce_audio_utils/juce_audio_utils.h>
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

/** Where the shared settings / Category-Type workbook live, and a button to move them. */
class FolderSettingsComponent : public juce::Component
{
public:
    /** onChoose receives the new folder and returns a message to show (e.g. how many files were copied). */
    FolderSettingsComponent (std::function<juce::File()> getFolder, std::function<juce::String (const juce::File&)> onChoose);

    void resized() override;
    void paint (juce::Graphics& g) override;

private:
    std::function<juce::File()> getFolder;
    std::function<juce::String (const juce::File&)> onChoose;
    juce::Label intro, pathLabel, status;
    juce::TextButton changeButton, openButton;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FolderSettingsComponent)
};

/** Tabs: mouse wheel assignments, keyboard shortcuts, shared settings folder, audio device. */
class InputSettingsComponent : public juce::TabbedComponent
{
public:
    InputSettingsComponent (WheelMap& wheel, KeyMap& keys, juce::AudioDeviceManager& devices, std::function<void()> onChanged,
                            std::function<juce::File()> getFolder, std::function<juce::String (const juce::File&)> onChooseFolder);
};
} // namespace wt
