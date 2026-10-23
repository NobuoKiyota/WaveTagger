#include "MainComponent.h"
#include "Core/Exporter.h"
#include "Core/Categories.h"
#include "Core/Persistence.h"
#include "Core/WavMarkers.h"
#include "Theme.h"
#include "Util/Text.h"

namespace wt
{
namespace
{
constexpr int toolbarHeight = 38;
constexpr int overviewHeight = 40;
constexpr int listHeight = 200;
constexpr int transportHeight = 62;
} // namespace

MainComponent::MainComponent()
{
    formats.registerBasicFormats();
    setWantsKeyboardFocus (true);

    for (auto* b : { &openButton, &audioButton, &importButton, &splitButton, &exportButton, &libraryButton, &inputButton })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }
    autoPreviewToggle.setWantsKeyboardFocus (false);
    autoPreviewToggle.setToggleState (true, juce::dontSendNotification);
    autoPreviewToggle.onClick = [this] { scheduleSave(); };
    addAndMakeVisible (autoPreviewToggle);
    continueToggle.setWantsKeyboardFocus (false);
    continueToggle.setToggleState (true, juce::dontSendNotification);
    continueToggle.onClick = [this] { lastInfoKey = {}; refreshLibraryInfo(); scheduleSave(); };
    addAndMakeVisible (continueToggle);

    openButton.onClick = [this] { showOpenDialog(); };
    audioButton.onClick = [this] { showAudioSettings(); };
    importButton.onClick = [this] {
        if (audio == nullptr)
            return;
        const auto n = importMarkers();
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Import markers",
                                                n > 0 ? juce::String (n) + WT_U8 (" 件のマーカーを読み込みました (Ctrl+Z で元に戻せます)")
                                                      : WT_U8 ("この WAV にはマーカー(cue)が見つかりませんでした"));
    };
    splitButton.onClick = [this] { if (audio != nullptr) showAutoSplitDialog(); };
    exportButton.onClick = [this] { if (audio != nullptr) startExport(); };
    libraryButton.onClick = [this] { showLibrary(); };
    inputButton.onClick = [this] { showInputSettings(); };

    const auto schemes = theme::getSpectrogramSchemeNames();
    for (int i = 0; i < schemes.size(); ++i)
        schemeCombo.addItem (schemes[i], i + 1);
    schemeCombo.setSelectedId (1, juce::dontSendNotification);
    schemeCombo.setWantsKeyboardFocus (false);
    schemeCombo.onChange = [this] {
        waveView.setColourScheme (schemeCombo.getSelectedId() - 1);
        scheduleSave();
        grabKeyboardFocus();
    };
    addAndMakeVisible (schemeCombo);

    hintLabel.setColour (juce::Label::textColourId, theme::textDim);
    hintLabel.setFont (juce::FontOptions (12.5f));
    hintLabel.setMinimumHorizontalScale (0.8f);
    addAndMakeVisible (hintLabel);

    fileLabel.setColour (juce::Label::textColourId, theme::text);
    fileLabel.setFont (juce::FontOptions (14.0f));
    addAndMakeVisible (fileLabel);

    blendLabel.setText ("Spec / Wave", juce::dontSendNotification);
    floorLabel.setText ("Floor dB", juce::dontSendNotification);
    for (auto* l : { &blendLabel, &floorLabel })
    {
        l->setColour (juce::Label::textColourId, theme::textDim);
        l->setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (l);
    }

    blendSlider.setRange (0.0, 1.0);
    blendSlider.setValue (0.5, juce::dontSendNotification);
    blendSlider.onValueChange = [this] { waveView.setBlend ((float) blendSlider.getValue()); };

    floorSlider.setRange (-160.0, -50.0, 1.0);
    floorSlider.setValue (-110.0, juce::dontSendNotification);
    floorSlider.onValueChange = [this] { waveView.setFloorDb ((float) floorSlider.getValue()); };

    for (auto* s : { &blendSlider, &floorSlider })
    {
        s->setSliderStyle (juce::Slider::LinearHorizontal);
        s->setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        s->setWantsKeyboardFocus (false);
        addAndMakeVisible (s);
    }

    addAndMakeVisible (palette);
    addAndMakeVisible (overview);
    addAndMakeVisible (waveView);
    addAndMakeVisible (list);
    addAndMakeVisible (transport);

    waveView.setCycleModel (&cycles);
    waveView.setWheelMap (&wheelMap);
    spectrogram.onNewImage = [this] { waveView.repaint(); };

    palette.onSlotClicked = [this] (int slot) { handleTypeKey (slot); grabKeyboardFocus(); };
    palette.onEditingFinished = [this] { grabKeyboardFocus(); };

    waveView.onCursorClicked = [this] (juce::int64 s) {
        if (playback.isPlaying())
            playback.play (s, -1, transport.isLooping());
    };
    waveView.onCycleClicked = [this] (int index) {
        list.selectRow (index);
        waveView.setSelectedCycles ({ index });
        if (autoPreviewToggle.getToggleState())
            previewCycle (index);
    };
    waveView.onCycleEdited = [this] (int index) {
        list.selectRow (index);
        waveView.setSelectedCycles ({ index });
        if (autoPreviewToggle.getToggleState())
            previewCycle (index);
    };
    waveView.onCycleDoubleClicked = [this] (int index) {
        if (index < 0 || index >= cycles.size())
            return;
        editingCycle = index;
        view.setSelection (cycles[index].start, cycles[index].end);
        list.selectRow (index);
        waveView.setSelectedCycles ({ index });
        updateHint();
    };
    waveView.onCycleMenuChoice = [this] (int index, int type) {
        auto rows = list.getSelectedRows();
        if (! rows.contains (index))
            rows = { index };

        if (type == -2)
        {
            cycles.remove (rows);
            list.selectRows ({});
            waveView.setSelectedCycles ({});
        }
        else
        {
            applyType (rows, type);
        }
    };

    list.setSampleRate (48000.0);
    list.onPreview = [this] (int row) { if (autoPreviewToggle.getToggleState()) previewCycle (row); };
    list.onSelectionChanged = [this] { waveView.setSelectedCycles (list.getSelectedRows()); };
    list.onTypeChosen = [this] (const juce::Array<int>& rows, int type) { applyType (rows, type); };
    list.onDeleteKey = [this] { deleteSelectedCycles(); };
    list.onReturnKey = [this] (int row) {
        if (view.hasSelection())
        {
            editingCycle = row;
            confirmReselect (-2);
        }
    };

    transport.onToStart = [this] { view.clearSelection(); view.setCursor (0); view.ensureVisible (0); };
    transport.onPlay = [this] { if (! playback.isPlaying()) togglePlay(); };
    transport.onStop = [this] { stopPlayback(); };
    transport.onLoopChanged = [this] (bool loop) { playback.setLooping (loop); };

    loadSettings();

    view.addChangeListener (this);
    cycles.addChangeListener (this);
    libraryIndex.addChangeListener (this);
    catalog.addChangeListener (this);
    refreshLibraryInfo();
    updateHint();
    vblank = std::make_unique<juce::VBlankAttachment> (this, [this] { onVBlank(); });

    setSize (1400, 900);
}

