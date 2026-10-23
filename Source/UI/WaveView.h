#pragma once

#include "Core/CycleModel.h"
#include "Core/SpectrogramRenderer.h"
#include "InputMap.h"
#include "ViewState.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace wt
{
/** RX-style spectrogram + waveform overlay, one lane per channel, with time ruler, dB and frequency axes. */
class WaveView : public juce::Component,
                 private juce::ChangeListener,
                 private juce::Timer
{
public:
    static constexpr int leftStrip = 18;
    static constexpr int dbAxisWidth = 44;
    static constexpr int freqAxisWidth = 58;
    static constexpr int rightAxis = dbAxisWidth + freqAxisWidth;
    static constexpr int rulerHeight = 24;
    static constexpr int laneGap = 3;

    WaveView (ViewState& view, SpectrogramRenderer& spectrogram);
    ~WaveView() override;

    void setAudio (AudioDataPtr newAudio);
    void setPlayhead (juce::int64 sample);
    /** 0 = spectrogram only, 0.5 = both, 1 = waveform only. */
    void setBlend (float newBlend);
    void setFloorDb (float db);
    void setColourScheme (int scheme);
    void setWheelMap (const WheelMap* map) { wheelMap = map; }

    void setCycleModel (CycleModel* model);
    void setSelectedCycles (const juce::Array<int>& rows);

    /** Called when the user clicks (without dragging) to move the cursor. */
    std::function<void (juce::int64)> onCursorClicked;
    std::function<void (int cycleIndex)> onCycleClicked;
    /** Double click on a cycle: the caller loads its range into the selection for re-selecting. */
    std::function<void (int cycleIndex)> onCycleDoubleClicked;
    /** A cycle's edge was dragged or the cycle was moved (called on release with the new index). */
    std::function<void (int cycleIndex)> onCycleEdited;
    /** type: 0..8 slot, -1 untyped, -2 delete. */
    std::function<void (int cycleIndex, int type)> onCycleMenuChoice;

    juce::Rectangle<int> getLanesArea() const;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    void mouseMagnify (const juce::MouseEvent& e, float scaleFactor) override;

private:
    enum class Zone { Wave, DbAxis, FreqAxis, Other };
    enum class DragMode { None, Select, EdgeStart, EdgeEnd, Move };

    struct CycleHit
    {
        int index = -1;
        int edge = 0; // -1 start edge, +1 end edge, 0 inside
    };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    void requestSpectrogram();
    juce::Rectangle<int> getLaneBounds (int channel) const;
    int getLaneHeight() const;
    int laneIndexAt (float y) const;
    float sampleToX (double sample) const;
    juce::int64 xToSample (float x) const;
    Zone zoneAt (juce::Point<float> p) const;
    double yToNorm (juce::Rectangle<int> lane, float y) const;
    float normToY (juce::Rectangle<int> lane, double norm) const;
    CycleHit hitTestCycle (juce::Point<float> p) const;
    void computeNeighbourBounds (int index, juce::int64& prevLimit, juce::int64& nextLimit) const;

    void drawSpectrogram (juce::Graphics& g);
    void drawWaveform (juce::Graphics& g, int channel, juce::Rectangle<int> lane, float alpha);
    void drawOverlays (juce::Graphics& g);
    void drawCycles (juce::Graphics& g);
    void drawFrequencyAxis (juce::Graphics& g, juce::Rectangle<int> lane);
    void drawDbAxis (juce::Graphics& g, juce::Rectangle<int> lane);
    void drawRuler (juce::Graphics& g);
    void showCycleMenu (int cycleIndex);

    ViewState& view;
    SpectrogramRenderer& spectrogram;
    AudioDataPtr audio;
    const WheelMap* wheelMap = nullptr;
    juce::int64 playhead = -1;
    float blend = 0.5f;
    float floorDb = -110.0f;
    int colourScheme = 0;
    CycleModel* cycles = nullptr;
    juce::Array<int> selectedCycles;

    DragMode dragMode = DragMode::None;
    bool dragging = false;
    bool justDoubleClicked = false;
    bool editStarted = false;
    int editIndex = -1;
    juce::int64 dragAnchor = 0, origStart = 0, origEnd = 0, prevLimit = 0, nextLimit = 0;
    float lastMouseX = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveView)
};
} // namespace wt
