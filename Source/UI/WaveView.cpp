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
    if (cycles != nullptr)
        cycles->removeChangeListener (this);
}

void WaveView::setCycleModel (CycleModel* model)
{
    if (cycles != nullptr)
        cycles->removeChangeListener (this);
    cycles = model;
    if (cycles != nullptr)
        cycles->addChangeListener (this);
    repaint();
}

void WaveView::setSelectedCycles (const juce::Array<int>& rows)
{
    selectedCycles = rows;
    repaint();
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

void WaveView::setColourScheme (int scheme)
{
    colourScheme = scheme;
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

int WaveView::laneIndexAt (float y) const
{
    const auto n = audio != nullptr ? audio->getNumChannels() : 1;
    for (int ch = 0; ch < n; ++ch)
        if (y < (float) getLaneBounds (ch).getBottom() + (float) laneGap * 0.5f)
            return ch;
    return n - 1;
}

WaveView::Zone WaveView::zoneAt (juce::Point<float> p) const
{
    const auto lanes = getLanesArea();
    if (p.y < (float) lanes.getY() || p.y >= (float) (lanes.getBottom() + rulerHeight))
        return Zone::Other;

    if (p.x >= (float) lanes.getX() && p.x < (float) lanes.getRight())
        return Zone::Wave;

    if (p.y >= (float) lanes.getBottom())
        return Zone::Other;

    if (p.x >= (float) lanes.getRight() && p.x < (float) (lanes.getRight() + dbAxisWidth))
        return Zone::DbAxis;
    if (p.x >= (float) (lanes.getRight() + dbAxisWidth))
        return Zone::FreqAxis;
    return Zone::Other;
}

double WaveView::yToNorm (juce::Rectangle<int> lane, float y) const
{
    const auto rel = 1.0 - (double) (y - (float) lane.getY()) / (double) lane.getHeight();
    return view.getFreqLo() + rel * (view.getFreqHi() - view.getFreqLo());
}

float WaveView::normToY (juce::Rectangle<int> lane, double norm) const
{
    const auto rel = (norm - view.getFreqLo()) / (view.getFreqHi() - view.getFreqLo());
    return (float) lane.getBottom() - (float) rel * (float) lane.getHeight();
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

void WaveView::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &view)
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
    p.normLo = view.getFreqLo();
    p.normHi = view.getFreqHi();
    p.scheme = colourScheme;
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

        drawDbAxis (g, lane);
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
    const auto half = (float) lane.getHeight() * 0.5f * 0.96f * (float) view.getWaveGain();
    const auto spp = view.getSamplesPerPixel();
    const auto viewStart = view.getViewStart();

    juce::Graphics::ScopedSaveState clipState (g);
    g.reduceClipRegion (lane);

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
    drawCycles (g);

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

void WaveView::drawCycles (juce::Graphics& g)
{
    if (cycles == nullptr)
        return;

    const auto area = getLanesArea();
    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (area);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));

    const auto v0 = view.getViewStart(), v1 = view.getViewEnd();
    for (int i = 0; i < cycles->size(); ++i)
    {
        const auto& c = (*cycles)[i];
        if ((double) c.end < v0 || (double) c.start > v1)
            continue;

        const auto selected = selectedCycles.contains (i);
        const auto colour = c.type >= 0 ? cycles->getType (c.type).colour : juce::Colours::grey;
        const auto x1 = sampleToX ((double) c.start);
        const auto x2 = sampleToX ((double) c.end);
        const juce::Rectangle<float> r (x1, (float) area.getY(), juce::jmax (1.0f, x2 - x1), (float) area.getHeight());

        // NG cycles stay visible but recede
        const auto fade = c.ng ? 0.4f : 1.0f;
        g.setColour (colour.withAlpha ((selected ? 0.30f : 0.13f) * fade));
        g.fillRect (r);
        g.setColour (colour.withAlpha ((selected ? 1.0f : 0.7f) * fade));
        g.drawRect (r, selected ? 2.0f : 1.0f);

        const auto tag = r.withHeight (16.0f);
        g.setColour (colour.withAlpha ((selected ? 1.0f : 0.8f) * fade));
        g.fillRect (tag);
        if (tag.getWidth() > 28.0f)
        {
            g.setColour (colour.getBrightness() > 0.6f ? juce::Colours::black : juce::Colours::white);
            g.drawText ((c.ng ? juce::String ("NG  ") : juce::String()) + cycles->nameOf (i), tag.reduced (3.0f, 0.0f), juce::Justification::centredLeft, true);
        }
    }
}

