#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace wt
{
enum class Action
{
    None = 0,
    Open, Library, Export, SelectAll, Undo, Redo,
    PlayStop, Confirm, Cancel, Delete,
    ToStart, ToEnd, ZoomFit, ZoomSelection, ToggleLoop, ZoomIn, ZoomOut, ScrollLeft, ScrollRight,
    AutoSplit, ImportMarkers, SaveWav, AcceptSuggestion,
    Type1, Type2, Type3, Type4, Type5, Type6, Type7, Type8, Type9,
    Count
};

/** Keyboard shortcuts: each action can have several keys (e.g. "+" and "="). Editable, persisted, with defaults. */
class KeyMap
{
public:
    KeyMap() { reset(); }

    Action find (const juce::KeyPress& key) const;

    /** Gives the action exactly this one key and removes the key from any other action; returns the action it was taken from. */
    Action bindSingle (Action a, const juce::KeyPress& key);
    void clear (Action a) { bindings[(size_t) a].clear(); }
    void reset();

    juce::String describe (Action a) const;
    static juce::String label (Action a);

    juce::var toVar() const;
    void fromVar (const juce::var& v);

private:
    std::array<juce::Array<juce::KeyPress>, (size_t) Action::Count> bindings;
};
} // namespace wt
