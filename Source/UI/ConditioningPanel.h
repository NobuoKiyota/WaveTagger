#pragma once

#include "Core/Conditioning.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace wt
{
/** The conditioning chain's settings. The owner supplies the readouts (hum found in this recording, the numbers for what is
    selected) and listens for changes. */
class ConditioningPanel : public juce::Component
{
public:
    struct Hooks
    {
        std::function<void (const ConditionSettings&)> changed;
        std::function<juce::String()> humStatus;     // e.g. "60 Hz (4 harmonics)" / "none" / "looking..."
        std::function<juce::String()> selectionInfo; // gain for the cycle that is selected
    };

    ConditioningPanel (const ConditionSettings& initial, Hooks hooks);

    void setSettings (const ConditionSettings& s);
    const ConditionSettings& getSettings() const { return settings; }

    /** Refreshes the two readouts. */
    void updateReadouts();

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void edited();
    void showSettings();
    void applyPreset();

    ConditionSettings settings;
    Hooks hooks;
    bool updating = false;

    juce::Label presetLabel, humLabel, cutLabel, cutHzLabel, normLabel, targetLabel, ceilingLabel, humStatusLabel, selectionLabel, noteLabel;
    juce::ComboBox presetBox, humBox, cutOrderBox, normBox;
    juce::ToggleButton dcToggle, cutToggle, ditherToggle, gainToggle;
    juce::TextEditor cutHz, target, ceiling;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ConditioningPanel)
};

class ConditioningWindow : public juce::DocumentWindow
{
public:
    ConditioningWindow (const ConditionSettings& initial, ConditioningPanel::Hooks hooks);
    ConditioningPanel& getPanel() { return *panel; }
    void closeButtonPressed() override { setVisible (false); }

private:
    ConditioningPanel* panel = nullptr;
};
} // namespace wt
