#pragma once

#include "Core/AudioData.h"
#include "ViewState.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace wt
{
/** Whole-file waveform with the visible range; click/drag to navigate. */
class OverviewBar : public juce::Component,
                    private juce::ChangeListener
{
public:
    explicit OverviewBar (ViewState& view);
    ~OverviewBar() override;

    void setAudio (AudioDataPtr newAudio);
    void setPlayhead (juce::int64 sample);

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
    void rebuildImage();
    float sampleToX (double sample) const;

    ViewState& view;
    AudioDataPtr audio;
    juce::Image image;
    juce::int64 playhead = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OverviewBar)
};
} // namespace wt
