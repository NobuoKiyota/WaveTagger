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

enum class HumMode
{
    off,
    automatic, // looks for 50 Hz and 60 Hz mains hum and only acts if one of them is clearly there
    mains50,   // forced system: only that one is looked for, but a single clear harmonic is enough
    mains60
};

/** Everything defaults to "off": an untouched ConditionOptions leaves the audio bit-exact. */
struct ConditionOptions
{
    bool dcRemove = false;

    HumMode humMode = HumMode::off;
    int humHarmonics = 8;           // harmonics 1..N of the mains frequency are examined
    double humProminenceDb = 10.0;  // a harmonic counts if it stands this far above its neighbourhood
    double humQ = 30.0;             // notch width: -3 dB bandwidth is frequency / Q

    float lowCutHz = 0.0f; // 0 = off
    int lowCutOrder = 2;   // 2 or 4 (Butterworth, single pass: no latency, length unchanged)

    NormalizeMode normalize = NormalizeMode::off;
    double targetDb = -1.0;   // dBFS for peak, LUFS for lufs
    double ceilingDb = -1.0;  // the sample peak never ends up above this (no limiter: the gain is reduced instead)
    double maxGainDb = 30.0;  // keeps near-silent cycles from being blown up into noise

    bool dither = true; // TPDF, only when writing 16-bit and something above was applied

    /** Stage A: needs the whole recording (filters need run-up, so they run before cutting). */
    bool needsFile() const { return dcRemove || humMode != HumMode::off || lowCutHz > 0.0f; }
    bool active() const { return needsFile() || normalize != NormalizeMode::off; }
};

struct ConditionReport
{
    double dcOffset = 0.0; // largest per-channel mean that was removed
    double humHz = 0.0;    // 50 or 60 when hum was found and notched, otherwise 0
    int humNotches = 0;
    double peakBeforeDb = -240.0, peakAfterDb = -240.0;
    double lufsBefore = -240.0; // only measured for NormalizeMode::lufs
    double gainDb = 0.0;
    bool gainLimited = false; // the ceiling or maxGainDb stopped the gain short of the target
};

struct HumDetection
{
    double mainsHz = 0.0;                // 50 or 60; 0 = no hum found
    std::vector<double> notchHz;         // measured frequency of every harmonic that stood out (follows mains drift)
    std::vector<double> prominenceDb;    // how far each one stood out
};

/** Looks at the quietest fifth of the recording (the noise floor between the sounds) with a long FFT and checks the
    harmonics of 50 / 60 Hz. Recordings shorter than ~0.35 s are not analysed. Never modifies the buffer. */
HumDetection detectHum (const juce::AudioBuffer<float>& buffer, double sampleRate, const ConditionOptions& options);

/** Stage A, in place: DC removal, then hum notches, then low cut. The length never changes. */
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