MainComponent::~MainComponent()
{
    saveNow();
    vblank = nullptr;
    view.removeChangeListener (this);
    cycles.removeChangeListener (this);
    libraryIndex.removeChangeListener (this);
    catalog.removeChangeListener (this);
    libraryWindow = nullptr;
    loader.removeAllJobs (true, 10000);
    spectrogram.onNewImage = nullptr;
    waveView.setCycleModel (nullptr);
}

//==============================================================================
void MainComponent::loadSettings()
{
    const auto s = readJson (getSettingsFile());

    // Index roots: legacy library is read-only; the new library is where exports go.
    juce::StringArray roots;
    if (const auto* arr = s.getProperty ("libraryRoots", {}).getArray())
        for (const auto& r : *arr)
            roots.add (r.toString());
    if (roots.isEmpty())
        for (const char* def : { "G:\\SE", "G:\\SFX_Library" })
            if (juce::File (def).isDirectory())
                roots.add (def);
    libraryIndex.setRoots (roots);

    // Category / Type CSV folder: remembered once chosen so it never silently moves
    const auto savedDir = s.getProperty ("configDir", "").toString();
    catalog.setDirectory (savedDir.isNotEmpty() ? juce::File (savedDir) : Catalog::defaultDirectory());

    if (! s.isObject())
        return;

    catalog.usageFromVar (s.getProperty ("categoryUsage", {}));
    keyMap.fromVar (s.getProperty ("keys", {}));

    cycles.settingsFromVar (s.getProperty ("cycle", {}));
    exportDir = juce::File (s.getProperty ("exportDir", "").toString());
    autoPreviewToggle.setToggleState ((bool) s.getProperty ("autoPreview", true), juce::dontSendNotification);
    continueToggle.setToggleState ((bool) s.getProperty ("continueNumbering", true), juce::dontSendNotification);
    wheelMap.fromVar (s.getProperty ("wheel", {}));

    const auto scheme = juce::jlimit (0, schemeCombo.getNumItems() - 1, (int) s.getProperty ("scheme", 0));
    schemeCombo.setSelectedId (scheme + 1, juce::dontSendNotification);
    waveView.setColourScheme (scheme);
}

