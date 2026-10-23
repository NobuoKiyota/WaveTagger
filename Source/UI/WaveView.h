#pragma once

#include "Core/CycleModel.h"
#include "Core/SpectrogramRenderer.h"
#include "ViewState.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace wt
{
/** RX-style spectrogram + waveform overlay, one lane per channel, with time ruler and frequency axis. */
class WaveView : public juce::Component,
                 private juce::ChangeListener,
                 private juce::Timer
{
public:
    static constexpr int leftStrip = 18;
    static constexpr int rightAxis = 58;
    static constexpr int rulerHeight = 24;
    static constexpr int laneGap = 3;

    WaveView (ViewState& view, SpectrogramRenderer& spectrogram);
    ~WaveView() override;

    void setAudio (AudioDataPtr newAudio);
    void setPlayhead (juce::int64 sample);
    /** 0 = spectrogram only, 0.5 = both, 1 = waveform only. */
    void setBlend (float newBlend);
    void setFloorDb (float db);

    void setCycleModel (CycleModel* model);
    void setSelectedCycles (const juce::Array<int>& rows);

    /** Called when the user clicks (without dragging) to move the cursor. */
    std::function<void (juce::int64)> onCursorClicked;
    std::function<void (int cycleIndex)> onCycleClicked;
    /** type: 0..8 slot, -1 untyped, -2 delete. */
    std::function<void (int cycleIndex, int type)> onCycleMenuChoice;

    juce::Rectangle<int> getLanesArea() const;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    void mouseMagnify (const juce::MouseEvent& e, float scaleFactor) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void drawCycles (juce::Graphics& g);
    void showCycleMenu (int cycleIndex);

    void requestSpectrogram();
    juce::Rectangle<int> getLaneBounds (int channel) const;
    int getLaneHeight() const;
    float sampleToX (double sample) const;
    juce::int64 xToSample (float x) const;

    void drawSpectrogram (juce::Graphics& g);
    void drawWaveform (juce::Graphics& g, int channel, juce::Rectangle<int> lane, float alpha);
    void drawOverlays (juce::Graphics& g);
    void drawFrequencyAxis (juce::Graphics& g, juce::Rectangle<int> lane);
    void drawRuler (juce::Graphics& g);

    ViewState& view;
    SpectrogramRenderer& spectrogram;
    AudioDataPtr audio;
    juce::int64 playhead = -1;
    float blend = 0.5f;
    float floorDb = -110.0f;
    CycleModel* cycles = nullptr;
    juce::Array<int> selectedCycles;

    bool dragging = false;
    juce::int64 dragAnchor = 0;
    float lastMouseX = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveView)
};
} // namespace wt
