#include "CycleModel.h"

namespace wt
{
namespace
{
constexpr size_t maxUndo = 200;

juce::String sanitizeFileName (juce::String s)
{
    for (const char* c = "\\/:*?\"<>|"; *c != 0; ++c)
        s = s.replaceCharacter ((juce::juce_wchar) *c, '_');
    return s.trim();
}
} // namespace

CycleModel::CycleModel()
{
    const juce::uint32 colours[numSlots] = { 0xff4fa3e0, 0xffe0894f, 0xff6cc070, 0xffc46bd1, 0xffe0cf4f,
                                             0xff4fd1c4, 0xffe05a6c, 0xff9a8be0, 0xffa0a8b0 };
    for (int i = 0; i < numSlots; ++i)
        types[(size_t) i].colour = juce::Colour (colours[i]);

    types[0].name = "push";
    types[1].name = "pull";
    types[2].name = "write";
}

void CycleModel::setTypeName (int slot, const juce::String& name)
{
    if (slot < 0 || slot >= numSlots || types[(size_t) slot].name == name)
        return;
    types[(size_t) slot].name = name;
    changed();
}

void CycleModel::setPrefix (const juce::String& p)
{
    if (p != prefix)
    {
        prefix = p;
        changed();
    }
}

void CycleModel::setPattern (const juce::String& p)
{
    if (p != pattern)
    {
        pattern = p;
        changed();
    }
}

void CycleModel::setCategory (const juce::String& c)
{
    if (c != category)
    {
        category = c;
        changed();
    }
}

void CycleModel::setNumberOffsets (const std::array<int, numSlots>& offsets)
{
    if (offsets != numberOffsets)
    {
        numberOffsets = offsets;
        changed();
    }
}

bool CycleModel::getNameStem (int type, juce::String& stem) const
{
    const auto out = pattern.replace ("{prefix}", prefix).replace ("{type}", typeNameOf (type));
    const auto i = out.indexOf ("{num");
    if (i < 0)
        return false;
    const auto j = out.indexOfChar (i, '}');
    if (j < 0 || j < out.length() - 1)
        return false;

    stem = sanitizeFileName (out.substring (0, i));
    return stem.isNotEmpty();
}

//==============================================================================
void CycleModel::pushUndo()
{
    undoStack.push_back (cycles);
    if (undoStack.size() > maxUndo)
        undoStack.erase (undoStack.begin());
    redoStack.clear();
}

void CycleModel::sortCycles()
{
    std::stable_sort (cycles.begin(), cycles.end(), [] (const Cycle& a, const Cycle& b) { return a.start < b.start; });
}

int CycleModel::add (juce::int64 start, juce::int64 end, int type)
{
    if (end < start)
        std::swap (start, end);

    pushUndo();
    cycles.push_back ({ start, end, type });
    sortCycles();

    int index = 0;
    for (int i = 0; i < size(); ++i)
        if (cycles[(size_t) i].start == start && cycles[(size_t) i].end == end && cycles[(size_t) i].type == type)
            index = i;

    changed();
    return index;
}

void CycleModel::setType (const juce::Array<int>& indices, int type)
{
    bool any = false;
    for (auto i : indices)
        any = any || (i >= 0 && i < size() && cycles[(size_t) i].type != type);
    if (! any)
        return;

    pushUndo();
    for (auto i : indices)
        if (i >= 0 && i < size())
        {
            cycles[(size_t) i].type = type;
            cycles[(size_t) i].confirmed = true; // a person decided
        }
    changed();
}

void CycleModel::setRange (int index, juce::int64 start, juce::int64 end)
{
    if (index < 0 || index >= size())
        return;
    if (end < start)
        std::swap (start, end);

    pushUndo();
    cycles[(size_t) index].start = start;
    cycles[(size_t) index].end = end;
    sortCycles();
    changed();
}

void CycleModel::setRating (const juce::Array<int>& indices, int stars)
{
    pushUndo();
    for (auto i : indices)
        if (i >= 0 && i < size())
            cycles[(size_t) i].rating = juce::jlimit (0, 5, stars);
    changed();
}

void CycleModel::setNg (const juce::Array<int>& indices, bool ng)
{
    pushUndo();
    for (auto i : indices)
        if (i >= 0 && i < size())
            cycles[(size_t) i].ng = ng;
    changed();
}

void CycleModel::setAttributes (const juce::Array<int>& indices, const juce::String& text)
{
    bool any = false;
    for (auto i : indices)
        any = any || (i >= 0 && i < size() && cycles[(size_t) i].attributes != text);
    if (! any)
        return;

    pushUndo();
    for (auto i : indices)
        if (i >= 0 && i < size())
            cycles[(size_t) i].attributes = text;
    changed();
}

void CycleModel::setAttributes (int index, const juce::String& text)
{
    if (index < 0 || index >= size() || cycles[(size_t) index].attributes == text)
        return;
    pushUndo();
    cycles[(size_t) index].attributes = text;
    changed();
}

void CycleModel::setSuggestions (const std::vector<int>& indices, const juce::StringArray& typeNames, const std::vector<float>& confidences)
{
    bool any = false;
    for (size_t k = 0; k < indices.size(); ++k)
    {
        const auto i = indices[k];
        if (i < 0 || i >= size())
            continue;
        cycles[(size_t) i].autoType = typeNames[(int) k];
        cycles[(size_t) i].autoConf = confidences[k];
        any = true;
    }
    if (any)
        changed();
}

void CycleModel::setSuggestion (int index, const juce::String& typeName, float confidence, int typeSlot, bool apply)
{
    if (index < 0 || index >= size())
        return;

    auto& c = cycles[(size_t) index];
    c.autoType = typeName;
    c.autoConf = confidence;
    if (apply)
    {
        c.type = typeSlot;
        c.confirmed = false;
    }
    changed();
}

void CycleModel::markAllConfirmed()
{
    bool any = false;
    for (auto& c : cycles)
    {
        any = any || ! c.confirmed;
        c.confirmed = true;
    }
    if (any)
        changed();
}

int CycleModel::update (int index, juce::int64 start, juce::int64 end, int type)
{
    if (index < 0 || index >= size())
        return -1;
    if (end < start)
        std::swap (start, end);

    pushUndo();
    auto& c = cycles[(size_t) index];
    c.start = start;
    c.end = end;
    c.type = type;
    c.confirmed = true;
    sortCycles();

    int found = 0;
    for (int i = 0; i < size(); ++i)
        if (cycles[(size_t) i].start == start && cycles[(size_t) i].end == end && cycles[(size_t) i].type == type)
            found = i;
    changed();
    return found;
}

void CycleModel::setRangeLive (int index, juce::int64 start, juce::int64 end)
{
    if (index < 0 || index >= size())
        return;
    cycles[(size_t) index].start = start;
    cycles[(size_t) index].end = end;
    changed();
}

int CycleModel::finishEdit (int index)
{
    if (index < 0 || index >= size())
        return -1;

    const auto c = cycles[(size_t) index];
    sortCycles();
    int found = index;
    for (int i = 0; i < size(); ++i)
        if (cycles[(size_t) i].start == c.start && cycles[(size_t) i].end == c.end && cycles[(size_t) i].type == c.type)
            found = i;
    changed();
    return found;
}

void CycleModel::remove (const juce::Array<int>& indices)
{
    if (indices.isEmpty())
        return;

    pushUndo();
    auto sorted = indices;
    sorted.sort();
    for (int k = sorted.size(); --k >= 0;)
        if (sorted[k] >= 0 && sorted[k] < size())
            cycles.erase (cycles.begin() + sorted[k]);
    changed();
}

void CycleModel::replaceAll (std::vector<Cycle> newCycles, bool undoable)
{
    if (undoable)
        pushUndo();
    else
    {
        undoStack.clear();
        redoStack.clear();
    }

    cycles = std::move (newCycles);
    sortCycles();
    changed();
}

void CycleModel::undo()
{
    if (undoStack.empty())
        return;
    redoStack.push_back (cycles);
    cycles = std::move (undoStack.back());
    undoStack.pop_back();
    changed();
}

void CycleModel::redo()
{
    if (redoStack.empty())
        return;
    undoStack.push_back (cycles);
    cycles = std::move (redoStack.back());
    redoStack.pop_back();
    changed();
}

//==============================================================================
int CycleModel::findAt (juce::int64 sample) const
{
    for (int i = 0; i < size(); ++i)
        if (sample >= cycles[(size_t) i].start && sample < cycles[(size_t) i].end)
            return i;
    return -1;
}

bool CycleModel::overlapsAny (juce::int64 start, juce::int64 end) const
{
    for (const auto& c : cycles)
        if (start < c.end && end > c.start)
            return true;
    return false;
}

int CycleModel::numberOf (int index) const
{
    const auto type = cycles[(size_t) index].type;
    int n = 1;
    for (int i = 0; i < index; ++i)
        if (cycles[(size_t) i].type == type)
            ++n;
    return n + (type >= 0 && type < numSlots ? numberOffsets[(size_t) type] : 0);
}

int CycleModel::countOfType (int type) const
{
    int n = 0;
    for (const auto& c : cycles)
        n += c.type == type ? 1 : 0;
    return n;
}

juce::String CycleModel::typeNameOf (int type) const
{
    return type >= 0 && type < numSlots && types[(size_t) type].name.isNotEmpty() ? types[(size_t) type].name : "untyped";
}

juce::String CycleModel::nameFor (int type, int number) const
{
    auto out = pattern.replace ("{prefix}", prefix).replace ("{type}", typeNameOf (type));

    int pos = 0;
    for (;;)
    {
        const auto i = out.indexOf (pos, "{num");
        if (i < 0)
            break;
        const auto j = out.indexOfChar (i, '}');
        if (j < 0)
            break;

        const auto spec = out.substring (i + 4, j);
        const auto width = spec.startsWith (":") ? spec.substring (1).getIntValue() : 0;
        const auto digits = juce::String (number).paddedLeft ('0', width);
        out = out.replaceSection (i, j - i + 1, digits);
        pos = i + digits.length();
    }

    return sanitizeFileName (out);
}

juce::String CycleModel::nameOf (int index) const
{
    return nameFor (cycles[(size_t) index].type, numberOf (index));
}

//==============================================================================
juce::var CycleModel::cyclesToVar() const
{
    juce::Array<juce::var> arr;
    for (const auto& c : cycles)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("start", (juce::int64) c.start);
        o->setProperty ("end", (juce::int64) c.end);
        o->setProperty ("type", c.type);
        o->setProperty ("autoType", c.autoType);
        o->setProperty ("autoConf", (double) c.autoConf);
        o->setProperty ("confirmed", c.confirmed);
        o->setProperty ("rating", c.rating);
        o->setProperty ("ng", c.ng);
        o->setProperty ("attrs", c.attributes);
        arr.add (juce::var (o));
    }
    return arr;
}

