#pragma once

#include <juce_events/juce_events.h>
#include <juce_graphics/juce_graphics.h>

namespace wt
{
struct MarkerType
{
    juce::String name;
    juce::Colour colour;
};

/** A marked region. type: 0..8 = palette slot, -1 = untyped. */
struct Cycle
{
    juce::int64 start = 0, end = 0;
    int type = -1;
};

/** Cycles (sorted by start), the 9-slot type palette, naming template and undo history. */
class CycleModel : public juce::ChangeBroadcaster
{
public:
    static constexpr int numSlots = 9;

    CycleModel();

    //== palette / naming
    const MarkerType& getType (int slot) const { return types[(size_t) slot]; }
    bool isTypeUsable (int slot) const { return slot >= 0 && slot < numSlots && types[(size_t) slot].name.isNotEmpty(); }
    void setTypeName (int slot, const juce::String& name);

    const juce::String& getPrefix() const { return prefix; }
    const juce::String& getPattern() const { return pattern; }
    void setPrefix (const juce::String& p);
    void setPattern (const juce::String& p);

    //== cycles
    int size() const { return (int) cycles.size(); }
    const Cycle& operator[] (int i) const { return cycles[(size_t) i]; }

    /** Returns the index of the new cycle after sorting. */
    int add (juce::int64 start, juce::int64 end, int type);
    void setType (const juce::Array<int>& indices, int type);
    void setRange (int index, juce::int64 start, juce::int64 end);
    void remove (const juce::Array<int>& indices);
    void replaceAll (std::vector<Cycle> newCycles, bool undoable = true);

    int findAt (juce::int64 sample) const;
    bool overlapsAny (juce::int64 start, juce::int64 end) const;

    /** 1-based number of this cycle among cycles of the same type, in time order. */
    int numberOf (int index) const;
    int countOfType (int type) const;
    juce::String nameOf (int index) const;
    juce::String nameFor (int type, int number) const;
    /** The type name used in file names / metadata. */
    juce::String typeNameOf (int type) const;

    //== undo
    bool canUndo() const { return ! undoStack.empty(); }
    bool canRedo() const { return ! redoStack.empty(); }
    void undo();
    void redo();

    //== persistence
    juce::var cyclesToVar() const;
    void cyclesFromVar (const juce::var& v);
    juce::var settingsToVar() const;
    void settingsFromVar (const juce::var& v);

private:
    void pushUndo();
    void sortCycles();
    void changed() { sendSynchronousChangeMessage(); }

    std::array<MarkerType, numSlots> types;
    juce::String prefix { "pen" };
    juce::String pattern { "{prefix}_{type}{num:02}" };
    std::vector<Cycle> cycles;
    std::vector<std::vector<Cycle>> undoStack, redoStack;
};
} // namespace wt