void MainComponent::scheduleSave()
{
    const auto generation = ++saveGeneration;
    juce::Component::SafePointer<MainComponent> safe (this);
    juce::Timer::callAfterDelay (700, [safe, generation] {
        if (safe != nullptr && safe->saveGeneration == generation)
            safe->saveNow();
    });
}

void MainComponent::saveNow()
{
    if (snapshotOutput != juce::File())
        return;

    auto* settings = new juce::DynamicObject();
    settings->setProperty ("cycle", cycles.settingsToVar());
    settings->setProperty ("exportDir", exportDir.getFullPathName());
    settings->setProperty ("autoPreview", autoPreviewToggle.getToggleState());
    settings->setProperty ("continueNumbering", continueToggle.getToggleState());
    settings->setProperty ("wheel", wheelMap.toVar());
    settings->setProperty ("keys", keyMap.toVar());
    settings->setProperty ("configDir", catalog.getDirectory().getFullPathName());
    settings->setProperty ("categoryUsage", catalog.usageToVar());
    settings->setProperty ("scheme", schemeCombo.getSelectedId() - 1);

    juce::Array<juce::var> rootArray;
    for (const auto& r : libraryIndex.getRoots())
        rootArray.add (r);
    settings->setProperty ("libraryRoots", rootArray);
    writeJson (getSettingsFile(), juce::var (settings));

    if (audio != nullptr)
    {
        auto* project = new juce::DynamicObject();
        project->setProperty ("audio", audio->file.getFullPathName());
        project->setProperty ("sampleRate", audio->sampleRate);
        project->setProperty ("cycles", cycles.cyclesToVar());
        writeJson (getProjectFileFor (audio->file), juce::var (project));
    }
}

//==============================================================================
void MainComponent::openFile (const juce::File& file)
{
    stopPlayback();
    saveNow();
    statusText = WT_U8 ("読み込み中... ") + file.getFileName();
    fileLabel.setText (statusText, juce::dontSendNotification);

    juce::Component::SafePointer<MainComponent> safe (this);
    loader.addJob ([this, safe, file] {
        juce::String error;
        auto data = loadAudioFile (formats, file, error);
        juce::MessageManager::callAsync ([safe, data, error] {
            if (safe != nullptr)
                safe->audioLoaded (data, error);
        });
    });
}

void MainComponent::audioLoaded (AudioDataPtr data, const juce::String& error)
{
    if (data == nullptr)
    {
        fileLabel.setText (error, juce::dontSendNotification);
        if (snapshotOutput != juce::File())
        {
            std::fprintf (stderr, "%s\n", error.toRawUTF8());
            juce::JUCEApplication::getInstance()->setApplicationReturnValue (1);
            juce::JUCEApplication::quit();
        }
        return;
    }

    audio = std::move (data);
    playback.setAudio (audio);
    waveView.setAudio (audio);
    overview.setAudio (audio);
    view.reset (audio->getNumSamples(), audio->sampleRate);
    list.setSampleRate (audio->sampleRate);
    editingCycle = -1;
    updateHint();
    list.selectRows ({});
    waveView.setSelectedCycles ({});

    auto label = audio->file.getFullPathName();

    const auto project = readJson (getProjectFileFor (audio->file));
    if (project.isObject() && project.hasProperty ("cycles") && snapshotOutput == juce::File())
    {
        cycles.cyclesFromVar (project.getProperty ("cycles", {}));
    }
    else
    {
        cycles.replaceAll ({}, false);
        const auto n = snapshotOutput == juce::File() ? importMarkers() : 0;
        if (n > 0)
            label += WT_U8 ("   (WAV 内のマーカー ") + juce::String (n) + WT_U8 (" 件を読み込み)");
    }

    fileLabel.setText (label, juce::dontSendNotification);
    if (onTitleChanged)
        onTitleChanged ("WaveTagger - " + audio->file.getFileName());

    refreshTransport();
    grabKeyboardFocus();

    if (snapshotOutput != juce::File())
    {
        // demo: auto-split the whole file and hand out types round-robin so the UI can be checked
        runAutoSplit ({});
        std::vector<Cycle> typed;
        for (int i = 0; i < cycles.size(); ++i)
            typed.push_back ({ cycles[i].start, cycles[i].end, i % 3 });
        cycles.replaceAll (std::move (typed), false);

        if (juce::SystemStats::getEnvironmentVariable ("WT_DEMO_ZOOM", {}).isNotEmpty())
        {
            view.zoomFreqAround (0.45, 0.4);
            view.zoomGain (4.0);
        }

        const auto row = juce::jmin (3, cycles.size() - 1);
        if (row >= 0)
        {
            list.selectRow (row);
            waveView.setSelectedCycles ({ row });
        }

        juce::Component::SafePointer<MainComponent> safe (this);
        juce::Timer::callAfterDelay (2500, [safe] { if (safe != nullptr) safe->takeSnapshot(); });
    }
}

