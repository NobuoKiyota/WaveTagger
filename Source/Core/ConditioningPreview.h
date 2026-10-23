#pragma once

#include "AudioData.h"
#include "Conditioning.h"

namespace wt
{
/** Builds, in the background, what the conditioning chain would make of the open recording: the hum that is in it, and
    (only when asked for) a copy with stage A applied to listen to. Everything here is used from the message thread. */
class ConditioningPreview
{
public:
    ConditioningPreview();
    ~ConditioningPreview();

    void setAudio (AudioDataPtr original);

    /** Settings changed: whatever was built is stale. Work starts again after a short pause (sliders send many changes). */
    void setOptions (const ConditionOptions& options);

    /** Keep a conditioned copy up to date (needed while listening to it). When stage A is not needed at all the original is
        its own conditioned version and nothing is copied. */
    void requestProcessed();

    /** Drops the copy (it is as large as the recording). */
    void releaseProcessed();

    /** nullptr until the copy for the *current* audio and options is ready. */
    AudioDataPtr getProcessed() const { return processed; }
    bool isBuilding() const { return building; }
    const ConditionReport& getFileReport() const { return fileReport; }

    /** The hum found in the recording (needs humMode != off). humReady() is false while it is being looked for. */
    bool humReady() const { return hum.ready; }
    const HumDetection& getHum() const { return hum.result; }

    /** The left/right balance of the recording (needs panMode != off). */
    bool panReady() const { return pan.ready; }
    const PanDetection& getPan() const { return pan.result; }

    std::function<void()> onChanged; // after anything above changed

private:
    struct Shared
    {
        std::atomic<bool> alive { true };
        std::atomic<int> generation { 0 };
    };

    void startSoon();
    void start();
    void finish (int generation, bool wantedProcessed, HumDetection foundHum, PanDetection foundPan, AudioDataPtr result, ConditionReport report);

    std::shared_ptr<Shared> shared = std::make_shared<Shared>();
    AudioDataPtr original, processed;
    ConditionOptions options;
    ConditionReport fileReport;
    bool processedWanted = false, building = false;
    struct
    {
        bool ready = false;
        HumDetection result;
    } hum;
    struct
    {
        bool ready = false;
        PanDetection result;
    } pan;

    juce::ThreadPool pool { juce::ThreadPoolOptions {}.withNumberOfThreads (1) };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ConditioningPreview)
};
} // namespace wt
