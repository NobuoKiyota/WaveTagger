#include "ConditioningPreview.h"

#include <juce_events/juce_events.h>

namespace wt
{
namespace
{
constexpr int settleMs = 300;
}

ConditioningPreview::ConditioningPreview() = default;

ConditioningPreview::~ConditioningPreview()
{
    shared->alive = false; // results that are still on their way are dropped; the pool then waits for the running job
}

void ConditioningPreview::setAudio (AudioDataPtr newAudio)
{
    original = std::move (newAudio);
    processed = nullptr;
    processedWanted = false;
    fileReport = {};
    hum = {};
    pan = {};
    building = false;
    ++shared->generation;
    startSoon();
}

void ConditioningPreview::setOptions (const ConditionOptions& newOptions)
{
    if (newOptions == options)
        return;

    options = newOptions;
    processed = nullptr;
    fileReport = {};
    hum = {};
    pan = {};
    ++shared->generation;
    startSoon();
}

void ConditioningPreview::requestProcessed()
{
    if (processedWanted && (processed != nullptr || building))
        return;

    processedWanted = true;
    ++shared->generation;
    start();
}

void ConditioningPreview::releaseProcessed()
{
    processedWanted = false;
    processed = nullptr;
    if (building)
    {
        building = false;
        ++shared->generation; // the copy that is still being made is not wanted any more
        start();              // (the hum search that went with it is)
    }
}

void ConditioningPreview::startSoon()
{
    const auto token = shared;
    const auto generation = token->generation.load();
    juce::Timer::callAfterDelay (settleMs, [this, token, generation] {
        if (token->alive && token->generation == generation)
            start();
    });
}

void ConditioningPreview::start()
{
    if (original == nullptr)
        return;

    const auto token = shared;
    const auto generation = token->generation.load();
    const auto audio = original;
    const auto opts = options;
    const auto wantProcessed = processedWanted;

    if (! opts.needsFile())
    {
        // nothing in stage A: the recording is its own conditioned version
        processed = wantProcessed ? original : nullptr;
        fileReport = {};
    }

    const auto buildCopy = wantProcessed && opts.needsFile();
    building = buildCopy;
    hum.ready = opts.humMode == HumMode::off;
    pan.ready = opts.panMode == PanMode::off;

    if (! buildCopy && opts.humMode == HumMode::off && opts.panMode == PanMode::off)
    {
        if (onChanged)
            onChanged();
        return;
    }

    pool.addJob ([this, token, generation, audio, opts, buildCopy] {
        if (! token->alive || token->generation != generation)
            return;

        HumDetection found;
        if (opts.humMode != HumMode::off)
            found = detectHum (audio->buffer, audio->sampleRate, opts);
        PanDetection balance;
        if (opts.panMode != PanMode::off)
            balance = detectPanBalance (audio->buffer, audio->sampleRate, opts);

        AudioDataPtr result;
        ConditionReport report;
        if (buildCopy && token->generation == generation)
        {
            auto copy = std::make_shared<AudioData>();
            copy->file = audio->file;
            copy->sampleRate = audio->sampleRate;
            copy->bitsPerSample = audio->bitsPerSample;
            copy->isFloat = audio->isFloat;
            copy->formatName = audio->formatName;
            copy->buffer.makeCopyOf (audio->buffer);
            conditionFile (copy->buffer, copy->sampleRate, opts, report);
            result = std::move (copy);
        }

        juce::MessageManager::callAsync ([this, token, generation, buildCopy, found = std::move (found), balance, result = std::move (result), report]() mutable {
            if (token->alive && token->generation == generation)
                finish (generation, buildCopy, std::move (found), balance, std::move (result), report);
        });
    });
}

void ConditioningPreview::finish (int, bool wantedProcessed, HumDetection foundHum, PanDetection foundPan, AudioDataPtr result, ConditionReport report)
{
    hum.ready = true;
    hum.result = std::move (foundHum);
    pan.ready = true;
    pan.result = foundPan;
    if (wantedProcessed)
    {
        processed = std::move (result);
        fileReport = report;
        building = false;
    }

    if (onChanged)
        onChanged();
}
} // namespace wt