void MainComponent::runSnapshot (const juce::File& input, const juce::File& output)
{
    snapshotOutput = output;
    openFile (input);
}

void MainComponent::takeSnapshot()
{
    // wait (up to 3 minutes) for a running library scan so the snapshot shows the finished index
    if (libraryIndex.isScanning() && ++snapshotWaits < 360)
    {
        juce::Component::SafePointer<MainComponent> safe (this);
        juce::Timer::callAfterDelay (500, [safe] { if (safe != nullptr) safe->takeSnapshot(); });
        return;
    }

    refreshLibraryInfo();
    const auto image = createComponentSnapshot (getLocalBounds());
    snapshotOutput.deleteFile();
    juce::FileOutputStream stream (snapshotOutput);
    juce::PNGImageFormat png;
    const auto ok = stream.openedOk() && png.writeImageToStream (image, stream);
    juce::JUCEApplication::getInstance()->setApplicationReturnValue (ok ? 0 : 1);
    juce::JUCEApplication::quit();
}

//==============================================================================
void MainComponent::togglePlay()
{
    if (audio == nullptr)
        return;

    if (playback.isPlaying())
    {
        stopPlayback();
        return;
    }

    if (view.hasSelection())
    {
        playback.play (view.getSelectionStart(), view.getSelectionEnd(), transport.isLooping());
        return;
    }

    // Space on a previewed cycle replays exactly that cycle
    const auto rows = list.getSelectedRows();
    if (rows.size() == 1 && rows[0] < cycles.size() && cycles[rows[0]].start == view.getCursor())
        playback.play (cycles[rows[0]].start, cycles[rows[0]].end, transport.isLooping());
    else
        playback.play (view.getCursor(), -1, transport.isLooping());
}

void MainComponent::stopPlayback()
{
    playback.stop();
}

void MainComponent::onVBlank()
{
    if (++frameCounter % 120 == 0)
        catalog.reloadIfChanged();

    const auto playing = playback.isPlaying();

    if (playing)
    {
        const auto pos = playback.getPosition();
        waveView.setPlayhead (pos);
        overview.setPlayhead (pos);

        if ((double) pos > view.getViewEnd() || (double) pos < view.getViewStart())
            view.setView ((double) pos - 0.02 * (view.getViewEnd() - view.getViewStart()), view.getSamplesPerPixel());

        refreshTransport();
    }
    else if (wasPlaying)
    {
        waveView.setPlayhead (-1);
        overview.setPlayhead (-1);
        refreshTransport();
    }

    wasPlaying = playing;
}

void MainComponent::refreshTransport()
{
    transport.update (view, audio, playback.isPlaying() ? playback.getPosition() : -1, playback.isPlaying());
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &cycles)
    {
        scheduleSave();
        waveView.setSelectedCycles (list.getSelectedRows());
        refreshLibraryInfo();
    }
    else if (source == &libraryIndex)
    {
        refreshLibraryInfo();
    }
    else if (source == &catalog)
    {
        scheduleSave();
    }
    else
    {
        refreshTransport();
    }
}

//==============================================================================
void MainComponent::handleTypeKey (int slot)
{
    if (audio == nullptr || ! cycles.isTypeUsable (slot))
        return;

    if (editingCycle >= 0 && view.hasSelection())
    {
        confirmReselect (slot);
        return;
    }

    if (view.hasSelection())
    {
        addCycleFromSelection (slot);
        return;
    }

    const auto rows = list.getSelectedRows();
    if (! rows.isEmpty())
        applyType (rows, slot);
}

