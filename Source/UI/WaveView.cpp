#include "WaveView.h"
#include "Theme.h"
#include "Util/Text.h"

namespace wt
{
WaveView::WaveView (ViewState& v, SpectrogramRenderer& s)
    : view (v), spectrogram (s)
{
    setOpaque (true);
    setWantsKeyboardFocus (false);
    view.addChangeListener (this);
}

WaveView::~WaveView()
{
    view.removeChangeListener (this);
}

void WaveView::setAudio (AudioDataPtr newAudio)
{
    audio = std::move (newAudio);
    playhead = -1;
    requestSpectrogram();
    repaint();
}

void WaveView::setPlayhead (juce::int64 sample)
{
    if (sample == playhead)
        return;

    const auto area = getLanesArea();
    auto dirty = [&] (juce::int64 s) {
        if (s >= 0)
            repaint ((int) sampleToX ((double) s) - 2, area.getY(), 5, area.getHeight());
    };
    dirty (playhead);
    playhead = sample;
    dirty (playhead);
}

void WaveView::setBlend (float newBlend)
{
    blend = juce::jlimit (0.0f, 1.0f, newBlend);
    repaint();
}

void WaveView::setFloorDb (float db)
{
    floorDb = db;
    requestSpectrogram();
}

juce::Rectangle<int> WaveView::getLanesArea() const
{
    return getLocalBounds().withTrimmedLeft (leftStrip).withTrimmedRight (rightAxis).withTrimmedBottom (rulerHeight);
}

int WaveView::getLaneHeight() const
{
    const auto n = audio != nullptr ? audio->getNumChannels() : 1;
    return juce::jmax (1, (getLanesArea().getHeight() - laneGap * (n - 1)) / n);
}

juce::Rectangle<int> WaveView::getLaneBounds (int channel) const
{
    const auto area = getLanesArea();
    const auto h = getLaneHeight();
    return { area.getX(), area.getY() + channel * (h + laneGap), area.getWidth(), h };
}

float WaveView::sampleToX (double sample) const
{
    return (float) getLanesArea().getX() + (float) ((sample - view.getViewStart()) / view.getSamplesPerPixel());
}

juce::int64 WaveView::xToSample (float x) const
{
    const auto s = view.getViewStart() + (double) (x - (float) getLanesArea().getX()) * view.getSamplesPerPixel();
    return juce::jlimit<juce::int64> (0, view.getNumSamples(), (juce::int64) std::llround (s));
}

void WaveView::resized()
{
    view.setWidth (getLanesArea().getWidth());
    requestSpectrogram();
}

void WaveView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    requestSpectrogram();
    repaint();
}

void WaveView::requestSpectrogram()
{
    if (audio == nullptr || getWidth() <= 0)
        return;

    SpectrogramRenderer::Params p;
    p.audio = audio;
    p.samplesPerPixel = view.getSamplesPerPixel();
    p.firstColumn = (juce::int64) std::floor (view.getViewStart() / p.samplesPerPixel);
    p.width = getLanesArea().getWidth() + 2;
    p.laneHeight = getLaneHeight();
    p.floorDb = floorDb;
    spectrogram.request (p);
}

//==============================================================================
void WaveView::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);

    if (audio == nullptr)
    {
        g.setColour (theme::textDim);
        g.setFont (juce::FontOptions (18.0f));
        g.drawText (WT_U8 ("音声ファイルをドロップ  /  Ctrl+O で開く"), getLanesArea(), juce::Justification::centred);
        return;
    }

    for (int ch = 0; ch < audio->getNumChannels(); ++ch)
    {
        g.setColour (juce::Colours::black);
        g.fillRect (getLaneBounds (ch));
    }

    drawSpectrogram (g);

    const auto waveAlpha = juce::jmin (1.0f, 2.0f * blend);
    for (int ch = 0; ch < audio->getNumChannels(); ++ch)
    {
        const auto lane = getLaneBounds (ch);
        if (waveAlpha > 0.0f)
            drawWaveform (g, ch, lane, waveAlpha);

        drawFrequencyAxis (g, lane);

        g.setColour (theme::textDim);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        const auto label = audio->getNumChannels() == 2 ? juce::String (ch == 0 ? "L" : "R") : juce::String (ch + 1);
        g.drawText (label, juce::Rectangle<int> (0, lane.getY(), leftStrip, lane.getHeight()), juce::Justification::centred);
    }

    drawOverlays (g);
    drawRuler (g);
}

