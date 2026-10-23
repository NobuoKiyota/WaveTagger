#pragma once

#include "AutoSplit.h"
#include "Suggester.h"
#include "WavChunks.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <set>

namespace wt
{
struct InboxOptions
{
    juce::File inbox, raw;
    juce::StringArray categoryNames;       // exact Category names; a folder named like one (any case) becomes the Category hint
    AutoSplitParams split;
    juce::String appVersion { "inbox" };
    std::shared_ptr<const Suggester> suggester; // what has been learned so far (null = no suggestions)
    float autoThreshold = 0.7f;             // from this confidence on, the Type is applied (and marked 'not looked at yet')
};

struct InboxOutcome
{
    bool ok = false;
    juce::String error;
    juce::File original;   // where the recording was found
    juce::File rawFile;    // the copy in the Raw folder, now carrying the markers
    juce::File archived;   // the original after it was moved to _Done (empty if that failed)
    juce::String archiveNote;
    int cycles = 0;
    juce::String categoryHint, typeHint;
    int autoTyped = 0;        // cycles typed by the suggestion engine
    int uncertain = 0;        // cycles left without a Type: a person has to decide
    float minConfidence = -1.0f; // the least certain suggestion made (-1: none was made)
};

/**
    One recording from the inbox, start to finish:
      copy to Raw/<year>/ (verified identical; non-WAV formats are converted to 24-bit WAV)
      -> auto-split into cycles -> embed them (cue + wtag) in the Raw copy -> move the original to <inbox>/_Done/
    Folder names are labels: <inbox>/TOOLS/Open/take.wav = Category TOOLS, Type Open.
    The original is only moved after the Raw copy has been written AND verified; any failure leaves it where it was.
*/
InboxOutcome processInboxFile (const juce::File& file, const InboxOptions& options, juce::AudioFormatManager& formats);

/** Tracks files until their size/modification time stop changing (Drive may still be downloading them). */
struct InboxScanState
{
    struct Seen
    {
        juce::int64 size = 0, mtime = 0;
        int stable = 0;
    };

    std::map<juce::String, Seen> seen;
    std::set<juce::String> ignored; // "path|size|mtime" of files that must not be imported (existing backlog, failed attempts)

    static juce::String signature (const juce::File& f);
};

/** All audio files under the inbox that could be imported (skips _Done, _Failed, temp and hidden files). */
std::vector<juce::File> listInboxCandidates (const juce::File& inbox);

/** Files that have been unchanged for `requiredStable` consecutive scans and are ready to import. */
std::vector<juce::File> scanForReady (const juce::File& inbox, InboxScanState& state, int requiredStable = 3);

/** Polls the inbox every couple of seconds on its own thread and imports whatever is ready, one file at a time. */
class InboxWatcher : private juce::Thread
{
public:
    explicit InboxWatcher (juce::AudioFormatManager& formats);
    ~InboxWatcher() override;

    struct Config
    {
        juce::File inbox, raw;
        bool enabled = false;
        juce::StringArray categoryNames;
        std::shared_ptr<const Suggester> suggester;
        juce::File stateFile; // remembers which files must not be imported (the existing backlog, failed attempts) across restarts
    };

    void configure (const Config& config);
    /** Marks everything that is in the inbox right now as "do not import" (so only new recordings are picked up). */
    int ignoreExisting();
    void scanNow();
    void forgetFailure (const juce::File& f);

    /** Called on the message thread after each file (success or failure). */
    std::function<void (const InboxOutcome&)> onOutcome;

    juce::String getStatus() const;
    bool isRunning() const { return enabledFlag; }

private:
    void run() override;
    void saveIgnored() const;
    void loadIgnored();

    juce::AudioFormatManager& formats;
    mutable juce::CriticalSection lock;
    Config config;
    InboxScanState state;
    juce::String status { "stopped" };
    std::atomic<bool> enabledFlag { false };
    juce::WaitableEvent wake;
    int stableScans = 3;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true); // lets late callbacks notice the watcher is gone

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InboxWatcher)
};
} // namespace wt