void MainComponent::addCycleFromSelection (int slot)
{
    const auto start = view.getSelectionStart(), end = view.getSelectionEnd();
    if ((double) (end - start) < audio->sampleRate * 0.001)
        return;

    const auto index = cycles.add (start, end, slot);
    view.clearSelection();
    list.selectRow (index);
    waveView.setSelectedCycles ({ index });

    if (autoPreviewToggle.getToggleState())
        previewCycle (index);
}

void MainComponent::applyType (const juce::Array<int>& rows, int type)
{
    cycles.setType (rows, type);
}

bool MainComponent::confirmReselect (int type)
{
    if (editingCycle < 0 || editingCycle >= cycles.size() || ! view.hasSelection())
        return false;

    const auto start = view.getSelectionStart(), end = view.getSelectionEnd();
    if ((double) (end - start) < audio->sampleRate * 0.001)
        return false;

    const auto newType = type == -2 ? cycles[editingCycle].type : type;
    const auto index = cycles.update (editingCycle, start, end, newType);

    editingCycle = -1;
    view.clearSelection();
    list.selectRow (index);
    waveView.setSelectedCycles ({ index });
    updateHint();

    if (autoPreviewToggle.getToggleState())
        previewCycle (index);
    return true;
}

void MainComponent::updateHint()
{
    if (editingCycle >= 0 && editingCycle < cycles.size())
    {
        hintLabel.setText (WT_U8 ("再選択中: ") + cycles.nameOf (editingCycle)
                               + WT_U8 ("  —  範囲を選び直して Enter(タイプそのまま)か数字キー(タイプも変更)で確定 / Esc で取消"),
                           juce::dontSendNotification);
        hintLabel.setColour (juce::Label::textColourId, juce::Colour (0xffffc533));
        return;
    }

    juce::StringArray parts;
    const char* mods[] = { "", "Shift+", "Alt+", "Ctrl+" };
    for (int m = 0; m < (int) ModKey::Count; ++m)
    {
        const auto a = wheelMap.get (WheelContext::Wave, (ModKey) m);
        if (a != WheelAction::None)
            parts.add (juce::String (mods[m]) + WT_U8 ("ホイール: ") + WheelMap::actionName (a));
    }
    if (wheelMap.get (WheelContext::DbAxis, ModKey::None) != WheelAction::None)
        parts.add (WT_U8 ("dB 目盛り上: ") + WheelMap::actionName (wheelMap.get (WheelContext::DbAxis, ModKey::None)));

    hintLabel.setText (parts.joinIntoString ("  |  ") + WT_U8 ("   ||   サイクルの端ドラッグ: 境界調整  Alt+ドラッグ: 移動  ダブルクリック: 範囲の再選択"),
                       juce::dontSendNotification);
    hintLabel.setColour (juce::Label::textColourId, theme::textDim);
}

void MainComponent::showInputSettings()
{
    auto* content = new InputSettingsComponent (wheelMap, keyMap, [this] {
        updateHint();
        scheduleSave();
    });

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (content);
    options.dialogTitle = WT_U8 ("入力設定 (マウス / キーボード)");
    options.dialogBackgroundColour = theme::panel;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();
}

void MainComponent::previewCycle (int index)
{
    if (audio == nullptr || index < 0 || index >= cycles.size())
        return;

    const auto& c = cycles[index];
    view.setCursor (c.start);
    view.ensureVisible ((double) c.start);
    playback.play (c.start, c.end, false);
}

void MainComponent::deleteSelectedCycles()
{
    const auto rows = list.getSelectedRows();
    if (rows.isEmpty())
        return;

    cycles.remove (rows);
    list.selectRows ({});
    waveView.setSelectedCycles ({});
}

