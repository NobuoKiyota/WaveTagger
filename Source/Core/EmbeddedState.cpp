#include "EmbeddedState.h"

namespace wt
{
EmbedRequest buildEmbedRequest (const CycleModel& cycles, double sampleRate, const juce::String& appVersion)
{
    EmbedRequest request;

    juce::Array<juce::var> cycleArray;
    for (int i = 0; i < cycles.size(); ++i)
    {
        const auto& c = cycles[i];
        const auto name = cycles.nameOf (i);

        request.regions.push_back ({ c.start, c.end - c.start, name });

        auto* o = new juce::DynamicObject();
        o->setProperty ("start", (juce::int64) c.start);
        o->setProperty ("end", (juce::int64) c.end);
        o->setProperty ("type", c.type);
        o->setProperty ("typeName", c.type >= 0 ? cycles.getType (c.type).name : juce::String());
        o->setProperty ("name", name);
        o->setProperty ("number", cycles.numberOf (i));
        o->setProperty ("autoType", c.autoType);
        o->setProperty ("autoConf", (double) c.autoConf);
        o->setProperty ("confirmed", c.confirmed);
        o->setProperty ("rating", c.rating);
        o->setProperty ("ng", c.ng);
        o->setProperty ("attrs", c.attributes);
        cycleArray.add (juce::var (o));
    }

    juce::Array<juce::var> typeNames;
    for (int i = 0; i < CycleModel::numSlots; ++i)
        typeNames.add (cycles.getType (i).name);

    auto* root = new juce::DynamicObject();
    root->setProperty ("schema", currentWtagSchema);
    root->setProperty ("app", "WaveTagger");
    root->setProperty ("appVersion", appVersion);
    root->setProperty ("savedAt", juce::Time::currentTimeMillis());
    root->setProperty ("sampleRate", sampleRate);
    root->setProperty ("prefix", cycles.getPrefix());
    root->setProperty ("pattern", cycles.getPattern());
    root->setProperty ("category", cycles.getCategory());
    root->setProperty ("types", typeNames);
    root->setProperty ("cycles", cycleArray);

    request.wtagJson = juce::JSON::toString (juce::var (root), true);
    return request;
}

bool parseWtag (const juce::var& json, WtagData& out)
{
    if (! json.isObject())
        return false;

    out = {};
    out.schema = (int) json.getProperty ("schema", 0);
    out.savedAt = (juce::int64) json.getProperty ("savedAt", 0);
    out.sampleRate = (double) json.getProperty ("sampleRate", 0.0);
    out.category = json.getProperty ("category", "").toString();
    out.prefix = json.getProperty ("prefix", "").toString();

    const auto* arr = json.getProperty ("cycles", {}).getArray();
    if (out.schema < 1 || arr == nullptr)
        return false;

    for (const auto& item : *arr)
    {
        WtagCycle c;
        c.start = (juce::int64) item.getProperty ("start", 0);
        c.end = (juce::int64) item.getProperty ("end", 0);
        c.type = (int) item.getProperty ("type", -1);
        c.typeName = item.getProperty ("typeName", "").toString();
        c.name = item.getProperty ("name", "").toString();
        c.autoType = item.getProperty ("autoType", "").toString();
        c.autoConf = (float) (double) item.getProperty ("autoConf", -1.0);
        c.confirmed = (bool) item.getProperty ("confirmed", true);
        c.rating = (int) item.getProperty ("rating", 0);
        c.ng = (bool) item.getProperty ("ng", false);
        c.attributes = item.getProperty ("attrs", "").toString();
        if (c.end > c.start)
            out.cycles.push_back (c);
    }
    return true;
}
} // namespace wt
