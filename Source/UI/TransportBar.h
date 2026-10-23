#pragma once

#include "Core/AudioData.h"
#include "ViewState.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace wt
{
/** Play controls, big time readout and RX-style Sel/View Start/End/Length table. */
class TransportBar : public juce::Component
{
public:
    TransportBar();

    std::function<void()> onToStart, onPlay, onStop;
    std::function<void (bool)> onLoopChanged;

    void setLooping (bool shouldLoop) { loopButton.setToggleState (shouldLoop, juce::dontSendNotification); }
    bool isLooping() const { return loopButton.getToggleState(); }

    void update (const ViewState& view, const AudioDataPtr& audio, juce::int64 playhead, bool playing);

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    juce::TextButton toStartButton { "|<" }, playButton, stopButton, loopButton { "Loop" };
    juce::String timeText { "00:00:00.000" }, formatText;
    juce::StringArray selRow, viewRow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransportBar)
};
} // namespace wt