void WaveView::drawSpectrogram (juce::Graphics& g)
{
    const auto specAlpha = juce::jmin (1.0f, 2.0f * (1.0f - blend));
    if (specAlpha <= 0.0f)
        return;

    const auto result = spectrogram.getLatest();
    if (! result.image.isValid() || result.params.audio != audio)
        return;

    const auto& rp = result.params;
    const auto area = getLanesArea();
    const auto dx = (double) area.getX() + ((double) rp.firstColumn * rp.samplesPerPixel - view.getViewStart()) / view.getSamplesPerPixel();
    const auto dw = (double) rp.width * rp.samplesPerPixel / view.getSamplesPerPixel();

    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (area);
    g.setOpacity (specAlpha);
    g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);

    for (int ch = 0; ch < audio->getNumChannels(); ++ch)
    {
        const auto lane = getLaneBounds (ch);
        g.drawImage (result.image,
                     (int) std::floor (dx), lane.getY(), juce::jmax (1, (int) std::ceil (dw)), lane.getHeight(),
                     0, ch * rp.laneHeight, rp.width, rp.laneHeight);
    }
}

void WaveView::drawWaveform (juce::Graphics& g, int channel, juce::Rectangle<int> lane, float alpha)
{
    const auto mid = (float) lane.getCentreY();
    const auto half = (float) lane.getHeight() * 0.5f * 0.96f;
    const auto spp = view.getSamplesPerPixel();
    const auto viewStart = view.getViewStart();

    g.setColour (theme::waveform.withAlpha (alpha * 0.35f));
    g.drawHorizontalLine ((int) mid, (float) lane.getX(), (float) lane.getRight());

    g.setColour (theme::waveform.withAlpha (alpha));

    if (spp >= 1.0)
    {
        juce::RectangleList<float> rects;
        rects.ensureStorageAllocated (lane.getWidth());

        for (int x = 0; x < lane.getWidth(); ++x)
        {
            const auto s0 = viewStart + x * spp;
            const auto mm = audio->peaks.getRange (audio->buffer, channel, (juce::int64) s0, (juce::int64) (s0 + spp) + 1);
            const auto y1 = mid - mm.hi * half;
            const auto y2 = mid - mm.lo * half;
            rects.addWithoutMerging ({ (float) (lane.getX() + x), y1, 1.0f, juce::jmax (1.0f, y2 - y1) });
        }
        g.fillRectList (rects);
    }
    else
    {
        const auto* data = audio->buffer.getReadPointer (channel);
        const auto first = juce::jmax<juce::int64> (0, (juce::int64) std::floor (viewStart) - 1);
        const auto last = juce::jmin<juce::int64> (audio->getNumSamples() - 1, (juce::int64) std::ceil (view.getViewEnd()) + 1);

        juce::Path path;
        for (auto s = first; s <= last; ++s)
        {
            const auto pt = juce::Point<float> (sampleToX ((double) s), mid - data[s] * half);
            if (s == first)
                path.startNewSubPath (pt);
            else
                path.lineTo (pt);

            if (spp < 1.0 / 8.0)
                g.fillEllipse (pt.x - 2.0f, pt.y - 2.0f, 4.0f, 4.0f);
        }
        g.strokePath (path, juce::PathStrokeType (1.3f));
    }
}

