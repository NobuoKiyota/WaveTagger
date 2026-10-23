#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace wt
{
enum class NormalizeMode
{
    off,
    peak, // target is a sample peak in dBFS
    lufs  // target is integrated loudness in LUFS (BS.1770, gated)
};

/** Everything defaults to "off": an untouched ConditionOptions leaves the audio bit-exact. */
struct ConditionOptions
{
    bool dcRemove = false;
    float lowCutHz = 0.0f; // 0 = off
    int lowCutOrder = 2;   // 2 or 4 (Butterworth, single pass: no latency, length unchanged)

    NormalizeMode normalize = NormalizeMode::off;
    double targetDb = -1.0;   // dBFS for peak, LUFS for lufs
    double ceilingDb = -1.0;  // the sample peak never ends up above this (no limiter: the gain is reduced instead)
    double maxGainDb = 30.0;  // keeps near-silent cycles from being blown up into noise

    bool dither = true; // TPDF, only when writing 16-bit and something above was applied

    /** Stage A: needs the whole recording (filters need run-up, so they run before cutting). */
    bool needsFile() const { return dcRemove || lowCutHz > 0.0f; }
    bool active() const { return needsFile() || normalize != NormalizeMode::off; }
};

struct ConditionReport
{
    double dcOffset = 0.0; // largest per-channel mean that was removed
    double peakBeforeDb = -240.0, peakAfterDb = -240.0;
    double lufsBefore = -240.0; // only measured for NormalizeMode::lufs
    double gainDb = 0.0;
    bool gainLimited = false; // the ceiling or maxGainDb stopped the gain short of the target
};

/** Stage A, in place: DC removal, then low cut. The length never changes. */
void conditionFile (juce::AudioBuffer<float>& buffer, double sampleRate, const ConditionOptions& options, ConditionReport& report);

/** Stage B, in place, on one already-cut cycle: normalisation. */
void conditionCycle (juce::AudioBuffer<float>& buffer, double sampleRate, const ConditionOptions& options, ConditionReport& report);

/** Largest absolute sample over all channels. */
float samplePeak (const juce::AudioBuffer<float>& buffer);

/** Integrated loudness in LUFS (400 ms blocks, absolute -70 / relative -10 LU gates; shorter than one block is
    measured ungated). Every channel counts with weight 1. Returns -240 for silence. */
double measureLoudness (const juce::AudioBuffer<float>& buffer, double sampleRate);

/** Adds triangular noise of +-1 LSB of a `bits`-bit target. */
void addTpdfDither (juce::AudioBuffer<float>& buffer, int bits, juce::int64 seed);

/** e.g. "cond=dc,lc20Hz/2,norm=-1.0dBFS,gain=+3.2dB" for the WAV comment. */
juce::String describeConditioning (const ConditionOptions& options, const ConditionReport& report);
} // namespace wt
