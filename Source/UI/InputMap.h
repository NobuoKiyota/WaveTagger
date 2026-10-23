#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace wt
{
enum class WheelContext { Wave = 0, FreqAxis, DbAxis, Count };
enum class ModKey { None = 0, Shift, Alt, Ctrl, Count };
enum class WheelAction { None = 0, TimeZoom, TimeScroll, FreqZoom, FreqScroll, GainZoom, Count };

/** What the mouse wheel does for each (area, modifier key) pair. Defaults follow RX. Editable and persisted. */
class WheelMap
{
public:
    WheelMap() { reset(); }

    WheelAction get (WheelContext c, ModKey m) const { return table[(size_t) c][(size_t) m]; }
    void set (WheelContext c, ModKey m, WheelAction a) { table[(size_t) c][(size_t) m] = a; }
    void reset();

    static ModKey modKeyFor (const juce::ModifierKeys& mods);
    static juce::String actionName (WheelAction a);
    static juce::String contextName (WheelContext c);
    static juce::String modKeyName (ModKey m);

    juce::var toVar() const;
    void fromVar (const juce::var& v);

private:
    std::array<std::array<WheelAction, (size_t) ModKey::Count>, (size_t) WheelContext::Count> table {};
};

/** Grid of dropdowns: rows = area, columns = modifier key. */
class MouseSettingsComponent : public juce::Component
{
public:
    MouseSettingsComponent (WheelMap& map, std::function<void()> onChanged);

    void resized() override;
    void paint (juce::Graphics& g) override;

private:
    WheelMap& map;
    std::function<void()> changed;
    juce::OwnedArray<juce::ComboBox> combos;
    juce::OwnedArray<juce::Label> labels;
    juce::TextButton resetButton { "Reset to RX defaults" };

    void syncFromMap();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MouseSettingsComponent)
};
} // namespace wt