void WaveView::drawOverlays (juce::Graphics& g)
{
    const auto area = getLanesArea();
    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (area);

    if (view.hasSelection())
    {
        const auto x1 = sampleToX ((double) view.getSelectionStart());
        const auto x2 = sampleToX ((double) view.getSelectionEnd());
        g.setColour (juce::Colours::white.withAlpha (0.18f));
        g.fillRect (juce::Rectangle<float> (x1, (float) area.getY(), x2 - x1, (float) area.getHeight()));

        g.setColour (juce::Colours::white.withAlpha (0.75f));
        const float dashes[] = { 3.0f, 3.0f };
        g.drawDashedLine ({ x1, (float) area.getY(), x1, (float) area.getBottom() }, dashes, 2, 1.0f);
        g.drawDashedLine ({ x2, (float) area.getY(), x2, (float) area.getBottom() }, dashes, 2, 1.0f);
    }

    g.setColour (theme::cursor);
    g.drawVerticalLine ((int) sampleToX ((double) view.getCursor()), (float) area.getY(), (float) area.getBottom());

    if (playhead >= 0)
    {
        g.setColour (theme::playhead);
        g.fillRect (juce::Rectangle<float> (sampleToX ((double) playhead) - 0.75f, (float) area.getY(), 1.5f, (float) area.getHeight()));
    }
}

void WaveView::drawFrequencyAxis (juce::Graphics& g, juce::Rectangle<int> lane)
{
    static const double freqs[] = { 100, 300, 500, 1000, 1500, 2000, 3000, 5000, 7000, 10000, 15000, 20000, 30000, 40000 };
    const auto sr = audio->sampleRate;
    const auto x = lane.getRight();

    g.setFont (juce::FontOptions (10.5f));
    for (auto f : freqs)
    {
        if (f > sr * 0.5 * 0.97)
            break;

        const auto y = (float) lane.getBottom() - SpectrogramRenderer::freqToNorm (f, sr) * (float) lane.getHeight();
        if (y < (float) lane.getY() + 6.0f)
            continue;

        g.setColour (theme::outline);
        g.drawHorizontalLine ((int) y, (float) x, (float) x + 5.0f);

        juce::String label;
        if (f >= 1000)
            label = (std::fmod (f, 1000.0) == 0.0 ? juce::String ((int) (f / 1000)) : juce::String (f / 1000.0, 1)) + "k";
        else
            label = juce::String ((int) f);

        g.setColour (theme::textDim);
        g.drawText (label, juce::Rectangle<float> ((float) x + 7.0f, y - 7.0f, (float) rightAxis - 9.0f, 14.0f), juce::Justification::centredLeft);
    }
}

void WaveView::drawRuler (juce::Graphics& g)
{
    const auto area = getLanesArea();
    const juce::Rectangle<int> ruler (area.getX(), area.getBottom(), area.getWidth(), rulerHeight);

    g.setColour (theme::panel);
    g.fillRect (ruler.withX (0).withWidth (getWidth()));

    const auto sr = view.getSampleRate();
    const auto secondsPerPixel = view.getSamplesPerPixel() / sr;
    const auto target = 90.0 * secondsPerPixel;

    static const double steps[] = { 0.0001, 0.0002, 0.0005, 0.001, 0.002, 0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.5,
                                    1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 900, 1800, 3600 };
    double step = steps[std::size (steps) - 1];
    for (auto s : steps)
        if (s >= target)
        {
            step = s;
            break;
        }

    const auto decimals = step < 0.001 ? 4 : step < 0.01 ? 3 : step < 0.1 ? 2 : step < 1.0 ? 1 : 0;
    const auto startSec = view.getViewStart() / sr;
    const auto endSec = view.getViewEnd() / sr;
    const auto minor = step / 5.0;

    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (ruler);
    g.setFont (juce::FontOptions (11.0f));

    for (auto t = std::floor (startSec / minor) * minor; t <= endSec + minor; t += minor)
    {
        const auto x = (float) area.getX() + (float) ((t - startSec) / secondsPerPixel);
        const auto isMajor = std::abs (std::remainder (t, step)) < minor * 0.01;

        g.setColour (theme::outline.brighter (isMajor ? 0.4f : 0.0f));
        g.drawVerticalLine ((int) x, (float) ruler.getY(), (float) ruler.getY() + (isMajor ? 8.0f : 4.0f));

        if (isMajor)
        {
            juce::String label;
            if (t >= 60.0)
            {
                const auto m = (int) (t / 60.0);
                label = juce::String (m) + ":" + juce::String (t - m * 60.0, decimals).paddedLeft ('0', decimals > 0 ? 3 + decimals : 2);
            }
            else
            {
                label = juce::String (t, decimals);
            }

            g.setColour (theme::textDim);
            g.drawText (label, juce::Rectangle<float> (x + 3.0f, (float) ruler.getY() + 4.0f, 80.0f, (float) rulerHeight - 6.0f),
                        juce::Justification::centredLeft);
        }
    }

    g.setColour (theme::textDim);
    g.drawText ("sec", ruler.withX (area.getRight()).withWidth (rightAxis), juce::Justification::centred);
}

