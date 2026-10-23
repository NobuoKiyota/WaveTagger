#pragma once

#include "Persistence.h"

namespace wt
{
struct Category
{
    juce::String name; // used as folder name
    juce::String jp;   // display alias
};

/**
    UCS-style top-level categories (simplified; the official list also has sub-categories).
    Override by putting lines of "NAME<TAB>日本語" into %APPDATA%\WaveTagger\categories.txt.
*/
inline std::vector<Category> getCategories()
{
    std::vector<Category> out;

    const auto custom = getAppDataDir().getChildFile ("categories.txt");
    if (custom.existsAsFile())
    {
        for (const auto& line : juce::StringArray::fromLines (custom.loadFileAsString()))
            if (line.trim().isNotEmpty())
                out.push_back ({ line.upToFirstOccurrenceOf ("\t", false, false).trim(),
                                 line.fromFirstOccurrenceOf ("\t", false, false).trim() });
        if (! out.empty())
            return out;
    }

    static const char* const table[][2] = {
        { "AIRCRAFT", "航空機" },   { "ALARMS", "警報" },        { "AMBIENCE", "環境音" },    { "ANIMALS", "動物" },
        { "BEEPS", "ビープ" },      { "BELLS", "ベル" },         { "BIRDS", "鳥" },           { "BOATS", "船" },
        { "BULLETS", "弾" },        { "CARTOON", "カートゥーン" }, { "CERAMICS", "陶器" },     { "CHAINS", "鎖" },
        { "CHEMICALS", "化学" },    { "CLOTH", "布" },           { "COMMUNICATIONS", "通信" }, { "COMPUTERS", "コンピュータ" },
        { "CREATURES", "クリーチャー" }, { "CROWDS", "群衆" },   { "DESIGNED", "加工音" },     { "DESTRUCTION", "破壊" },
        { "DIRT", "土" },           { "DISASTER", "災害" },      { "DOORS", "ドア" },         { "DRAWERS", "引き出し" },
        { "ELECTRICITY", "電気" },  { "EVOLVING", "変化音" },    { "EXPLOSIONS", "爆発" },    { "FEET", "足音" },
        { "FIGHT", "格闘" },        { "FIRE", "火" },            { "FIREWORKS", "花火" },     { "FOLEY", "フォーリー" },
        { "FOOD", "食べ物" },       { "GAMES", "ゲーム" },       { "GLASS", "ガラス" },       { "GUNS", "銃" },
        { "HORNS", "警笛" },        { "HUMAN", "人体" },         { "ICE", "氷" },             { "LASERS", "レーザー" },
        { "LEATHER", "革" },        { "LIQUID", "液体" },        { "LOCKS", "錠" },           { "MACHINES", "機械" },
        { "MAGIC", "魔法" },        { "METAL", "金属" },         { "MOTORS", "モーター" },    { "MOVEMENT", "動き" },
        { "MUSIC", "音楽" },        { "NOISE", "ノイズ" },       { "PAPER", "紙" },           { "PLASTIC", "プラスチック" },
        { "PORTALS", "ポータル" },  { "RAIN", "雨" },            { "ROBOTS", "ロボット" },    { "ROCKS", "岩" },
        { "ROPE", "ロープ" },       { "RUBBER", "ゴム" },        { "SAND", "砂" },            { "SCIFI", "SF" },
        { "SPORTS", "スポーツ" },   { "SWOOSHES", "スウィッシュ" }, { "TOOLS", "工具" },      { "TOYS", "おもちゃ" },
        { "TRAINS", "電車" },       { "UI", "UI" },              { "VEHICLES", "乗り物" },    { "VOICES", "声" },
        { "WATER", "水" },          { "WEAPONS", "武器" },       { "WEATHER", "天候" },       { "WIND", "風" },
        { "WOOD", "木" },
    };

    for (const auto& row : table)
        out.push_back ({ row[0], juce::String::fromUTF8 (row[1]) });
    return out;
}
} // namespace wt
