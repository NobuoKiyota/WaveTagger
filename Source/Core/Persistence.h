#pragma once

#include <juce_core/juce_core.h>

namespace wt
{
/** App data lives in %APPDATA%\WaveTagger (never next to the audio, which may sit in a synced Drive folder). */
inline juce::File getAppDataDir()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("WaveTagger");
    dir.createDirectory();
    return dir;
}

inline juce::File getSettingsFile() { return getAppDataDir().getChildFile ("settings.json"); }

inline juce::File getProjectFileFor (const juce::File& audio)
{
    auto dir = getAppDataDir().getChildFile ("projects");
    dir.createDirectory();
    const auto hash = juce::String::toHexString (audio.getFullPathName().toLowerCase().hashCode64());
    return dir.getChildFile (hash + "_" + audio.getFileNameWithoutExtension().substring (0, 40) + ".wtproj");
}

inline juce::var readJson (const juce::File& file)
{
    if (! file.existsAsFile())
        return {};
    return juce::JSON::parse (file);
}

inline bool writeJson (const juce::File& file, const juce::var& v)
{
    return file.replaceWithText (juce::JSON::toString (v, false), false, false, "\n");
}
} // namespace wt