void MainComponent::showAutoSplitDialog()
{
    auto* w = new juce::AlertWindow (WT_U8 ("自動分割"),
                                     WT_U8 ("音のある区間を検出してサイクル(未分類)にします。\n既存のサイクルと重なる区間は追加しません。"),
                                     juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("above", "12", WT_U8 ("ノイズ床からの上乗せ (dB)"));
    w->addTextEditor ("gap", "120", WT_U8 ("この長さ未満の無音はつなぐ (ms)"));
    w->addTextEditor ("len", "25", WT_U8 ("この長さ未満の音は捨てる (ms)"));
    w->addTextEditor ("pad", "40", WT_U8 ("前後の余白 (ms)"));
    w->addButton (WT_U8 ("実行"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton (WT_U8 ("キャンセル"), 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<MainComponent> safe (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int result) {
        if (result != 1 || safe == nullptr)
            return;

        AutoSplitParams p;
        p.aboveNoiseDb = w->getTextEditorContents ("above").getFloatValue();
        p.minGapMs = w->getTextEditorContents ("gap").getFloatValue();
        p.minLengthMs = w->getTextEditorContents ("len").getFloatValue();
        p.padMs = w->getTextEditorContents ("pad").getFloatValue();
        safe->runAutoSplit (p);
    }), true);
}

void MainComponent::runAutoSplit (const AutoSplitParams& params)
{
    if (audio == nullptr)
        return;

    const auto from = view.hasSelection() ? view.getSelectionStart() : 0;
    const auto to = view.hasSelection() ? view.getSelectionEnd() : audio->getNumSamples();

    std::vector<Cycle> all;
    for (int i = 0; i < cycles.size(); ++i)
        all.push_back (cycles[i]);

    for (const auto& [s, e] : detectSegments (*audio, from, to, params))
        if (! cycles.overlapsAny (s, e))
            all.push_back ({ s, e, -1 });

    cycles.replaceAll (std::move (all));
    view.clearSelection();
}

int MainComponent::importMarkers()
{
    const auto markers = readWavMarkers (audio->file);
    if (markers.empty())
        return 0;

    std::vector<Cycle> found;
    const auto typeFor = [this] (const juce::String& label) {
        int best = -1, bestLen = 0;
        for (int i = 0; i < CycleModel::numSlots; ++i)
            if (cycles.isTypeUsable (i) && cycles.getType (i).name.length() > bestLen
                && label.containsIgnoreCase (cycles.getType (i).name))
            {
                best = i;
                bestLen = cycles.getType (i).name.length();
            }
        return best;
    };

    const auto hasRegions = std::any_of (markers.begin(), markers.end(), [] (const RawMarker& m) { return m.length > 0; });
    for (size_t i = 0; i < markers.size(); ++i)
    {
        const auto& m = markers[i];
        juce::int64 end;
        if (hasRegions)
        {
            if (m.length <= 0)
                continue;
            end = m.start + m.length;
        }
        else
        {
            end = i + 1 < markers.size() ? markers[i + 1].start : audio->getNumSamples();
        }

        if (end > m.start)
            found.push_back ({ m.start, juce::jmin (end, audio->getNumSamples()), typeFor (m.label) });
    }

    const auto count = (int) found.size();
    if (count > 0)
        cycles.replaceAll (std::move (found));
    return count;
}

void MainComponent::startExport()
{
    std::vector<ExportItem> items;
    int untyped = 0;

    const auto folder = juce::File::createLegalFileName (cycles.getPrefix().trim());
    for (int i = 0; i < cycles.size(); ++i)
    {
        const auto& c = cycles[i];
        if (c.type < 0)
        {
            ++untyped;
            continue;
        }

        ExportItem item;
        item.start = c.start;
        item.end = c.end;
        item.subFolder = cycles.getCategory().isNotEmpty() ? cycles.getCategory() + "/" + folder : folder;
        item.fileName = cycles.nameOf (i);
        item.comment = "WaveTagger type=" + cycles.typeNameOf (c.type) + "; no=" + juce::String (cycles.numberOf (i))
                       + "; src=" + audio->file.getFileName();
        items.push_back (item);
    }

    if (items.empty())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Export",
                                                WT_U8 ("書き出せるサイクルがありません。タイプを割り当ててください。"));
        return;
    }

    auto start = exportDir;
    if (! start.isDirectory())
        start = juce::File ("G:\\SFX_Library\\Library").isDirectory() ? juce::File ("G:\\SFX_Library\\Library")
                                                                       : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);

    chooser = std::make_unique<juce::FileChooser> (WT_U8 ("書き出し先フォルダを選択 (中に \"") + folder + WT_U8 ("\" フォルダを作ります)"), start);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this, items = std::move (items), untyped] (const juce::FileChooser& fc) {
                              const auto root = fc.getResult();
                              if (! root.isDirectory())
                                  return;

                              exportDir = root;
                              scheduleSave();

                              juce::Component::SafePointer<MainComponent> safe (this);
                              auto data = audio;
                              const auto dest = root.getChildFile (items.front().subFolder);
                              loader.addJob ([safe, data, items, untyped, root, dest] {
                                  const auto result = exportCycles (*data, items, root);
                                  juce::MessageManager::callAsync ([safe, result, untyped, root, dest] {
                                      if (safe != nullptr && result.written > 0)
                                          safe->afterExport (dest);
                                      juce::String msg = juce::String (result.written) + WT_U8 (" 件を書き出しました\n") + root.getFullPathName();
                                      if (untyped > 0)
                                          msg += "\n\n" + juce::String (untyped) + WT_U8 (" 件は未分類のためスキップしました");
                                      if (result.skippedExisting.size() > 0)
                                          msg += "\n\n" + juce::String (result.skippedExisting.size())
                                                 + WT_U8 (" 件は同名ファイルが既にあるため上書きせずスキップしました");
                                      if (result.errors.size() > 0)
                                          msg += "\n\n" + juce::String (result.errors.size()) + WT_U8 (" 件は書き出しに失敗しました");
                                      juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Export", msg);
                                  });
                              });
                          });
}