void WaveView::showCycleMenu (int cycleIndex)
{
    juce::PopupMenu menu;
    menu.addSectionHeader (cycles->nameOf (cycleIndex));
    menu.addItem (99, WT_U8 ("— (未分類)"), true, (*cycles)[cycleIndex].type < 0);
    for (int i = 0; i < CycleModel::numSlots; ++i)
        if (cycles->isTypeUsable (i))
            menu.addItem (100 + i, juce::String (i + 1) + "  " + cycles->getType (i).name, true, (*cycles)[cycleIndex].type == i);
    menu.addSeparator();
    menu.addItem (1, WT_U8 ("削除"));

    juce::Component::SafePointer<WaveView> safe (this);
    menu.showMenuAsync ({}, [safe, cycleIndex] (int result) {
        if (safe == nullptr || result == 0 || ! safe->onCycleMenuChoice)
            return;
        safe->onCycleMenuChoice (cycleIndex, result == 1 ? -2 : result == 99 ? -1 : result - 100);
    });
}

void WaveView::drawDbAxis (juce::Graphics& g, juce::Rectangle<int> lane)
{
    const auto x0 = lane.getRight();
    const auto mid = (float) lane.getCentreY();
    const auto half = (float) lane.getHeight() * 0.5f * 0.96f * (float) view.getWaveGain();

    g.setColour (theme::outline);
    g.drawVerticalLine (x0, (float) lane.getY(), (float) lane.getBottom());

    static const float dbs[] = { 0, -1, -2, -3, -4, -6, -8, -10, -12, -15, -18, -20, -24, -30, -36, -42, -48, -60, -72, -90 };

    g.setFont (juce::FontOptions (10.5f));
    float lastY = (float) lane.getY();
    for (auto db : dbs)
    {
        const auto amp = std::pow (10.0f, db / 20.0f);
        const auto y = mid - amp * half;
        if (y < (float) lane.getY() + 7.0f || y > mid - 8.0f || y - lastY < 14.0f)
            continue;

        lastY = y;
        g.setColour (theme::outline);
        g.drawHorizontalLine ((int) y, (float) x0, (float) x0 + 5.0f);
        g.drawHorizontalLine ((int) (mid + (mid - y)), (float) x0, (float) x0 + 3.0f);

        g.setColour (theme::textDim);
        g.drawText (db == 0 ? juce::String ("0") : juce::String ((int) db),
                    juce::Rectangle<float> ((float) x0 + 7.0f, y - 7.0f, (float) dbAxisWidth - 9.0f, 14.0f), juce::Justification::centredLeft);
    }

    g.setColour (theme::textDim.withAlpha (0.7f));
    g.drawText ("dB", juce::Rectangle<int> (x0 + 7, lane.getBottom() - 16, dbAxisWidth - 9, 14), juce::Justification::centredLeft);
}

