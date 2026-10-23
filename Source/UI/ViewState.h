#pragma once

#include <juce_events/juce_events.h>

namespace wt
{
/** Shared horizontal view / cursor / selection state. Positions are in samples. */
class ViewState : public juce::ChangeBroadcaster
{
public:
    void reset (juce::int64 totalSamples, double rate)
    {
        numSamples = totalSamples;
        sampleRate = rate;
        cursor = 0;
        selStart = selEnd = 0;
        freqLo = 0.0;
        freqHi = 1.0;
        waveGain = 1.0;
        zoomToFit();
    }

    void setWidth (int px)
    {
        px = juce::jmax (1, px);
        if (px != widthPx)
        {
            widthPx = px;
            setView (viewStart, spp, true);
        }
    }

    double getViewStart() const { return viewStart; }
    double getViewEnd() const { return viewStart + spp * widthPx; }
    double getSamplesPerPixel() const { return spp; }
    juce::int64 getNumSamples() const { return numSamples; }
    double getSampleRate() const { return sampleRate; }
    int getWidth() const { return widthPx; }

    double getMinSpp() const { return 1.0 / 32.0; }
    double getMaxSpp() const { return juce::jmax (getMinSpp(), (double) numSamples / widthPx); }

    void setView (double start, double newSpp, bool forceNotify = false)
    {
        newSpp = juce::jlimit (getMinSpp(), getMaxSpp(), newSpp);
        const auto visible = newSpp * widthPx;
        start = juce::jlimit (0.0, juce::jmax (0.0, (double) numSamples - visible), start);

        if (forceNotify || start != viewStart || newSpp != spp)
        {
            viewStart = start;
            spp = newSpp;
            changed();
        }
    }

    void zoomAround (double px, double factor)
    {
        const auto anchor = viewStart + px * spp;
        const auto newSpp = juce::jlimit (getMinSpp(), getMaxSpp(), spp * factor);
        setView (anchor - px * newSpp, newSpp);
    }

    void scrollPixels (double px) { setView (viewStart + px * spp, spp); }
    void zoomToFit() { setView (0.0, getMaxSpp(), true); }
    void centreOn (double sample) { setView (sample - 0.5 * spp * widthPx, spp); }

    void zoomToRange (juce::int64 s0, juce::int64 s1)
    {
        if (s1 <= s0)
            return;
        const auto pad = (double) (s1 - s0) * 0.05;
        setView ((double) s0 - pad, ((double) (s1 - s0) + 2.0 * pad) / widthPx);
    }

    void ensureVisible (double sample)
    {
        if (sample < viewStart || sample > getViewEnd())
            setView (sample - 0.05 * spp * widthPx, spp);
    }

    //== vertical (frequency) range, normalised on the mel display scale: 0 = DC, 1 = Nyquist
    double getFreqLo() const { return freqLo; }
    double getFreqHi() const { return freqHi; }

    void zoomFreqAround (double anchorNorm, double factor)
    {
        const auto range = freqHi - freqLo;
        const auto rel = (anchorNorm - freqLo) / range;
        const auto newRange = juce::jlimit (0.02, 1.0, range * factor);
        setFreqRange (anchorNorm - rel * newRange, newRange);
    }

    void scrollFreq (double deltaNorm) { setFreqRange (freqLo + deltaNorm, freqHi - freqLo); }
    void resetFreq() { setFreqRange (0.0, 1.0); }

    //== waveform amplitude zoom (1 = full scale fits the lane)
    double getWaveGain() const { return waveGain; }
    void zoomGain (double factor)
    {
        const auto g = juce::jlimit (1.0, 512.0, waveGain * factor);
        if (g != waveGain)
        {
            waveGain = g;
            changed();
        }
    }

    juce::int64 getCursor() const { return cursor; }
    void setCursor (juce::int64 s)
    {
        s = juce::jlimit<juce::int64> (0, numSamples, s);
        if (s != cursor)
        {
            cursor = s;
            changed();
        }
    }

    bool hasSelection() const { return selEnd > selStart; }
    juce::int64 getSelectionStart() const { return selStart; }
    juce::int64 getSelectionEnd() const { return selEnd; }

    void setSelection (juce::int64 a, juce::int64 b)
    {
        const auto s = juce::jlimit<juce::int64> (0, numSamples, juce::jmin (a, b));
        const auto e = juce::jlimit<juce::int64> (0, numSamples, juce::jmax (a, b));
        if (s != selStart || e != selEnd)
        {
            selStart = s;
            selEnd = e;
            changed();
        }
    }

    void clearSelection() { setSelection (0, 0); }

private:
    void changed() { sendSynchronousChangeMessage(); }

    void setFreqRange (double lo, double range)
    {
        range = juce::jlimit (0.02, 1.0, range);
        lo = juce::jlimit (0.0, 1.0 - range, lo);
        if (lo != freqLo || lo + range != freqHi)
        {
            freqLo = lo;
            freqHi = lo + range;
            changed();
        }
    }

    double freqLo = 0.0, freqHi = 1.0, waveGain = 1.0;

    juce::int64 numSamples = 0;
    double sampleRate = 48000.0;
    int widthPx = 1;
    double viewStart = 0.0;
    double spp = 1.0;
    juce::int64 cursor = 0;
    juce::int64 selStart = 0, selEnd = 0;
};
} // namespace wt