//==============================================================================
void MainComponent::showLibrary()
{
    if (libraryWindow == nullptr)
    {
        libraryWindow = std::make_unique<LibraryWindow> (libraryIndex);
        libraryWindow->getPanel().onRootsChanged = [this] (const juce::StringArray& roots) { setLibraryRoots (roots); };
    }
    libraryWindow->setVisible (true);
    libraryWindow->toFront (true);
}

void MainComponent::setLibraryRoots (const juce::StringArray& roots)
{
    libraryIndex.setRoots (roots);
    libraryIndex.rescan();
    scheduleSave();
}

void MainComponent::afterExport (const juce::File& destFolder)
{
    auto roots = libraryIndex.getRoots();
    bool covered = false;
    for (const auto& r : roots)
        covered = covered || destFolder.getFullPathName().startsWithIgnoreCase (r);

    if (covered)
    {
        libraryIndex.refreshFolder (destFolder);
    }
    else
    {
        roots.add (destFolder.getParentDirectory().getFullPathName());
        setLibraryRoots (roots);
    }
}

void MainComponent::refreshLibraryInfo()
{
    const auto data = libraryIndex.getData();

    std::array<juce::String, CycleModel::numSlots> stems;
    juce::StringArray ownKeys;
    juce::String key = juce::String (data->generation) + (continueToggle.getToggleState() ? "c" : "n");
    for (int i = 0; i < CycleModel::numSlots; ++i)
        if (cycles.isTypeUsable (i) && cycles.getNameStem (i, stems[(size_t) i]))
        {
            ownKeys.add (makeKey (stems[(size_t) i]));
            key += "|" + stems[(size_t) i];
        }

    if (key == lastInfoKey)
        return;
    lastInfoKey = key;

    std::array<int, CycleModel::numSlots> offsets {};
    juce::StringArray summary, warnings;
    for (int i = 0; i < CycleModel::numSlots; ++i)
    {
        const auto& stem = stems[(size_t) i];
        if (stem.isEmpty())
            continue;

        const auto k = makeKey (stem);
        if (const auto* b = data->find (k))
        {
            summary.add (cycles.getType (i).name + " " + juce::String (b->count));
            if (continueToggle.getToggleState())
                offsets[(size_t) i] = b->maxNumber;
        }
        else
        {
            const auto similar = data->findSimilar (k, ownKeys, 2);
            if (similar.size() > 0)
                warnings.add (stem.trimCharactersAtEnd ("_- ") + " ≈ " + similar.joinIntoString (", "));
        }
    }

    cycles.setNumberOffsets (offsets);
    palette.setLibraryInfo (summary.isEmpty() ? WT_U8 ("既存: なし") : WT_U8 ("既存: ") + summary.joinIntoString (" / "),
                            warnings.isEmpty() ? juce::String() : WT_U8 ("⚠ 似た名前: ") + warnings.joinIntoString ("   "));
}

//==============================================================================
void MainComponent::showOpenDialog()
{
    chooser = std::make_unique<juce::FileChooser> (WT_U8 ("音声ファイルを開く"), juce::File(), formats.getWildcardForAllFormats());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc) {
                              const auto file = fc.getResult();
                              if (file.existsAsFile())
                                  openFile (file);
                          });
}

void MainComponent::showAudioSettings()
{
    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent> (playback.getDeviceManager(), 0, 0, 1, 2,
                                                                          false, false, true, false);
    selector->setSize (520, 360);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (selector.release());
    options.dialogTitle = "Audio Settings";
    options.dialogBackgroundColour = theme::panel;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
    g.setColour (theme::panel);
    g.fillRect (getLocalBounds().removeFromTop (toolbarHeight));
}

