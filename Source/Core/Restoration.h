#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace wt
{
/** How hard a repair works. `off` is never passed to the functions below: the caller does not call them. */
enum class Strength
{
    off,
    low,
    medium,
    high
};

//==============================================================================
/** De-clip: flat tops (a run of equal extreme samples) are replaced by a cubic curve through the neighbours. */
struct DeclipResult
{
    int runs = 0;         // flat tops that were rebuilt (0 = nothing was done)
    double gainDb = 0.0;  // the whole recording was lowered by this much so the rebuilt peaks fit under 0 dBFS (<= 0)
};

/** A channel needs at least 3 flat tops of 3..64 samples at its own maximum to count as clipped: otherwise it is left alone.
    Never changes the length. */
DeclipResult declip (juce::AudioBuffer<float>& buffer);

//==============================================================================
/** De-click: short isolated spikes (and, stronger, crackle) are replaced by an AR (linear prediction) interpolation. */
struct DeclickResult
{
    int regions = 0;        // spikes that were repaired
    bool tooMany = false;   // so much of the recording looked like clicks that it is probably the sound itself: nothing was done
};

DeclickResult declick (juce::AudioBuffer<float>& buffer, double sampleRate, Strength strength);

//==============================================================================
/** De-wind (simple): low-frequency rumble that is far above the recording's usual low end and lasts at least 0.3 s is
    attenuated by a high-pass whose corner follows the strength of the wind. Short bass hits are left alone. */
struct DewindResult
{
    int frames = 0;          // analysis frames (about 10 ms each) in which wind was attenuated; 0 = untouched
    double maxCutoffHz = 0.0;
};

DewindResult dewind (juce::AudioBuffer<float>& buffer, double sampleRate, Strength strength);

//==============================================================================
/** De-noise: broadband steady noise (hiss, room, fans) is reduced by spectral subtraction. The noise is measured in the
    quietest tenth of the recording; if the recording has no clearly quieter part (rain, machinery: the noise IS the sound)
    nothing is done. */
enum class DenoiseOutcome
{
    applied,
    noFloor,   // no part is clearly quieter than the rest, or the floor is digital silence
    tooShort   // under one second
};

struct DenoiseResult
{
    DenoiseOutcome outcome = DenoiseOutcome::tooShort;
    double floorDb = -240.0;           // level of the noise floor (RMS dBFS per channel)
    double averageReductionDb = 0.0;   // mean gain over time and frequency (<= 0)
};

/** Stereo channels share one gain per bin (the image does not move). The length never changes. */
DenoiseResult denoise (juce::AudioBuffer<float>& buffer, double sampleRate, Strength strength);
} // namespace wt