void CycleModel::cyclesFromVar (const juce::var& v)
{
    std::vector<Cycle> loaded;
    if (const auto* arr = v.getArray())
        for (const auto& item : *arr)
        {
            Cycle c;
            c.start = (juce::int64) item.getProperty ("start", 0);
            c.end = (juce::int64) item.getProperty ("end", 0);
            c.type = (int) item.getProperty ("type", -1);
            c.autoType = item.getProperty ("autoType", "").toString();
            c.autoConf = (float) (double) item.getProperty ("autoConf", -1.0);
            c.confirmed = (bool) item.getProperty ("confirmed", true);
            c.rating = (int) item.getProperty ("rating", 0);
            c.ng = (bool) item.getProperty ("ng", false);
            c.attributes = item.getProperty ("attrs", "").toString();
            loaded.push_back (c);
        }
    replaceAll (std::move (loaded), false);
}

juce::var CycleModel::settingsToVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("prefix", prefix);
    o->setProperty ("pattern", pattern);
    o->setProperty ("category", category);

    juce::Array<juce::var> names;
    for (const auto& t : types)
        names.add (t.name);
    o->setProperty ("typeNames", names);
    return juce::var (o);
}

void CycleModel::settingsFromVar (const juce::var& v)
{
    if (! v.isObject())
        return;

    prefix = v.getProperty ("prefix", prefix).toString();
    pattern = v.getProperty ("pattern", pattern).toString();
    category = v.getProperty ("category", category).toString();
    if (const auto* names = v.getProperty ("typeNames", {}).getArray())
        for (int i = 0; i < juce::jmin (numSlots, names->size()); ++i)
            types[(size_t) i].name = (*names)[i].toString();
    changed();
}
} // namespace wt