void MainComponent::resized()
{
    auto r = getLocalBounds();

    auto bar = r.removeFromTop (toolbarHeight).reduced (8, 5);
    openButton.setBounds (bar.removeFromLeft (56));
    bar.removeFromLeft (6);
    audioButton.setBounds (bar.removeFromLeft (66));
    bar.removeFromLeft (12);
    importButton.setBounds (bar.removeFromLeft (104));
    bar.removeFromLeft (6);
    splitButton.setBounds (bar.removeFromLeft (76));
    bar.removeFromLeft (6);
    exportButton.setBounds (bar.removeFromLeft (76));
    bar.removeFromLeft (6);
    libraryButton.setBounds (bar.removeFromLeft (66));
    bar.removeFromLeft (12);
    autoPreviewToggle.setBounds (bar.removeFromLeft (108));
    continueToggle.setBounds (bar.removeFromLeft (146));
    bar.removeFromLeft (8);

    inputButton.setBounds (bar.removeFromRight (74));
    bar.removeFromRight (6);
    schemeCombo.setBounds (bar.removeFromRight (104));
    bar.removeFromRight (10);
    floorSlider.setBounds (bar.removeFromRight (90));
    floorLabel.setBounds (bar.removeFromRight (56));
    bar.removeFromRight (8);
    blendSlider.setBounds (bar.removeFromRight (90));
    blendLabel.setBounds (bar.removeFromRight (82));
    fileLabel.setBounds (bar);

    palette.setBounds (r.removeFromTop (PalettePanel::height));
    transport.setBounds (r.removeFromBottom (transportHeight));
    list.setBounds (r.removeFromBottom (listHeight));
    hintLabel.setBounds (r.removeFromBottom (22).reduced (8, 0));
    overview.setBounds (r.removeFromTop (overviewHeight)
                            .withTrimmedLeft (WaveView::leftStrip)
                            .withTrimmedRight (WaveView::rightAxis));
    r.removeFromTop (4);
    waveView.setBounds (r);
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    const auto action = keyMap.find (key);
    if (action == Action::None)
        return false;

    if (action == Action::Open)
    {
        showOpenDialog();
        return true;
    }

    if (action == Action::Library)
    {
        showLibrary();
        return true;
    }

    if (audio == nullptr)
        return false;

    const auto centrePx = 0.5 * view.getWidth();

    switch (action)
    {
        case Action::Export:        startExport(); break;
        case Action::SelectAll:     view.setSelection (0, audio->getNumSamples()); break;
        case Action::Undo:          cycles.undo(); break;
        case Action::Redo:          cycles.redo(); break;
        case Action::PlayStop:      togglePlay(); break;
        case Action::Confirm:
            if (! confirmReselect (-2))
                return false;
            break;
        case Action::Cancel:
            editingCycle = -1;
            updateHint();
            view.clearSelection();
            break;
        case Action::Delete:        deleteSelectedCycles(); break;
        case Action::ToStart:       view.setCursor (0); view.ensureVisible (0); break;
        case Action::ToEnd:         view.setCursor (audio->getNumSamples()); view.ensureVisible ((double) audio->getNumSamples()); break;
        case Action::ZoomFit:       view.zoomToFit(); break;
        case Action::ZoomSelection:
            if (view.hasSelection())
                view.zoomToRange (view.getSelectionStart(), view.getSelectionEnd());
            break;
        case Action::ToggleLoop:
            transport.setLooping (! transport.isLooping());
            playback.setLooping (transport.isLooping());
            break;
        case Action::ZoomIn:        view.zoomAround (centrePx, 0.5); break;
        case Action::ZoomOut:       view.zoomAround (centrePx, 2.0); break;
        case Action::ScrollLeft:    view.scrollPixels (-view.getWidth() * 0.1); break;
        case Action::ScrollRight:   view.scrollPixels (view.getWidth() * 0.1); break;
        case Action::AutoSplit:     showAutoSplitDialog(); break;
        case Action::ImportMarkers: importButton.onClick(); break;
        default:
            if (action >= Action::Type1 && action <= Action::Type9)
                handleTypeKey ((int) action - (int) Action::Type1);
            else
                return false;
            break;
    }

    return true;
}

bool MainComponent::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (formats.findFormatForFileExtension (juce::File (f).getFileExtension()) != nullptr)
            return true;
    return false;
}

void MainComponent::filesDropped (const juce::StringArray& files, int, int)
{
    for (const auto& f : files)
    {
        const juce::File file (f);
        if (formats.findFormatForFileExtension (file.getFileExtension()) != nullptr)
        {
            openFile (file);
            return;
        }
    }
}
} // namespace wt
