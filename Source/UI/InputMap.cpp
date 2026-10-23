#include "InputMap.h"
#include "Theme.h"
#include "Util/Text.h"

namespace wt
{
namespace
{
constexpr int numContexts = (int) WheelContext::Count;
constexpr int numMods = (int) ModKey::Count;
constexpr int numActions = (int) WheelAction::Count;

const char* const contextKeys[] = { "wave", "freq", "db" };
const char* const modKeys[] = { "none", "shift", "alt", "ctrl" };
const char* const actionKeys[] = { "none", "timeZoom", "timeScroll", "freqZoom", "freqScroll", "gainZoom" };
} // namespace

void WheelMap::reset()
{
    for (auto& row : table)
        row.fill (WheelAction::None);

    auto& wave = table[(size_t) WheelContext::Wave];
    wave[(size_t) ModKey::None] = WheelAction::TimeZoom;
    wave[(size_t) ModKey::Shift] = WheelAction::TimeScroll;
    wave[(size_t) ModKey::Alt] = WheelAction::FreqZoom;
    wave[(size_t) ModKey::Ctrl] = WheelAction::GainZoom;

    auto& freq = table[(size_t) WheelContext::FreqAxis];
    freq[(size_t) ModKey::None] = WheelAction::FreqZoom;
    freq[(size_t) ModKey::Shift] = WheelAction::FreqScroll;
    freq[(size_t) ModKey::Alt] = WheelAction::FreqScroll;
    freq[(size_t) ModKey::Ctrl] = WheelAction::TimeZoom;

    auto& db = table[(size_t) WheelContext::DbAxis];
    db[(size_t) ModKey::None] = WheelAction::GainZoom;
    db[(size_t) ModKey::Ctrl] = WheelAction::TimeZoom;
}

ModKey WheelMap::modKeyFor (const juce::ModifierKeys& mods)
{
    if (mods.isCtrlDown() || mods.isCommandDown())
        return ModKey::Ctrl;
    if (mods.isAltDown())
        return ModKey::Alt;
    if (mods.isShiftDown())
        return ModKey::Shift;
    return ModKey::None;
}

juce::String WheelMap::actionName (WheelAction a)
{
    switch (a)
    {
        case WheelAction::TimeZoom:   return WT_U8 ("時間ズーム");
        case WheelAction::TimeScroll: return WT_U8 ("時間スクロール");
        case WheelAction::FreqZoom:   return WT_U8 ("周波数ズーム");
        case WheelAction::FreqScroll: return WT_U8 ("周波数スクロール");
        case WheelAction::GainZoom:   return WT_U8 ("波形の縦拡大");
        default:                      return WT_U8 ("なし");
    }
}

juce::String WheelMap::contextName (WheelContext c)
{
    switch (c)
    {
        case WheelContext::FreqAxis: return WT_U8 ("周波数ラベル上");
        case WheelContext::DbAxis:   return WT_U8 ("dB ラベル上");
        default:                     return WT_U8 ("波形 / スペクトログラム上");
    }
}

juce::String WheelMap::modKeyName (ModKey m)
{
    switch (m)
    {
        case ModKey::Shift: return "Shift";
        case ModKey::Alt:   return "Alt";
        case ModKey::Ctrl:  return "Ctrl";
        default:            return WT_U8 ("(なし)");
    }
}

juce::var WheelMap::toVar() const
{
    auto* root = new juce::DynamicObject();
    for (int c = 0; c < numContexts; ++c)
    {
        auto* row = new juce::DynamicObject();
        for (int m = 0; m < numMods; ++m)
            row->setProperty (modKeys[m], actionKeys[(int) table[(size_t) c][(size_t) m]]);
        root->setProperty (contextKeys[c], juce::var (row));
    }
    return juce::var (root);
}

void WheelMap::fromVar (const juce::var& v)
{
    if (! v.isObject())
        return;

    for (int c = 0; c < numContexts; ++c)
    {
        const auto row = v.getProperty (contextKeys[c], {});
        if (! row.isObject())
            continue;

        for (int m = 0; m < numMods; ++m)
        {
            const auto name = row.getProperty (modKeys[m], {}).toString();
            for (int a = 0; a < numActions; ++a)
                if (name == actionKeys[a])
                    table[(size_t) c][(size_t) m] = (WheelAction) a;
        }
    }
}

//==============================================================================
MouseSettingsComponent::MouseSettingsComponent (WheelMap& m, std::function<void()> onChanged)
    : map (m), changed (std::move (onChanged))
{
    for (int m2 = 0; m2 < numMods; ++m2)
    {
        auto* l = labels.add (new juce::Label ({}, WheelMap::modKeyName ((ModKey) m2) + " + " + WT_U8 ("ホイール")));
        l->setJustificationType (juce::Justification::centred);
        l->setColour (juce::Label::textColourId, theme::textDim);
        addAndMakeVisible (l);
    }

    for (int c = 0; c < numContexts; ++c)
    {
        auto* l = labels.add (new juce::Label ({}, WheelMap::contextName ((WheelContext) c)));
        l->setColour (juce::Label::textColourId, theme::text);
        addAndMakeVisible (l);

        for (int mk = 0; mk < numMods; ++mk)
        {
            auto* box = combos.add (new juce::ComboBox());
            for (int a = 0; a < numActions; ++a)
                box->addItem (WheelMap::actionName ((WheelAction) a), a + 1);
            box->onChange = [this, c, mk, box] {
                map.set ((WheelContext) c, (ModKey) mk, (WheelAction) (box->getSelectedId() - 1));
                if (changed)
                    changed();
            };
            addAndMakeVisible (box);
        }
    }

    resetButton.onClick = [this] {
        map.reset();
        syncFromMap();
        if (changed)
            changed();
    };
    addAndMakeVisible (resetButton);

    syncFromMap();
    setSize (760, 220);
}

void MouseSettingsComponent::syncFromMap()
{
    for (int c = 0; c < numContexts; ++c)
        for (int mk = 0; mk < numMods; ++mk)
            combos[c * numMods + mk]->setSelectedId ((int) map.get ((WheelContext) c, (ModKey) mk) + 1, juce::dontSendNotification);
}

void MouseSettingsComponent::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
}

void MouseSettingsComponent::resized()
{
    auto r = getLocalBounds().reduced (12, 10);
    const auto labelW = 200;
    const auto colW = (r.getWidth() - labelW) / numMods;

    auto header = r.removeFromTop (26);
    header.removeFromLeft (labelW);
    for (int m = 0; m < numMods; ++m)
        labels[m]->setBounds (header.removeFromLeft (colW));

    r.removeFromTop (6);
    for (int c = 0; c < numContexts; ++c)
    {
        auto row = r.removeFromTop (34);
        labels[numMods + c]->setBounds (row.removeFromLeft (labelW));
        for (int m = 0; m < numMods; ++m)
            combos[c * numMods + m]->setBounds (row.removeFromLeft (colW).reduced (4, 2));
    }

    r.removeFromTop (10);
    resetButton.setBounds (r.removeFromTop (28).removeFromLeft (200));
}
} // namespace wt
