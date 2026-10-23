#include "OverviewBar.h"
#include "Theme.h"

namespace wt
{
OverviewBar::OverviewBar (ViewState& v)
    : view (v)
{
    setOpaque (true);
    view.addChangeListener (this);
}

OverviewBar::~OverviewBar()
{
    view.removeChangeListener (this);
}

void OverviewBar::setAudio (AudioDataPtr newAudio)
{
    audio = std::move (newAudio);
    playhead = -1;
    rebuildImage();
    repaint();
}

void OverviewBar::setPlayhead (juce::int64 sample)
{
    if (sample != playhead)
    {
        playhead = sample;
        repaint();
    }
}

void OverviewBar::resized()
{
    rebuildImage();
}

float OverviewBar::sampleToX (double sample) const
{
    if (audio == nullptr || audio->getNumSamples() == 0)
        return 0.0f;
    return (float) (sample / (double) audio->getNumSamples() * getWidth());
}

void OverviewBar::rebuildImage()
{
    if (audio == nullptr || getWidth() <= 0 || getHeight() <= 0)
    {
        image = {};
        return;
    }

    image = juce::Image (juce::Image::ARGB, getWidth(), getHeight(), true);
    juce::Graphics g (image);

    const auto n = audio->getNumSamples();
    const auto mid = getHeight() * 0.5f;
    const auto half = getHeight() * 0.45f;
    const auto spp = (double) n / getWidth();

    juce::RectangleList<float> rects;
    for (int x = 0; x < getWidth(); ++x)
    {
        const auto s0 = (juce::int64) (x * spp);
        const auto s1 = (juce::int64) ((x + 1) * spp) + 1;
        MinMax mm { 1.0f, -1.0f };
        for (int ch = 0; ch < audio->getNumChannels(); ++ch)
        {
            const auto r = audio->peaks.getRange (audio->buffer, ch, s0, s1);
            mm.lo = juce::jmin (mm.lo, r.lo);
            mm.hi = juce::jmax (mm.hi, r.hi);
        }
        if (mm.hi < mm.lo)
            continue;
        rects.addWithoutMerging ({ (float) x, mid - mm.hi * half, 1.0f, juce::jmax (1.0f, (mm.hi - mm.lo) * half) });
    }

    g.setColour (theme::waveform.withAlpha (0.75f));
    g.fillRectList (rects);
}

void OverviewBar::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    if (audio == nullptr)
        return;

    g.drawImageAt (image, 0, 0);

    const auto h = (float) getHeight();

    if (view.hasSelection())
    {
        const auto x1 = sampleToX ((double) view.getSelectionStart());
        const auto x2 = sampleToX ((double) view.getSelectionEnd());
        g.setColour (juce::Colours::white.withAlpha (0.2f));
        g.fillRect (juce::Rectangle<float> (x1, 0.0f, juce::jmax (1.0f, x2 - x1), h));
    }

    const auto v1 = sampleToX (view.getViewStart());
    const auto v2 = sampleToX (view.getViewEnd());
    const juce::Rectangle<float> visible (v1, 0.5f, juce::jmax (2.0f, v2 - v1), h - 1.0f);
    g.setColour (theme::accent.withAlpha (0.18f));
    g.fillRect (visible);
    g.setColour (theme::accent);
    g.drawRect (visible, 1.0f);

    g.setColour (theme::cursor);
    g.drawVerticalLine ((int) sampleToX ((double) view.getCursor()), 0.0f, h);

    if (playhead >= 0)
    {
        g.setColour (theme::playhead);
        g.drawVerticalLine ((int) sampleToX ((double) playhead), 0.0f, h);
    }
}

void OverviewBar::mouseDown (const juce::MouseEvent& e)
{
    if (auto* parent = getParentComponent())
        parent->grabKeyboardFocus();
    mouseDrag (e);
}

void OverviewBar::mouseDrag (const juce::MouseEvent& e)
{
    if (audio == nullptr || getWidth() <= 0)
        return;
    view.centreOn ((double) e.position.x / getWidth() * (double) audio->getNumSamples());
}
} // namespace wt