void WaveView::drawFrequencyAxis (juce::Graphics& g, juce::Rectangle<int> lane)
{
    const auto sr = audio->sampleRate;
    const auto x = lane.getRight() + dbAxisWidth;
    const auto range = view.getFreqHi() - view.getFreqLo();

    g.setColour (theme::outline);
    g.drawVerticalLine (x, (float) lane.getY(), (float) lane.getBottom());

    std::vector<double> mults { 1, 2, 3, 5, 7 };
    if (range < 0.5)
        for (double m : { 1.5, 4.0, 6.0, 8.0, 9.0 })
            mults.push_back (m);
    if (range < 0.2)
        for (double m : { 1.2, 1.4, 1.6, 1.8, 2.5, 3.5, 4.5, 5.5, 6.5, 7.5, 8.5 })
            mults.push_back (m);

    std::vector<double> freqs;
    for (double decade : { 10.0, 100.0, 1000.0, 10000.0 })
        for (auto m : mults)
            if (decade * m <= sr * 0.5 * 0.99)
                freqs.push_back (decade * m);
    std::sort (freqs.begin(), freqs.end());

    g.setFont (juce::FontOptions (10.5f));
    float lastY = (float) lane.getBottom() + 100.0f;
    for (auto f : freqs)
    {
        const auto y = normToY (lane, SpectrogramRenderer::freqToNorm (f, sr));
        if (y < (float) lane.getY() + 7.0f || y > (float) lane.getBottom() - 6.0f || lastY - y < 22.0f)
            continue;

        lastY = y;
        g.setColour (theme::outline);
        g.drawHorizontalLine ((int) y, (float) x, (float) x + 5.0f);

        juce::String label;
        if (f >= 1000)
            label = juce::String (f / 1000.0, std::fmod (f, 1000.0) == 0.0 ? 0 : (std::fmod (f * 10.0, 1000.0) == 0.0 ? 1 : 2)) + "k";
        else
            label = juce::String (f, std::fmod (f, 1.0) == 0.0 ? 0 : 1);

        g.setColour (theme::textDim);
        g.drawText (label, juce::Rectangle<float> ((float) x + 7.0f, y - 7.0f, (float) freqAxisWidth - 9.0f, 14.0f), juce::Justification::centredLeft);
    }

    if (view.getFreqHi() - view.getFreqLo() < 0.999)
    {
        g.setColour (theme::accent);
        g.setFont (juce::FontOptions (10.0f));
        g.drawText ("Hz", juce::Rectangle<int> (x + 7, lane.getBottom() - 16, freqAxisWidth - 9, 14), juce::Justification::centredLeft);
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
    g.drawText ("sec", ruler.withX (area.getRight() + dbAxisWidth).withWidth (freqAxisWidth), juce::Justification::centred);
}

//==============================================================================
void WaveView::computeNeighbourBounds (int index, juce::int64& prev, juce::int64& next) const
{
    prev = 0;
    next = view.getNumSamples();
    const auto& c = (*cycles)[index];
    for (int j = 0; j < cycles->size(); ++j)
    {
        if (j == index)
            continue;
        const auto& o = (*cycles)[j];
        if (o.end <= c.start)
            prev = juce::jmax (prev, o.end);
        else if (o.start >= c.end)
            next = juce::jmin (next, o.start);
    }
}

WaveView::CycleHit WaveView::hitTestCycle (juce::Point<float> p) const
{
    CycleHit best;
    if (cycles == nullptr || zoneAt (p) != Zone::Wave || p.y >= (float) getLanesArea().getBottom())
        return best;

    constexpr float tolerance = 5.0f;
    float bestDist = tolerance + 0.01f;
    for (int i = 0; i < cycles->size(); ++i)
    {
        const auto& c = (*cycles)[i];
        const auto x1 = sampleToX ((double) c.start), x2 = sampleToX ((double) c.end);
        if (x2 < p.x - tolerance || x1 > p.x + tolerance)
            continue;

        const auto d1 = std::abs (p.x - x1), d2 = std::abs (p.x - x2);
        if (d1 < bestDist)
        {
            bestDist = d1;
            best = { i, -1 };
        }
        if (d2 < bestDist)
        {
            bestDist = d2;
            best = { i, 1 };
        }
        if (best.index < 0 && p.x > x1 && p.x < x2)
            best = { i, 0 };
    }
    return best;
}

void WaveView::mouseMove (const juce::MouseEvent& e)
{
    if (audio == nullptr)
        return;

    const auto hit = hitTestCycle (e.position);
    if (hit.index >= 0 && hit.edge != 0)
        setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
    else if (hit.index >= 0 && e.mods.isAltDown())
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    else
        setMouseCursor (juce::MouseCursor::NormalCursor);
}

void WaveView::mouseExit (const juce::MouseEvent&)
{
    setMouseCursor (juce::MouseCursor::NormalCursor);
}

void WaveView::mouseDown (const juce::MouseEvent& e)
{
    if (auto* parent = getParentComponent())
        parent->grabKeyboardFocus();

    dragMode = DragMode::None;
    editStarted = false;
    editIndex = -1;
    justDoubleClicked = false;

    if (audio == nullptr || zoneAt (e.position) != Zone::Wave)
        return;

    const auto s = xToSample (e.position.x);
    lastMouseX = e.position.x;

    if (e.mods.isPopupMenu())
    {
        const auto idx = cycles != nullptr ? cycles->findAt (s) : -1;
        if (idx >= 0)
            showCycleMenu (idx);
        return;
    }

    const auto hit = hitTestCycle (e.position);
    if (hit.index >= 0 && (hit.edge != 0 || e.mods.isAltDown()))
    {
        editIndex = hit.index;
        origStart = (*cycles)[hit.index].start;
        origEnd = (*cycles)[hit.index].end;
        dragAnchor = s;
        computeNeighbourBounds (hit.index, prevLimit, nextLimit);
        dragMode = hit.edge < 0 ? DragMode::EdgeStart : hit.edge > 0 ? DragMode::EdgeEnd : DragMode::Move;
        return;
    }

    dragMode = DragMode::Select;
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

    if (dragMode == DragMode::EdgeStart || dragMode == DragMode::EdgeEnd || dragMode == DragMode::Move)
    {
        const auto minLen = juce::jmax<juce::int64> (1, (juce::int64) (audio->sampleRate * 0.001));
        const auto s = xToSample (e.position.x);
        auto ns = origStart, ne = origEnd;

        if (dragMode == DragMode::EdgeStart)
            ns = juce::jlimit (prevLimit, origEnd - minLen, s);
        else if (dragMode == DragMode::EdgeEnd)
            ne = juce::jlimit (origStart + minLen, nextLimit, s);
        else
        {
            const auto delta = juce::jlimit (prevLimit - origStart, nextLimit - origEnd, s - dragAnchor);
            ns = origStart + delta;
            ne = origEnd + delta;
        }

        if (ns == (*cycles)[editIndex].start && ne == (*cycles)[editIndex].end)
            return;

        if (! editStarted)
        {
            cycles->beginEdit();
            editStarted = true;
        }
        cycles->setRangeLive (editIndex, ns, ne);
        return;
    }

    if (dragMode != DragMode::Select)
        return;

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

    const auto mode = dragMode;
    dragMode = DragMode::None;

    if (audio == nullptr || e.mods.isPopupMenu())
        return;

    if (mode == DragMode::EdgeStart || mode == DragMode::EdgeEnd || mode == DragMode::Move)
    {
        if (editStarted)
        {
            const auto idx = cycles->finishEdit (editIndex);
            if (onCycleEdited)
                onCycleEdited (idx);
        }
        editStarted = false;
        return;
    }

    if (mode != DragMode::Select)
        return;

    if (justDoubleClicked)
    {
        justDoubleClicked = false;
        dragging = false;
        return;
    }

    if (! dragging)
    {
        const auto s = xToSample (e.position.x);
        view.clearSelection();
        view.setCursor (s);
        if (onCursorClicked)
            onCursorClicked (s);

        const auto idx = cycles != nullptr ? cycles->findAt (s) : -1;
        if (idx >= 0 && onCycleClicked)
            onCycleClicked (idx);
    }
    else if (view.hasSelection())
    {
        view.setCursor (view.getSelectionStart());
    }

    dragging = false;
}

void WaveView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (audio == nullptr || cycles == nullptr || zoneAt (e.position) != Zone::Wave)
        return;

    const auto hit = hitTestCycle (e.position);
    const auto idx = hit.index >= 0 ? hit.index : cycles->findAt (xToSample (e.position.x));
    if (idx < 0)
        return;

    justDoubleClicked = true;
    if (onCycleDoubleClicked)
        onCycleDoubleClicked (idx);
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
    if (audio == nullptr || wheelMap == nullptr)
        return;

    const auto zone = zoneAt (e.position);
    if (zone == Zone::Other)
        return;

    const auto ctx = zone == Zone::FreqAxis ? WheelContext::FreqAxis : zone == Zone::DbAxis ? WheelContext::DbAxis : WheelContext::Wave;
    const auto action = wheelMap->get (ctx, WheelMap::modKeyFor (e.mods));
    const auto dy = (double) (wheel.deltaY != 0.0f ? wheel.deltaY : wheel.deltaX);
    const auto lanesArea = getLanesArea();
    const auto px = juce::jlimit (0.0, (double) lanesArea.getWidth(), (double) (e.position.x - (float) lanesArea.getX()));

    switch (action)
    {
        case WheelAction::TimeZoom:   view.zoomAround (px, std::pow (0.5, dy * 4.0)); break;
        case WheelAction::TimeScroll: view.scrollPixels (-dy * lanesArea.getWidth() * 0.5); break;
        case WheelAction::FreqZoom:
        {
            const auto lane = getLaneBounds (laneIndexAt (e.position.y));
            view.zoomFreqAround (yToNorm (lane, juce::jlimit ((float) lane.getY(), (float) lane.getBottom(), e.position.y)), std::pow (0.5, dy * 4.0));
            break;
        }
        case WheelAction::FreqScroll: view.scrollFreq (dy * (view.getFreqHi() - view.getFreqLo()) * 0.5); break;
        case WheelAction::GainZoom:   view.zoomGain (std::pow (2.0, dy * 4.0)); break;
        default: break;
    }
}

void WaveView::mouseMagnify (const juce::MouseEvent& e, float scaleFactor)
{
    if (audio != nullptr && scaleFactor > 0.0f)
        view.zoomAround ((double) (e.position.x - (float) getLanesArea().getX()), 1.0 / scaleFactor);
}
} // namespace wt
