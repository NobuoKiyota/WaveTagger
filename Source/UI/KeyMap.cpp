#include "KeyMap.h"
#include "Util/Text.h"

namespace wt
{
namespace
{
constexpr int numActions = (int) Action::Count;

struct Info
{
    const char* id;
    const char* jp;
};

const Info infos[numActions] = {
    { "none", "" },
    { "open", "ファイルを開く" },          { "library", "ライブラリ検索" },        { "export", "書き出し" },
    { "selectAll", "すべて選択" },         { "undo", "元に戻す" },                { "redo", "やり直し" },
    { "playStop", "再生 / 停止" },         { "confirm", "確定 (再選択)" },         { "cancel", "取消 / 選択解除" },
    { "delete", "サイクルを削除" },
    { "toStart", "先頭へ" },               { "toEnd", "末尾へ" },                 { "zoomFit", "全体を表示" },
    { "zoomSelection", "選択範囲にズーム" }, { "toggleLoop", "ループ切替" },        { "zoomIn", "拡大 (時間)" },
    { "zoomOut", "縮小 (時間)" },          { "scrollLeft", "左へスクロール" },     { "scrollRight", "右へスクロール" },
    { "autoSplit", "自動分割" },           { "importMarkers", "マーカー読み込み" },
    { "saveWav", "WAV にマーカーを書き込む" },
    { "acceptSuggestion", "候補の Type を採用" },
    { "type1", "Type 1" }, { "type2", "Type 2" }, { "type3", "Type 3" }, { "type4", "Type 4" }, { "type5", "Type 5" },
    { "type6", "Type 6" }, { "type7", "Type 7" }, { "type8", "Type 8" }, { "type9", "Type 9" },
};

juce::KeyPress plain (int code) { return juce::KeyPress (code); }
juce::KeyPress ch (juce::juce_wchar c) { return juce::KeyPress ((int) c, juce::ModifierKeys(), c); }
juce::KeyPress ctrl (int code) { return juce::KeyPress (code, juce::ModifierKeys::ctrlModifier, 0); }

bool hasModifiers (const juce::KeyPress& k)
{
    return k.getModifiers().isCommandDown() || k.getModifiers().isAltDown();
}
} // namespace

void KeyMap::reset()
{
    for (auto& b : bindings)
        b.clear();

    const auto set = [this] (Action a, std::initializer_list<juce::KeyPress> keys) {
        for (const auto& k : keys)
            bindings[(size_t) a].add (k);
    };

    set (Action::Open, { ctrl ('O') });
    set (Action::Library, { ctrl ('L') });
    set (Action::Export, { ctrl ('E') });
    set (Action::SaveWav, { ctrl ('S') });
    set (Action::AcceptSuggestion, { plain ('A') });
    set (Action::SelectAll, { ctrl ('A') });
    set (Action::Undo, { ctrl ('Z') });
    set (Action::Redo, { ctrl ('Y'), juce::KeyPress ('Z', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier, 0) });
    set (Action::PlayStop, { plain (juce::KeyPress::spaceKey) });
    set (Action::Confirm, { plain (juce::KeyPress::returnKey) });
    set (Action::Cancel, { plain (juce::KeyPress::escapeKey) });
    set (Action::Delete, { plain (juce::KeyPress::deleteKey), plain (juce::KeyPress::backspaceKey) });
    set (Action::ToStart, { plain (juce::KeyPress::homeKey) });
    set (Action::ToEnd, { plain (juce::KeyPress::endKey) });
    set (Action::ZoomFit, { plain ('F') });
    set (Action::ZoomSelection, { plain ('Z') });
    set (Action::ToggleLoop, { plain ('L') });
    set (Action::ZoomIn, { ch ('+'), ch ('='), plain (juce::KeyPress::numberPadAdd) });
    set (Action::ZoomOut, { ch ('-'), plain (juce::KeyPress::numberPadSubtract) });
    set (Action::ScrollLeft, { plain (juce::KeyPress::leftKey) });
    set (Action::ScrollRight, { plain (juce::KeyPress::rightKey) });

    for (int i = 0; i < 9; ++i)
        set ((Action) ((int) Action::Type1 + i), { ch ((juce::juce_wchar) ('1' + i)) });
}

Action KeyMap::find (const juce::KeyPress& key) const
{
    for (int a = 1; a < numActions; ++a)
        for (const auto& b : bindings[(size_t) a])
            if (key == b)
                return (Action) a;

    // printable keys that need Shift on some layouts ("+") are matched by the character they produce
    if (key.getTextCharacter() != 0 && ! hasModifiers (key))
        for (int a = 1; a < numActions; ++a)
            for (const auto& b : bindings[(size_t) a])
                if (b.getTextCharacter() != 0 && b.getTextCharacter() == key.getTextCharacter() && ! hasModifiers (b))
                    return (Action) a;

    return Action::None;
}

Action KeyMap::bindSingle (Action a, const juce::KeyPress& key)
{
    Action taken = Action::None;
    for (int o = 1; o < numActions; ++o)
    {
        auto& list = bindings[(size_t) o];
        for (int i = list.size(); --i >= 0;)
            if (list[i] == key)
            {
                list.remove (i);
                if ((Action) o != a)
                    taken = (Action) o;
            }
    }

    bindings[(size_t) a].clear();
    bindings[(size_t) a].add (key);
    return taken;
}

juce::String KeyMap::describe (Action a) const
{
    juce::StringArray parts;
    for (const auto& k : bindings[(size_t) a])
        parts.add (k.getTextDescription());
    return parts.isEmpty() ? WT_U8 ("(未割り当て)") : parts.joinIntoString ("  /  ");
}

juce::String KeyMap::label (Action a)
{
    return juce::String::fromUTF8 (infos[(int) a].jp);
}

juce::var KeyMap::toVar() const
{
    auto* o = new juce::DynamicObject();
    for (int a = 1; a < numActions; ++a)
    {
        juce::Array<juce::var> keys;
        for (const auto& k : bindings[(size_t) a])
            keys.add (k.getTextDescription());
        o->setProperty (infos[a].id, keys);
    }
    return juce::var (o);
}

void KeyMap::fromVar (const juce::var& v)
{
    if (! v.isObject())
        return;

    for (int a = 1; a < numActions; ++a)
    {
        const auto* arr = v.getProperty (infos[a].id, {}).getArray();
        if (arr == nullptr)
            continue; // not stored: keep the default

        bindings[(size_t) a].clear();
        for (const auto& item : *arr)
        {
            const auto k = juce::KeyPress::createFromDescription (item.toString());
            if (k.isValid())
                bindings[(size_t) a].add (k);
        }
    }
}
} // namespace wt