//==============================================================================
void WaveView::mouseDown (const juce::MouseEvent& e)
{
    if (auto* parent = getParentComponent())
        parent->grabKeyboardFocus();

    if (audio == nullptr)
        return;

    const auto s = xToSample (e.position.x);
    lastMouseX = e.position.x;

    if (e.mods.isShiftDown())
    {
        if (view.hasSelection())
            dragAnchor = std::abs (s - view.getSelectionStart()) < std::abs (s - view.getSelectionEnd())
                           ? view.getSelectionEnd()
                           : view.getSelectionStart();
        else
            dragAnchor = view.getCursor();

        dragging = true;
        view.setSelection (dragAnchor, s);
    }
    else
    {
        dragAnchor = s;
        dragging = false;
    }
}

void WaveView::mouseDrag (const juce::MouseEvent& e)
{
    if (audio == nullptr)
        return;

    lastMouseX = e.position.x;

    if (! dragging && std::abs (e.getDistanceFromDragStartX()) < 3)
        return;

    dragging = true;
    view.setSelection (dragAnchor, xToSample (e.position.x));

    const auto area = getLanesArea();
    if (e.position.x < (float) area.getX() || e.position.x > (float) area.getRight())
        startTimerHz (30);
    else
        stopTimer();
}

void WaveView::mouseUp (const juce::MouseEvent& e)
{
    stopTimer();
    if (audio == nullptr)
        return;

    if (! dragging)
    {
        const auto s = xToSample (e.position.x);
        view.clearSelection();
        view.setCursor (s);
        if (onCursorClicked)
            onCursorClicked (s);
    }
    else if (view.hasSelection())
    {
        view.setCursor (view.getSelectionStart());
    }

    dragging = false;
}

void WaveView::timerCallback()
{
    const auto area = getLanesArea();
    if (lastMouseX < (float) area.getX())
        view.scrollPixels ((double) (lastMouseX - (float) area.getX()));
    else if (lastMouseX > (float) area.getRight())
        view.scrollPixels ((double) (lastMouseX - (float) area.getRight()));
    else
        stopTimer();

    view.setSelection (dragAnchor, xToSample (lastMouseX));
}

void WaveView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (audio == nullptr)
        return;

    const auto px = (double) (e.position.x - (float) getLanesArea().getX());

    if (e.mods.isCtrlDown() || e.mods.isCommandDown())
    {
        view.zoomAround (px, std::pow (0.5, (double) wheel.deltaY * 4.0));
    }
    else
    {
        const auto delta = wheel.deltaX != 0.0f ? wheel.deltaX : wheel.deltaY;
        view.scrollPixels (-(double) delta * getLanesArea().getWidth() * 0.5);
    }
}

void WaveView::mouseMagnify (const juce::MouseEvent& e, float scaleFactor)
{
    if (audio != nullptr && scaleFactor > 0.0f)
        view.zoomAround ((double) (e.position.x - (float) getLanesArea().getX()), 1.0 / scaleFactor);
}
} // namespace wt
