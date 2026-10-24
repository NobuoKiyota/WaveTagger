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
    juce::String autoType;  // what the suggestion engine proposed; kept even if the user then changed it (for the learning log)
    float autoConf = -1.0f; // its confidence, or -1 if nothing was suggested
    bool confirmed = true;  // false: typed automatically (Inbox / "apply suggestions") and not looked at yet
    int rating = 0;         // 0 = unrated, 1..5 stars
    bool ng = false;
    juce::String attributes; // free text such as "strength=strong material=cardboard"
};

/** Cycles (sorted by start), the 9-slot type palette, naming template and undo history. */
class CycleModel : public juce::ChangeBroadcaster
{
public:
    static constexpr int numSlots = 9;

    CycleModel();

    //== palette / naming
    const MarkerType& getType (int slot) const { return types[(size_t) slot]; }
    /** "<n>  <Type>   <Japanese>" for lists and menus; the Japanese comes from whoever set the lookup (the catalog). */
    std::function<juce::String (const juce::String& typeName)> japaneseOf;
    juce::String describeType (int slot) const
    {
        auto text = juce::String (slot + 1) + "  " + types[(size_t) slot].name;
        const auto jp = japaneseOf ? japaneseOf (types[(size_t) slot].name) : juce::String();
        return jp.isNotEmpty() ? text + "   " + jp : text;
    }
    bool isTypeUsable (int slot) const { return slot >= 0 && slot < numSlots && types[(size_t) slot].name.isNotEmpty(); }
    void setTypeName (int slot, const juce::String& name);

    const juce::String& getPrefix() const { return prefix; }
    const juce::String& getPattern() const { return pattern; }
    void setPrefix (const juce::String& p);
    void setPattern (const juce::String& p);

    const juce::String& getCategory() const { return category; }
    void setCategory (const juce::String& c);

    /** Text of the file name before the number, e.g. "pen_push"; false if the pattern has no {num} or text after it. */
    bool getNameStem (int type, juce::String& stem) const;
    /** Highest number already used in the library per slot, so new files continue from there. */
    void setNumberOffsets (const std::array<int, numSlots>& offsets);
    int getNumberOffset (int slot) const { return slot >= 0 && slot < numSlots ? numberOffsets[(size_t) slot] : 0; }

    //== cycles
    int size() const { return (int) cycles.size(); }
    const Cycle& operator[] (int i) const { return cycles[(size_t) i]; }

    /** Returns the index of the new cycle after sorting. */
    int add (juce::int64 start, juce::int64 end, int type);
    void setType (const juce::Array<int>& indices, int type);
    void setRange (int index, juce::int64 start, juce::int64 end);
    /** Replace range and type in one undo step; returns the new index. */
    int update (int index, juce::int64 start, juce::int64 end, int type);

    /** Drag editing: beginEdit() once (one undo step), setRangeLive() while dragging, finishEdit() to re-sort. */
    void beginEdit() { pushUndo(); }
    void setRangeLive (int index, juce::int64 start, juce::int64 end);
    int finishEdit (int index);
    void remove (const juce::Array<int>& indices);
    void setRating (const juce::Array<int>& indices, int stars);
    void setNg (const juce::Array<int>& indices, bool ng);
    void setAttributes (int index, const juce::String& text);
    /** The same text on several cycles, as one undo step. */
    void setAttributes (const juce::Array<int>& indices, const juce::String& text);
    /** Records a suggestion on a cycle. If `apply`, the type is set too (and the cycle stays unconfirmed). */
    /** Several suggestions at once (one change notification). typeName empty = clear. Never typed, never an undo step. */
    void setSuggestions (const std::vector<int>& indices, const juce::StringArray& typeNames, const std::vector<float>& confidences);
    void setSuggestion (int index, const juce::String& typeName, float confidence, int typeSlot, bool apply);
    /** The user has looked at everything (e.g. saved to the WAV): no cycle is 'unconfirmed' any more. No undo step. */
    void markAllConfirmed();
    void replaceAll (std::vector<Cycle> newCycles, bool undoable = true);

    int findAt (juce::int64 sample) const;
    bool overlapsAny (juce::int64 start, juce::int64 end) const;

    /** Number among cycles of the same type in time order (1-based), plus the library offset. */
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
    juce::String category;
    std::array<int, numSlots> numberOffsets {};
    std::vector<Cycle> cycles;
    std::vector<std::vector<Cycle>> undoStack, redoStack;
};
} // namespace wt
