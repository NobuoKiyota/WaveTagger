#pragma once

#include "Restoration.h"
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

enum class PanMode
{
    off,
    automatic // measures the left/right level difference and only acts if it is a steady, moderate one
};

/** Everything defaults to "off": an untouched ConditionOptions leaves the audio bit-exact. */
struct ConditionOptions
{
    bool dcRemove = false;

    // repairs (Restoration.h): all off by default; the strength is how eager they are, never a promise that something is done
    bool declip = false;
    Strength declick = Strength::off;
    Strength dewind = Strength::off;
    Strength denoise = Strength::off;

    HumMode humMode = HumMode::off;
    int humHarmonics = 8;           // harmonics 1..N of the mains frequency are examined
    double humProminenceDb = 10.0;  // a harmonic counts if it stands this far above its neighbourhood
    double humQ = 30.0;             // notch width: -3 dB bandwidth is frequency / Q

    PanMode panMode = PanMode::off;
    double panThresholdDb = 0.5; // a smaller difference is left alone
    double panMaxDb = 6.0;       // a larger one is probably intended (hard pan) or a dead channel: left alone
    double panSpreadDb = 3.0;    // if the difference varies more than this (P25..P75) the stereo image moves: left alone

    float lowCutHz = 0.0f; // 0 = off
    int lowCutOrder = 2;   // 2 or 4 (Butterworth, single pass: no latency, length unchanged)

    NormalizeMode normalize = NormalizeMode::off;
    double targetDb = -1.0;   // dBFS for peak, LUFS for lufs
    double ceilingDb = -1.0;  // the sample peak never ends up above this (no limiter: the gain is reduced instead)
    double maxGainDb = 30.0;  // keeps near-silent cycles from being blown up into noise

    bool dither = true; // TPDF, only when writing 16-bit and something above was applied

    bool operator== (const ConditionOptions&) const = default;

    /** Stage A: needs the whole recording (filters need run-up, so they run before cutting). */
    bool needsFile() const
    {
        return dcRemove || declip || declick != Strength::off || dewind != Strength::off || denoise != Strength::off
               || humMode != HumMode::off || lowCutHz > 0.0f || panMode != PanMode::off;
    }
    bool active() const { return needsFile() || normalize != NormalizeMode::off; }
};

struct ConditionReport
{
    double dcOffset = 0.0; // largest per-channel mean that was removed
    int declipRuns = 0;       // flat tops rebuilt
    double declipGainDb = 0.0;
    int clicksFixed = 0;      // spikes repaired
    bool clicksTooMany = false;
    int windFrames = 0;       // analysis frames in which wind was attenuated
    double windMaxCutoffHz = 0.0;
    DenoiseOutcome denoiseOutcome = DenoiseOutcome::tooShort; // only meaningful when denoise is on
    double noiseFloorDb = -240.0;
    double denoiseAvgDb = 0.0;
    double humHz = 0.0;    // 50 or 60 when hum was found and notched, otherwise 0
    int humNotches = 0;
    bool panApplied = false;
    double panDb = 0.0;    // left minus right level, as measured (each channel was moved half of it towards the other)
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

enum class PanOutcome
{
    applied,     // a steady, moderate difference: it is corrected
    balanced,    // smaller than panThresholdDb
    tooLarge,    // larger than panMaxDb
    imageMoves,  // the difference changes during the recording
    tooShort,    // not enough sound to tell (or silence)
    notStereo
};

struct PanDetection
{
    PanOutcome outcome = PanOutcome::tooShort;
    double differenceDb = 0.0; // left minus right, over the frames that have sound in them
    double spreadDb = 0.0;     // P75 - P25 of the per-frame difference
    int activeFrames = 0;
};

/** Left/right level difference of the sound in the recording: frames of 50 ms above 100 Hz (so hum, rumble and wind do not count),
    only those well above the noise floor (so a lopsided floor does not either). Never modifies the buffer. */
PanDetection detectPanBalance (const juce::AudioBuffer<float>& buffer, double sampleRate, const ConditionOptions& options);

/** Stage A, in place: DC removal, de-clip, de-click, hum notches, low cut, de-wind, de-noise, then left/right balance. The length never changes. */
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

/** Short fade in and out of `fadeSamples` (the exporter's edge fade; the preview gain must see the same signal). */
void applyEdgeFade (juce::AudioBuffer<float>& buffer, int fadeSamples);

//==============================================================================
enum class ConditionPreset
{
    off,
    standard, // DC, automatic hum, 20 Hz low cut, automatic L/R balance, Peak -1 dBFS
    custom
};

/** The options a preset stands for (custom has none of its own: it is whatever the options say). */
ConditionOptions presetOptions (ConditionPreset preset);

/** What is kept in the shared preferences. */
struct ConditionSettings
{
    ConditionPreset preset = ConditionPreset::off;
    ConditionOptions options;
    bool previewGain = false; // A/B listening includes the normalisation gain

    bool operator== (const ConditionSettings&) const = default;
};

juce::var conditionSettingsToVar (const ConditionSettings& settings);

/** Missing, unknown or out-of-range values fall back to "off" / are clamped; never throws. */
ConditionSettings conditionSettingsFromVar (const juce::var& v);

/** Longest range whose normalisation gain is worked out for listening (longer ones play at unity). */
constexpr double maxPreviewSeconds = 60.0;

struct PreviewGain
{
    double gain = 1.0;
    bool available = true; // false: normalisation is on but the range is too long to measure
    ConditionReport report;
};

/** The gain the exporter would apply to [start, end) of `source` (which already has stage A applied):
    same edge fade, same normalisation. */
PreviewGain previewGainFor (const juce::AudioBuffer<float>& source, double sampleRate, juce::int64 start, juce::int64 end,
                            const ConditionOptions& options, float fadeMs = 2.0f);
} // namespace wt
