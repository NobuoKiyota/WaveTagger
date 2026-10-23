#include "TransportBar.h"
#include "Theme.h"
#include "Util/Text.h"

namespace wt
{
namespace
{
constexpr int tableWidth = 420;
}

TransportBar::TransportBar()
{
    playButton.setButtonText (WT_U8 ("▶"));
    stopButton.setButtonText (WT_U8 ("■"));
    loopButton.setClickingTogglesState (true);

    for (auto* b : { &toStartButton, &playButton, &stopButton, &loopButton })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }

    toStartButton.onClick = [this] { if (onToStart) onToStart(); };
    playButton.onClick = [this] { if (onPlay) onPlay(); };
    stopButton.onClick = [this] { if (onStop) onStop(); };
    loopButton.onClick = [this] { if (onLoopChanged) onLoopChanged (loopButton.getToggleState()); };

    selRow = { "-", "-", "-" };
    viewRow = { "-", "-", "-" };
}

void TransportBar::update (const ViewState& view, const AudioDataPtr& audio, juce::int64 playhead, bool playing)
{
    playButton.setToggleState (playing, juce::dontSendNotification);

    if (audio == nullptr)
        return;

    const auto sr = audio->sampleRate;
    auto t = [sr] (double samples) { return formatTime (samples / sr); };

    timeText = t ((double) (playhead >= 0 ? playhead : view.getCursor()));

    if (view.hasSelection())
        selRow = { t ((double) view.getSelectionStart()), t ((double) view.getSelectionEnd()),
                   t ((double) (view.getSelectionEnd() - view.getSelectionStart())) };
    else
        selRow = { "-", "-", "-" };

    viewRow = { t (view.getViewStart()), t (view.getViewEnd()), t (view.getViewEnd() - view.getViewStart()) };

    formatText = juce::String (audio->isFloat ? "32-bit float" : juce::String (audio->bitsPerSample) + "-bit")
               + " | " + juce::String ((int) sr) + " Hz | " + juce::String (audio->getNumChannels()) + " ch | "
               + audio->formatName;
    repaint();
}

void TransportBar::resized()
{
    auto r = getLocalBounds().reduced (8, 0);
    auto buttons = r.removeFromLeft (200).withSizeKeepingCentre (200, 28);
    toStartButton.setBounds (buttons.removeFromLeft (40));
    buttons.removeFromLeft (4);
    playButton.setBounds (buttons.removeFromLeft (48));
    buttons.removeFromLeft (4);
    stopButton.setBounds (buttons.removeFromLeft (40));
    buttons.removeFromLeft (4);
    loopButton.setBounds (buttons.removeFromLeft (56));
}

void TransportBar::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    g.setColour (theme::outline);
    g.drawHorizontalLine (0, 0.0f, (float) getWidth());

    auto r = getLocalBounds().reduced (8, 4);
    r.removeFromLeft (212);

    g.setColour (theme::text);
    g.setFont (juce::FontOptions ("Consolas", 26.0f, juce::Font::plain));
    g.drawText (timeText, r.removeFromLeft (190), juce::Justification::centredLeft);

    auto table = r.removeFromRight (tableWidth);
    const auto colW = (table.getWidth() - 40) / 3;
    const auto rowH = table.getHeight() / 3;

    g.setFont (juce::FontOptions (11.0f));
    g.setColour (theme::textDim);
    const char* headers[] = { "Start", "End", "Length" };
    for (int c = 0; c < 3; ++c)
        g.drawText (headers[c], table.getX() + 40 + c * colW, table.getY(), colW, rowH, juce::Justification::centredLeft);

    auto drawRow = [&] (const char* name, const juce::StringArray& values, int row) {
        const auto y = table.getY() + rowH * row;
        g.setColour (theme::textDim);
        g.drawText (name, table.getX(), y, 40, rowH, juce::Justification::centredLeft);
        g.setColour (theme::text);
        g.setFont (juce::FontOptions ("Consolas", 12.5f, juce::Font::plain));
        for (int c = 0; c < 3; ++c)
            g.drawText (values[c], table.getX() + 40 + c * colW, y, colW, rowH, juce::Justification::centredLeft);
        g.setFont (juce::FontOptions (11.0f));
    };
    drawRow ("Sel", selRow, 1);
    drawRow ("View", viewRow, 2);

    g.setColour (theme::textDim);
    g.setFont (juce::FontOptions (11.5f));
    g.drawText (formatText, r.reduced (12, 0), juce::Justification::centredRight);
}
} // namespace wt
