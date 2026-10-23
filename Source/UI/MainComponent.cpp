#include "MainComponent.h"
#include "Core/AudioFeatures.h"
#include "Core/Conditioning.h"
#include "Core/Exporter.h"
#include "Core/WavChunks.h"
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

    for (auto* b : { &openButton, &inboxButton, &importButton, &splitButton, &exportButton, &libraryButton, &inputButton })
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
    inboxButton.onClick = [this] { showInbox(); };
    importButton.setButtonText (juce::String ("Markers ") + WT_U8 ("▾"));
    importButton.onClick = [this] { showMarkersMenu(); };
    splitButton.onClick = [this] { if (audio != nullptr) showAutoSplitDialog(); };
    exportButton.onClick = [this] { if (audio != nullptr) startExport(); };
    libraryButton.onClick = [this] {
        juce::PopupMenu menu;
        menu.addItem (1, WT_U8 ("ライブラリ検索   ") + keyMap.describe (Action::Library));
        menu.addItem (2, WT_U8 ("過去ライブラリの区分し直し..."));

        juce::Component::SafePointer<MainComponent> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&libraryButton), [safe] (int r) {
            if (safe == nullptr)
                return;
            if (r == 1)
                safe->showLibrary();
            else if (r == 2)
                safe->showOrganizer();
        });
    };
    inputButton.setButtonText (WT_U8 ("設定..."));
    inputButton.onClick = [this] { showInputSettings(); };

    conditionButton.setButtonText (WT_U8 ("整音..."));
    conditionButton.setWantsKeyboardFocus (false);
    conditionButton.onClick = [this] { showConditioning(); };
    addAndMakeVisible (conditionButton);
    abToggle.setButtonText (WT_U8 ("B: 整音後で試聴"));
    abToggle.setWantsKeyboardFocus (false);
    abToggle.onClick = [this] { refreshListening(); };
    addAndMakeVisible (abToggle);
    conditionPreview.onChanged = [this] {
        refreshListening();
        if (conditioningWindow != nullptr)
            conditioningWindow->getPanel().updateReadouts();
    };

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

    palette.onCategoryChosen = [this] (const juce::String& name) { chooseCategory (name); };
    palette.onSlotClicked = [this] (int slot) { handleTypeKey (slot); grabKeyboardFocus(); };
    palette.onEditingFinished = [this] { grabKeyboardFocus(); };

    waveView.onCursorClicked = [this] (juce::int64 s) {
        if (playback.isPlaying())
            playRange (s, -1, transport.isLooping());
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

    inboxQueue.load();
    featureStore.load();
    setupLearning();
    inboxWatcher.onOutcome = [this] (const InboxOutcome& o) { handleInboxOutcome (o); };
    configureInbox();
    updateInboxBadge();

    view.addChangeListener (this);
    cycles.addChangeListener (this);
    libraryIndex.addChangeListener (this);
    catalog.addChangeListener (this);
    refreshLibraryInfo();
    updateHint();
    vblank = std::make_unique<juce::VBlankAttachment> (this, [this] { onVBlank(); });

    setSize (1400, 900);

    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this)] {
        if (safe != nullptr)
            safe->offerConfigMigration();
    });
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
    organizerWindow = nullptr;
    conditioningWindow = nullptr;
    conditionPreview.onChanged = nullptr;
    loader.removeAllJobs (true, 10000);
    spectrogram.onNewImage = nullptr;
    waveView.setCycleModel (nullptr);
}

//==============================================================================
void MainComponent::loadSettings()
{
    const auto s = readJson (getSettingsFile()); // this PC only

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

    // Category / Type workbook + shared preferences folder: remembered once chosen so it never silently moves
    const auto savedDir = s.getProperty ("configDir", "").toString();
    catalog.setDirectory (savedDir.isNotEmpty() ? juce::File (savedDir) : Catalog::defaultDirectory());

    exportDir = juce::File (s.getProperty ("exportDir", "").toString());
    rawDir = juce::File (s.getProperty ("rawDir", "").toString());
    inboxDir = juce::File (s.getProperty ("inboxDir", "").toString());
    organizedDir = juce::File (s.getProperty ("organizedDir", "").toString());
    inboxWatch = (bool) s.getProperty ("inboxWatch", false);
    backlogDecidedFor = s.getProperty ("inboxBacklogDecidedFor", "").toString();
    migrationDeclined = (bool) s.getProperty ("configMigrationDeclined", false);

    // Shared settings come from the config folder. The first time, carry over what this PC had in settings.json.
    // If the file exists but cannot be parsed (e.g. Drive is mid-sync) it is left alone rather than overwritten by defaults.
    auto prefs = readJson (catalog.getPreferencesFile());
    if (catalog.getPreferencesFile().existsAsFile() && ! prefs.isObject())
        prefsLoadFailed = true;
    else if (! prefs.isObject())
        prefs = s;

    if (! prefsLoadFailed)
        applySharedPreferences (prefs);
}

void MainComponent::applySharedPreferences (const juce::var& v)
{
    if (! v.isObject())
        return;

    const juce::ScopedValueSetter<bool> guard (applyingProfile, true); // loading must not be mistaken for the user editing
    profiles.fromVar (v.getProperty ("categoryProfiles", {}));
    catalog.usageFromVar (v.getProperty ("categoryUsage", {}));
    keyMap.fromVar (v.getProperty ("keys", {}));
    cycles.settingsFromVar (v.getProperty ("cycle", {}));
    autoPreviewToggle.setToggleState ((bool) v.getProperty ("autoPreview", true), juce::dontSendNotification);
    continueToggle.setToggleState ((bool) v.getProperty ("continueNumbering", true), juce::dontSendNotification);
    wheelMap.fromVar (v.getProperty ("wheel", {}));

    conditioning = conditionSettingsFromVar (v.getProperty ("conditioning", {}));
    conditionPreview.setOptions (conditioning.options);
    if (conditioningWindow != nullptr)
        conditioningWindow->getPanel().setSettings (conditioning);

    const auto scheme = juce::jlimit (0, schemeCombo.getNumItems() - 1, (int) v.getProperty ("scheme", 0));
    schemeCombo.setSelectedId (scheme + 1, juce::dontSendNotification);
    waveView.setColourScheme (scheme);
    updateHint();
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

    // this PC only: where things are on this machine
    auto* local = new juce::DynamicObject();
    local->setProperty ("exportDir", exportDir.getFullPathName());
    local->setProperty ("rawDir", rawDir.getFullPathName());
    local->setProperty ("inboxDir", inboxDir.getFullPathName());
    local->setProperty ("organizedDir", organizedDir.getFullPathName());
    local->setProperty ("inboxWatch", inboxWatch);
    local->setProperty ("inboxBacklogDecidedFor", backlogDecidedFor);
    local->setProperty ("configDir", catalog.getDirectory().getFullPathName());
    local->setProperty ("configMigrationDeclined", migrationDeclined);

    juce::Array<juce::var> rootArray;
    for (const auto& r : libraryIndex.getRoots())
        rootArray.add (r);
    local->setProperty ("libraryRoots", rootArray);
    writeJson (getSettingsFile(), juce::var (local));

    // shared between PCs (lives next to catalog.xlsx)
    if (! prefsLoadFailed)
    {
        auto* shared = new juce::DynamicObject();
        shared->setProperty ("cycle", cycles.settingsToVar());
        shared->setProperty ("autoPreview", autoPreviewToggle.getToggleState());
        shared->setProperty ("continueNumbering", continueToggle.getToggleState());
        shared->setProperty ("wheel", wheelMap.toVar());
        shared->setProperty ("keys", keyMap.toVar());
        shared->setProperty ("categoryUsage", catalog.usageToVar());
        shared->setProperty ("categoryProfiles", profiles.toVar());
        shared->setProperty ("scheme", schemeCombo.getSelectedId() - 1);
        shared->setProperty ("conditioning", conditionSettingsToVar (conditioning));

        const auto prefsFile = catalog.getPreferencesFile();
        if (prefsFile.getParentDirectory().isDirectory())
        {
            if (! prefsBackedUp && prefsFile.existsAsFile())
            {
                prefsFile.copyFileTo (prefsFile.getSiblingFile ("preferences.json.bak-" + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S")));
                prefsBackedUp = true; // once per session
            }
            writeJson (prefsFile, juce::var (shared));
        }
    }

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
    playback.setGain (1.0f);
    abToggle.setToggleState (false, juce::dontSendNotification); // a new recording starts on the original
    abToggle.setButtonText (WT_U8 ("B: 整音後で試聴"));
    conditionPreview.setAudio (audio);
    selectionGainCache = {};
    waveView.setAudio (audio);
    overview.setAudio (audio);
    view.reset (audio->getNumSamples(), audio->sampleRate);
    list.setSampleRate (audio->sampleRate);
    editingCycle = -1;
    featureCache.clear();
    suggestedRanges.clear();
    updateHint();
    list.selectRows ({});
    waveView.setSelectedCycles ({});

    auto label = audio->file.getFullPathName();

    // Where do this file's cycles come from? The WAV itself (wtag chunk) or this PC's project file, whichever was saved later.
    const auto realRun = snapshotOutput == juce::File();
    const auto projectFile = getProjectFileFor (audio->file);
    const auto project = realRun ? readJson (projectFile) : juce::var();
    const auto hasProject = project.isObject() && project.hasProperty ("cycles");

    juce::var wtagJson;
    WtagData embedded;
    const auto hasEmbedded = realRun && readWtag (audio->file, wtagJson) && parseWtag (wtagJson, embedded);

    if (preferEmbedded (hasEmbedded, embedded.savedAt, hasProject, projectFile.getLastModificationTime().toMilliseconds()))
    {
        const auto added = applyEmbeddedState (embedded);
        label += WT_U8 ("   (WAV に埋め込まれた情報を読み込み: ") + juce::String ((int) embedded.cycles.size()) + WT_U8 (" 件")
                 + (added > 0 ? WT_U8 (", Type 名 ") + juce::String (added) + WT_U8 (" 個をパレットに追加") : juce::String()) + ")";
    }
    else if (hasProject)
    {
        cycles.cyclesFromVar (project.getProperty ("cycles", {}));
    }
    else
    {
        cycles.replaceAll ({}, false);
        const auto n = realRun ? importMarkers() : 0;
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
        playRange (view.getSelectionStart(), view.getSelectionEnd(), transport.isLooping());
        return;
    }

    // Space on a previewed cycle replays exactly that cycle
    const auto rows = list.getSelectedRows();
    if (rows.size() == 1 && rows[0] < cycles.size() && cycles[rows[0]].start == view.getCursor())
        playRange (cycles[rows[0]].start, cycles[rows[0]].end, transport.isLooping());
    else
        playRange (view.getCursor(), -1, transport.isLooping());
}

//==============================================================================
/** Hands a range to the player, with the version (original / conditioned) and gain that the A/B switch asks for. */
void MainComponent::playRange (juce::int64 start, juce::int64 end, bool loop)
{
    playStart = start;
    playEnd = end;
    refreshListening();
    playback.play (start, end, loop);
}

void MainComponent::refreshListening()
{
    if (audio == nullptr)
        return;

    const auto wantB = abToggle.getToggleState();
    AudioDataPtr source = audio;
    auto ready = true;
    if (wantB)
    {
        conditionPreview.requestProcessed();
        if (const auto processed = conditionPreview.getProcessed())
            source = processed;
        else
            ready = false; // still being made: the original keeps playing in the meantime
    }
    else
    {
        conditionPreview.releaseProcessed();
    }

    playback.swapAudio (source);

    double gain = 1.0;
    const auto& options = conditioning.options;
    if (wantB && ready && conditioning.previewGain && options.normalize != NormalizeMode::off)
        gain = previewGainFor (source->buffer, source->sampleRate, playStart, playEnd < 0 ? audio->getNumSamples() : playEnd, options).gain;
    playback.setGain ((float) gain);

    abToggle.setButtonText (wantB && ! ready ? WT_U8 ("B: 整音後を作成中...") : WT_U8 ("B: 整音後で試聴"));
}

void MainComponent::setConditioning (const ConditionSettings& settings)
{
    conditioning = settings;
    conditionPreview.setOptions (settings.options);
    refreshListening();
    if (conditioningWindow != nullptr)
        conditioningWindow->getPanel().updateReadouts();
    scheduleSave();
}

void MainComponent::showConditioning()
{
    if (conditioningWindow == nullptr)
    {
        ConditioningPanel::Hooks hooks;
        hooks.changed = [this] (const ConditionSettings& s) { setConditioning (s); };
        hooks.humStatus = [this] { return humStatusText(); };
        hooks.selectionInfo = [this] { return selectionGainText(); };
        conditioningWindow = std::make_unique<ConditioningWindow> (conditioning, std::move (hooks));
    }

    conditioningWindow->getPanel().setSettings (conditioning);
    conditioningWindow->setVisible (true);
    conditioningWindow->toFront (true);
}

juce::String MainComponent::humStatusText() const
{
    if (audio == nullptr)
        return WT_U8 ("(録音を開くと表示します)");
    if (! conditionPreview.humReady())
        return WT_U8 ("調べています...");

    const auto& hum = conditionPreview.getHum();
    if (hum.mainsHz <= 0.0)
        return WT_U8 ("見つかりません (何もしません)");
    return juce::String (hum.mainsHz, 0) + WT_U8 (" Hz  (倍音 ") + juce::String ((int) hum.notchHz.size()) + WT_U8 (" 本を除去)");
}

juce::String MainComponent::selectionGainText()
{
    if (audio == nullptr)
        return {};

    const auto rows = list.getSelectedRows();
    juce::int64 start = 0, end = 0;
    if (rows.size() == 1 && rows[0] < cycles.size())
    {
        start = cycles[rows[0]].start;
        end = cycles[rows[0]].end;
    }
    else if (view.hasSelection())
    {
        start = view.getSelectionStart();
        end = view.getSelectionEnd();
    }
    else
    {
        return WT_U8 ("サイクルか範囲を選ぶと、ここにノーマライズの結果が出ます");
    }

    const auto& options = conditioning.options;
    if (options.normalize == NormalizeMode::off)
        return WT_U8 ("ノーマライズはオフです");

    // stage A shows up in the peak a little, so before the conditioned copy exists this is only an estimate
    const auto processed = conditionPreview.getProcessed();
    const auto estimate = options.needsFile() && processed == nullptr;
    const auto source = processed != nullptr ? processed : audio;

    auto& cache = selectionGainCache;
    if (cache.start != start || cache.end != end || cache.source != source.get() || ! (cache.options == options))
    {
        cache.start = start;
        cache.end = end;
        cache.source = source.get();
        cache.options = options;

        const auto pg = previewGainFor (source->buffer, source->sampleRate, start, end, options);
        if (! pg.available)
        {
            cache.text = WT_U8 ("選択が 60 秒を超えるため、ここでは計算しません (書き出しでは掛かります)");
        }
        else
        {
            const auto sign = [] (double v) { return juce::String (v >= 0 ? "+" : "") + juce::String (v, 1); };
            cache.text = WT_U8 ("選択: ピーク ") + juce::String (pg.report.peakBeforeDb, 1) + " -> " + juce::String (pg.report.peakAfterDb, 1)
                         + " dBFS  (" + sign (pg.report.gainDb) + " dB)";
            if (options.normalize == NormalizeMode::lufs)
                cache.text += WT_U8 ("   ラウドネス ") + juce::String (pg.report.lufsBefore, 1) + " -> " + juce::String (pg.report.lufsBefore + pg.report.gainDb, 1) + " LUFS";
            if (pg.report.gainLimited)
                cache.text += WT_U8 ("   ※上限で止まりました");
            if (estimate)
                cache.text += WT_U8 ("   (目安)");
        }
    }
    return cache.text;
}

juce::String MainComponent::conditioningSummary() const
{
    const auto& o = conditioning.options;
    juce::StringArray parts;
    if (o.dcRemove)
        parts.add (WT_U8 ("DC 除去"));
    if (o.humMode != HumMode::off)
        parts.add (WT_U8 ("ハム除去 (") + (o.humMode == HumMode::automatic ? WT_U8 ("自動") : o.humMode == HumMode::mains50 ? "50 Hz" : "60 Hz") + ")");
    if (o.lowCutHz > 0.0f)
        parts.add (WT_U8 ("ローカット ") + juce::String (o.lowCutHz, 0) + " Hz");
    if (o.normalize == NormalizeMode::peak)
        parts.add ("Peak " + juce::String (o.targetDb, 1) + " dBFS");
    else if (o.normalize == NormalizeMode::lufs)
        parts.add (juce::String (o.targetDb, 1) + " LUFS");
    return parts.joinIntoString (" / ");
}

void MainComponent::stopPlayback()
{
    playback.stop();
}

void MainComponent::onVBlank()
{
    if (++frameCounter % 120 == 0)
        catalog.reloadIfChanged();

    suggestTick();

    if (frameCounter % 15 == 0 && conditioningWindow != nullptr && conditioningWindow->isVisible())
        conditioningWindow->getPanel().updateReadouts();

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
        if (! applyingProfile && cycles.getCategory().isNotEmpty())
        {
            juce::StringArray names;
            for (int i = 0; i < CycleModel::numSlots; ++i)
                names.add (cycles.getType (i).name);
            profiles.remember (cycles.getCategory(), cycles.getPrefix(), names);
        }

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
        configureInbox();
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
    auto* content = new InputSettingsComponent (
        wheelMap, keyMap, playback.getDeviceManager(),
        [this] {
            updateHint();
            scheduleSave();
        },
        [this] { return catalog.getDirectory(); },
        [this] (const juce::File& dir) { return changeConfigFolder (dir); });

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (content);
    options.dialogTitle = WT_U8 ("設定 (マウス / キーボード / 設定フォルダ)");
    options.dialogBackgroundColour = theme::panel;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();
}

/** Moves to another config folder: files the new place lacks are copied over (nothing is overwritten or deleted). */
juce::String MainComponent::changeConfigFolder (const juce::File& newDir)
{
    if (newDir == catalog.getDirectory())
        return WT_U8 ("すでにこのフォルダを使っています。");

    saveNow(); // make sure the current shared settings are in the old folder before they are copied
    newDir.createDirectory();
    const auto copied = Catalog::copyConfigFiles (catalog.getDirectory(), newDir);

    catalog.setDirectory (newDir);
    prefsLoadFailed = false;
    prefsBackedUp = false;

    // a folder that already holds preferences (another PC set it up) wins
    const auto prefs = readJson (catalog.getPreferencesFile());
    if (catalog.getPreferencesFile().existsAsFile() && ! prefs.isObject())
        prefsLoadFailed = true;
    else if (prefs.isObject())
        applySharedPreferences (prefs);

    lastInfoKey = {};
    setupLearning();
    refreshLibraryInfo();
    scheduleSave();
    return juce::String (copied) + WT_U8 (" 個のファイルを新しい場所へコピーし、このフォルダに切り替えました。\n元のフォルダのファイルは、そのまま残してあります。");
}

void MainComponent::offerConfigMigration()
{
    const auto shared = Catalog::sharedDirectory();
    if (shared.getFullPathName().isEmpty() || migrationDeclined || catalog.getDirectory() == shared)
        return;

    juce::Component::SafePointer<MainComponent> safe (this);
    juce::AlertWindow::showOkCancelBox (
        juce::MessageBoxIconType::QuestionIcon, WT_U8 ("設定フォルダを共有フォルダへ移しますか?"),
        WT_U8 ("Category / Type の表やショートカットなどの共通設定を、Google Drive のミラー上の次のフォルダへ移すと、別の PC でも同じ設定を使えます。\n\n")
            + shared.getFullPathName() + WT_U8 ("\n\n現在のフォルダの内容はコピーするだけで、元のファイルは残します。"),
        WT_U8 ("移す"), WT_U8 ("いまのまま"), this,
        juce::ModalCallbackFunction::create ([safe, shared] (int result) {
            if (safe == nullptr)
                return;

            if (result == 1)
            {
                const auto message = safe->changeConfigFolder (shared);
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, WT_U8 ("設定フォルダを移しました"), message);
            }
            else
            {
                safe->migrationDeclined = true;
                safe->scheduleSave();
            }
        }));
}

void MainComponent::previewCycle (int index)
{
    if (audio == nullptr || index < 0 || index >= cycles.size())
        return;

    const auto& c = cycles[index];
    view.setCursor (c.start);
    view.ensureVisible ((double) c.start);
    playRange (c.start, c.end, false);
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

    auto title = WT_U8 ("書き出し先フォルダを選択 (中に \"") + folder + WT_U8 ("\" フォルダを作ります)");
    if (conditioning.options.active())
        title += WT_U8 ("  整音: ") + conditioningSummary();

    chooser = std::make_unique<juce::FileChooser> (title, start);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this, items = std::move (items), untyped, options = conditioning.options] (const juce::FileChooser& fc) {
                              const auto root = fc.getResult();
                              if (! root.isDirectory())
                                  return;

                              exportDir = root;
                              scheduleSave();

                              juce::Component::SafePointer<MainComponent> safe (this);
                              auto data = audio;
                              const auto dest = root.getChildFile (items.front().subFolder);
                              loader.addJob ([safe, data, items, untyped, root, dest, options] {
                                  const auto result = exportCycles (*data, items, root, 24, 2.0f, options);
                                  juce::MessageManager::callAsync ([safe, result, untyped, root, dest, options] {
                                      if (safe != nullptr && result.written > 0)
                                      {
                                          safe->afterExport (dest);
                                          safe->recordLabels ("export");
                                      }
                                      juce::String msg = juce::String (result.written) + WT_U8 (" 件を書き出しました\n") + root.getFullPathName();
                                      if (options.active() && result.written > 0)
                                      {
                                          juce::StringArray lines;
                                          if (options.humMode != HumMode::off)
                                              lines.add (result.humHz > 0.0 ? WT_U8 ("ハム ") + juce::String (result.humHz, 0) + WT_U8 (" Hz を除去 (倍音 ") + juce::String (result.humNotches) + WT_U8 (" 本)")
                                                                            : WT_U8 ("ハムは見つからなかったため、何もしていません"));
                                          if (options.normalize != NormalizeMode::off)
                                          {
                                              const auto sign = [] (double v) { return juce::String (v >= 0 ? "+" : "") + juce::String (v, 1); };
                                              lines.add (WT_U8 ("ゲイン ") + sign (result.minGainDb) + " ... " + sign (result.maxGainDb) + " dB");
                                              if (result.gainLimited > 0)
                                                  lines.add (juce::String (result.gainLimited) + WT_U8 (" 件は上限 (ピークの上限 / 最大ゲイン) で止まり、目標に届いていません"));
                                          }
                                          msg += WT_U8 ("\n\n整音しました: ") + lines.joinIntoString ("\n");
                                      }
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
// Inbox: new recordings are copied to Raw, split into cycles, and queued for review
//==============================================================================
InboxPanel::Hooks MainComponent::makeInboxHooks()
{
    InboxPanel::Hooks hooks;
    hooks.getInbox = [this] { return inboxDir; };
    hooks.getRaw = [this] { return rawDir; };
    hooks.isWatching = [this] { return inboxWatch; };
    hooks.getStatus = [this] { return inboxWatcher.getStatus(); };
    hooks.chooseInbox = [this] { chooseInboxFolder ({}); };
    hooks.chooseRaw = [this] { chooseRawFolder ([this] { configureInbox(); }); };
    hooks.scanNow = [this] { inboxWatcher.scanNow(); };
    hooks.setWatching = [this] (bool on) { setInboxWatching (on); };
    hooks.openItem = [this] (const QueueItem& q) {
        const juce::File f (q.raw.isNotEmpty() ? q.raw : q.original);
        if (f.existsAsFile())
            openFile (f);
        else
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Inbox", WT_U8 ("ファイルが見つかりません:\n") + f.getFullPathName());
    };
    hooks.retryItem = [this] (const QueueItem& q) {
        inboxWatcher.forgetFailure (juce::File (q.original));
        for (int i = 0; i < inboxQueue.size(); ++i)
            if (inboxQueue.getItems()[(size_t) i].original == q.original && inboxQueue.getItems()[(size_t) i].status == "failed")
            {
                inboxQueue.remove (i);
                break;
            }
        inboxQueue.save();
        updateInboxBadge();
        inboxWatcher.scanNow();
    };
    hooks.queueChanged = [this] {
        inboxQueue.save();
        updateInboxBadge();
    };
    return hooks;
}

void MainComponent::showOrganizer()
{
    if (organizerWindow == nullptr)
    {
        OrganizerHooks hooks;
        hooks.getRoots = [this] { return libraryIndex.getRoots(); };
        hooks.getOutputDir = [this] {
            if (organizedDir.getFullPathName().isNotEmpty())
                return organizedDir;

            // the organised view must sit on the same drive as the originals: next to the first library root
            const auto roots = libraryIndex.getRoots();
            if (juce::File ("G:\\SFX_Library").isDirectory())
                return juce::File ("G:\\SFX_Library\\_Organized");
            return roots.isEmpty() ? juce::File() : juce::File (roots[0]).getParentDirectory().getChildFile ("_Organized");
        };
        hooks.setOutputDir = [this] (const juce::File& dir) {
            organizedDir = dir;
            scheduleSave();
        };
        hooks.getIndex = [this] { return libraryIndex.getData(); };
        organizerWindow = std::make_unique<OrganizerWindow> (catalog, featureStore, formats, hooks);
    }

    organizerWindow->setVisible (true);
    organizerWindow->toFront (true);
}

void MainComponent::showInbox()
{
    if (inboxWindow == nullptr)
        inboxWindow = std::make_unique<InboxWindow> (inboxQueue, makeInboxHooks());

    inboxWindow->getPanel().refresh();
    inboxWindow->setVisible (true);
    inboxWindow->toFront (true);
}

void MainComponent::updateInboxBadge()
{
    const auto n = inboxQueue.countNew();
    inboxButton.setButtonText (n > 0 ? "Inbox (" + juce::String (n) + ")" : juce::String ("Inbox"));
    if (inboxWindow != nullptr)
        inboxWindow->getPanel().refresh();
}

void MainComponent::configureInbox()
{
    InboxWatcher::Config config;
    config.inbox = inboxDir;
    config.raw = rawDir;
    config.enabled = inboxWatch && inboxDir.isDirectory() && rawDir.getFullPathName().isNotEmpty();
    for (const auto& c : catalog.getCategories())
        config.categoryNames.add (c.name);
    config.stateFile = getAppDataDir().getChildFile ("inbox_ignored.json");
    config.suggester = std::make_shared<Suggester> (suggester); // a snapshot of what has been learned so far
    inboxWatcher.configure (config);
}

void MainComponent::handleInboxOutcome (const InboxOutcome& outcome)
{
    inboxQueue.add (outcome);
    inboxQueue.save();
    updateInboxBadge();
}

void MainComponent::chooseInboxFolder (std::function<void()> then)
{
    auto start = inboxDir;
    if (! start.isDirectory())
    {
        const auto drive = juce::String::fromUTF8 ("D:\\マイドライブ\\pcm録音");
        start = juce::File (drive).isDirectory() ? juce::File (drive) : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    }

    chooser = std::make_unique<juce::FileChooser> (WT_U8 ("受信フォルダを選択 (新しい録音だけが入るフォルダ。pcm録音 の中に _Inbox を作るのがおすすめです)"), start);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this, then = std::move (then)] (const juce::FileChooser& fc) {
                              const auto dir = fc.getResult();
                              if (! dir.isDirectory())
                                  return;
                              inboxDir = dir;
                              scheduleSave();
                              configureInbox();
                              updateInboxBadge();
                              if (then)
                                  then();
                          });
}

void MainComponent::setInboxWatching (bool on)
{
    if (! on)
    {
        inboxWatch = false;
        configureInbox();
        scheduleSave();
        updateInboxBadge();
        return;
    }

    if (! inboxDir.isDirectory())
    {
        chooseInboxFolder ([this] { setInboxWatching (true); });
        return;
    }
    if (rawDir.getFullPathName().isEmpty())
    {
        chooseRawFolder ([this] { setInboxWatching (true); });
        return;
    }

    const auto begin = [this] {
        inboxWatch = true;
        backlogDecidedFor = inboxDir.getFullPathName();
        configureInbox();
        scheduleSave();
        updateInboxBadge();
    };

    // Recordings already sitting in the folder are never imported by surprise
    const auto existing = (int) listInboxCandidates (inboxDir).size();
    if (existing == 0 || backlogDecidedFor == inboxDir.getFullPathName())
    {
        begin();
        return;
    }

    juce::Component::SafePointer<MainComponent> safe (this);
    juce::AlertWindow::showYesNoCancelBox (
        juce::MessageBoxIconType::QuestionIcon, WT_U8 ("すでにある録音の扱い"),
        WT_U8 ("この受信フォルダには、すでに ") + juce::String (existing) + WT_U8 (" 個の録音があります。\n\n"
               "・すべて取り込む: Raw にコピーし、自動分割して、元のファイルは _Done に移します\n"
               "・新しい録音だけ取り込む: いまある分は、そのまま触らずに残します"),
        WT_U8 ("すべて取り込む"), WT_U8 ("新しい録音だけ"), WT_U8 ("キャンセル"), this,
        juce::ModalCallbackFunction::create ([safe, begin] (int result) {
            if (safe == nullptr || result == 0)
                return;

            if (result == 2)
                safe->inboxWatcher.ignoreExisting();
            begin();
        }));
}

//==============================================================================
// Learning: labels people give are remembered; the engine suggests from them
//==============================================================================
void MainComponent::setupLearning()
{
    labelStore.setLocation (catalog.getDirectory().getChildFile ("learning"), juce::SystemStats::getComputerName());
    labelStore.load();
    suggester.rebuild (labelStore);
    suggesterRevision = labelStore.getRevision();
    suggestedRanges.clear();
}

bool MainComponent::featuresFor (juce::int64 start, juce::int64 end, FeatureVector& out)
{
    if (audio == nullptr)
        return false;

    const auto key = std::make_pair (start, end);
    const auto it = featureCache.find (key);
    if (it != featureCache.end())
    {
        out = it->second;
        return true;
    }

    if (! computeSegmentFeatures (audio->buffer, audio->sampleRate, start, end, out))
        return false;
    featureCache[key] = out;
    return true;
}

/** A few cycles per frame: suggestions appear without ever blocking the window. */
void MainComponent::suggestTick()
{
    if (audio == nullptr || snapshotOutput != juce::File())
        return;

    if (suggesterRevision != labelStore.getRevision())
    {
        suggester.rebuild (labelStore);
        suggesterRevision = labelStore.getRevision();
        suggestedRanges.clear(); // new knowledge: look at every untyped cycle again
        configureInbox();        // the Inbox imports with the latest knowledge
    }
    if (! suggester.isReady())
        return;

    std::vector<int> indices;
    juce::StringArray types;
    std::vector<float> confidences;
    for (int i = 0; i < cycles.size() && indices.size() < 4; ++i)
    {
        const auto& c = cycles[i];
        const auto range = std::make_pair (c.start, c.end);
        if (c.type >= 0 || suggestedRanges.count (range) != 0)
            continue;

        suggestedRanges.insert (range);
        FeatureVector v {};
        if (! featuresFor (c.start, c.end, v))
            continue;

        const auto sug = suggester.suggest (v, cycles.getCategory());
        indices.push_back (i);
        types.add (sug.valid() ? sug.type : juce::String());
        confidences.push_back (sug.valid() ? sug.confidence : -1.0f);
    }

    if (! indices.empty())
        cycles.setSuggestions (indices, types, confidences);
}

/** The slot holding this Type name; a name the palette lacks takes the first free slot (so a suggestion is never lost). */
int MainComponent::slotForTypeName (const juce::String& name, bool createIfMissing)
{
    for (int i = 0; i < CycleModel::numSlots; ++i)
        if (cycles.getType (i).name.equalsIgnoreCase (name))
            return i;

    if (createIfMissing)
        for (int i = 0; i < CycleModel::numSlots; ++i)
            if (cycles.getType (i).name.isEmpty())
            {
                cycles.setTypeName (i, name);
                return i;
            }
    return -1;
}

void MainComponent::acceptSuggestions (const juce::Array<int>& rows)
{
    for (auto i : rows)
    {
        if (i < 0 || i >= cycles.size() || cycles[i].autoType.isEmpty())
            continue;

        const auto slot = slotForTypeName (cycles[i].autoType, true);
        if (slot >= 0)
            cycles.setType ({ i }, slot);
    }
}

int MainComponent::applyAllSuggestions (float minConfidence)
{
    int applied = 0;
    juce::Array<int> rows;
    for (int i = 0; i < cycles.size(); ++i)
        if (cycles[i].type < 0 && cycles[i].autoType.isNotEmpty() && cycles[i].autoConf >= minConfidence)
            rows.add (i);

    if (rows.isEmpty())
        return 0;

    // as one undo step; typed automatically, so they stay "unconfirmed" until the file is saved
    juce::Array<int> slots;
    for (auto i : rows)
        slots.add (slotForTypeName (cycles[i].autoType, true));

    std::vector<Cycle> all;
    for (int i = 0; i < cycles.size(); ++i)
        all.push_back (cycles[i]);
    for (int k = 0; k < rows.size(); ++k)
        if (slots[k] >= 0)
        {
            all[(size_t) rows[k]].type = slots[k];
            all[(size_t) rows[k]].confirmed = false;
            ++applied;
        }

    cycles.replaceAll (std::move (all));
    return applied;
}

/** Saving (or exporting) is the moment a person has looked at the file: every cycle becomes a confirmed example. */
void MainComponent::recordLabels (const juce::String& source)
{
    if (audio == nullptr || snapshotOutput != juce::File())
        return;

    cycles.markAllConfirmed();

    std::vector<LabelSample> out;
    for (int i = 0; i < cycles.size(); ++i)
    {
        const auto& c = cycles[i];
        if (c.type < 0)
            continue;

        LabelSample s;
        if (! featuresFor (c.start, c.end, s.v))
            continue;

        s.fileName = audio->file.getFileName();
        s.start = c.start;
        s.end = c.end;
        s.key = LabelStore::makeKey (s.fileName, audio->getNumSamples(), c.start, c.end); // length in samples: unchanged by embedding markers
        s.category = cycles.getCategory();
        s.type = cycles.getType (c.type).name;
        s.source = source;
        s.human = true;
        s.suggestedCategory = cycles.getCategory();
        s.suggestedType = c.autoType;
        s.suggestedConfidence = c.autoConf;
        s.rating = c.rating;
        s.ng = c.ng;
        s.attributes = c.attributes;
        out.push_back (s);
    }

    labelStore.upsert (std::move (out));
}

//==============================================================================
// Category -> Prefix / Type names
//==============================================================================
CategoryProfile MainComponent::resolveCategoryProfile (const juce::String& name) const
{
    juce::String csvPrefix;
    for (const auto& c : catalog.getCategories())
        if (c.name == name)
            csvPrefix = c.prefix;

    return profiles.resolve (name, csvPrefix, catalog.getTypes (name));
}

void MainComponent::applyCategoryProfile (const juce::String& name, const CategoryProfile& profile)
{
    {
        // one batch: the palette passes through half-updated states that must not be remembered
        const juce::ScopedValueSetter<bool> guard (applyingProfile, true);
        cycles.setPrefix (profile.prefix);
        for (int i = 0; i < CycleModel::numSlots; ++i)
            cycles.setTypeName (i, profile.types[i]);
        cycles.setCategory (name);
    }

    juce::StringArray names;
    for (int i = 0; i < CycleModel::numSlots; ++i)
        names.add (cycles.getType (i).name);
    profiles.remember (name, cycles.getPrefix(), names);
}

/** Choosing a Category switches Prefix and Type names to that Category's own. If cycles are already typed, ask first,
    because their output names would change. */
void MainComponent::chooseCategory (const juce::String& name)
{
    catalog.noteUsed (name);

    if (name.isEmpty())
    {
        cycles.setCategory (name);
        return;
    }

    const auto target = resolveCategoryProfile (name);

    bool differs = cycles.getPrefix() != target.prefix;
    for (int i = 0; i < CycleModel::numSlots; ++i)
        differs = differs || cycles.getType (i).name != target.types[i];

    bool anyTyped = false;
    for (int i = 0; i < cycles.size(); ++i)
        anyTyped = anyTyped || cycles[i].type >= 0;

    if (! differs || ! anyTyped)
    {
        applyCategoryProfile (name, target);
        return;
    }

    juce::Component::SafePointer<MainComponent> safe (this);
    juce::AlertWindow::showOkCancelBox (
        juce::MessageBoxIconType::QuestionIcon, WT_U8 ("Category に合わせて切り替えますか?"),
        WT_U8 ("「") + name + WT_U8 ("」の Prefix と Type 名に切り替えます。\n\nPrefix: ") + cycles.getPrefix() + " -> " + target.prefix
            + WT_U8 ("\n\nすでに打ったサイクルの出力名も変わります。「いまのまま」を選ぶと、Category だけ変え、現在の Prefix と Type 名をこの Category の設定として覚えます。"),
        WT_U8 ("切り替える"), WT_U8 ("いまのまま"), this,
        juce::ModalCallbackFunction::create ([safe, name, target] (int result) {
            if (safe == nullptr)
                return;
            if (result == 1)
                safe->applyCategoryProfile (name, target);
            else
                safe->cycles.setCategory (name); // keeps the current names; they are remembered for this Category
        }));
}

//==============================================================================
// Markers inside the WAV
//==============================================================================
void MainComponent::showMarkersMenu()
{
    juce::PopupMenu menu;
    menu.addItem (1, WT_U8 ("WAV 内のマーカーを読み込む (RX など)"), audio != nullptr);
    menu.addItem (2, WT_U8 ("WAV にマーカーを書き込む   ") + keyMap.describe (Action::SaveWav), audio != nullptr);
    menu.addSeparator();
    menu.addItem (4, WT_U8 ("候補の Type を採用 (選んだサイクル)   ") + keyMap.describe (Action::AcceptSuggestion), audio != nullptr);
    menu.addItem (5, WT_U8 ("候補をすべて適用 (確信度 70% 以上)"), audio != nullptr);
    menu.addSeparator();
    menu.addItem (3, WT_U8 ("Raw フォルダ: ") + (rawDir.getFullPathName().isEmpty() ? WT_U8 ("(未設定)") : rawDir.getFullPathName()) + WT_U8 ("  を変更..."));

    juce::Component::SafePointer<MainComponent> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&importButton), [safe] (int result) {
        if (safe == nullptr)
            return;
        if (result == 1)
            safe->doImportMarkers();
        else if (result == 2)
            safe->saveMarkersToWav();
        else if (result == 3)
            safe->chooseRawFolder ({});
        else if (result == 4)
            safe->acceptSuggestions (safe->list.getSelectedRows());
        else if (result == 5)
        {
            const auto n = safe->applyAllSuggestions (0.7f);
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, WT_U8 ("候補の適用"),
                                                    n > 0 ? juce::String (n) + WT_U8 (" 件に Type を付けました(「(自動)」と表示。保存すると確認済みになります。Ctrl+Z で戻せます)")
                                                          : WT_U8 ("確信度 70% 以上の候補がありません。"));
        }
    });
}

void MainComponent::doImportMarkers()
{
    if (audio == nullptr)
        return;

    const auto n = importMarkers();
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Import markers",
                                            n > 0 ? juce::String (n) + WT_U8 (" 件のマーカーを読み込みました (Ctrl+Z で元に戻せます)")
                                                  : WT_U8 ("この WAV にはマーカー(cue)が見つかりませんでした"));
}

bool MainComponent::isInRawFolder (const juce::File& f) const
{
    return rawDir.getFullPathName().isNotEmpty() && f.isAChildOf (rawDir);
}

void MainComponent::chooseRawFolder (std::function<void()> then)
{
    auto start = rawDir;
    if (! start.isDirectory())
        start = juce::File ("G:\\SFX_Library").isDirectory() ? juce::File ("G:\\SFX_Library") : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);

    chooser = std::make_unique<juce::FileChooser> (WT_U8 ("Raw フォルダを選択 (元の録音のコピーを、年ごとのサブフォルダに保管します)"), start);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this, then = std::move (then)] (const juce::FileChooser& fc) {
                              const auto dir = fc.getResult();
                              if (! dir.isDirectory())
                                  return;
                              rawDir = dir;
                              scheduleSave();
                              if (then)
                                  then();
                          });
}

juce::File MainComponent::uniqueRawDestination (const juce::File& source) const
{
    const auto year = juce::String (juce::Time::getCurrentTime().getYear());
    const auto folder = rawDir.getChildFile (year);

    auto dest = folder.getChildFile (source.getFileName());
    for (int i = 1; dest.exists(); ++i)
        dest = folder.getChildFile (source.getFileNameWithoutExtension() + "_" + juce::String (i) + source.getFileExtension());
    return dest;
}

void MainComponent::saveMarkersToWav()
{
    if (audio == nullptr || embedding)
        return;

    if (cycles.size() == 0)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, WT_U8 ("WAV に書き込む"),
                                                WT_U8 ("サイクルがまだありません。範囲を選んで数字キーで打ってから、もう一度お試しください。"));
        return;
    }

    const auto source = audio->file;
    if (! source.hasFileExtension ("wav"))
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, WT_U8 ("WAV に書き込む"),
                                                WT_U8 ("マーカーを書き込めるのは WAV ファイルだけです (") + source.getFileName() + WT_U8 (")。"));
        return;
    }

    // a recording already kept in the Raw folder is updated in place
    if (isInRawFolder (source))
    {
        embedNow (source, source, false);
        return;
    }

    juce::Component::SafePointer<MainComponent> safe (this);
    juce::AlertWindow::showYesNoCancelBox (
        juce::MessageBoxIconType::QuestionIcon, WT_U8 ("WAV にマーカーを書き込む"),
        WT_U8 ("このファイルは Raw フォルダの外にあります:\n") + source.getFullPathName()
            + WT_U8 ("\n\n・Raw にコピーして書き込む (おすすめ): 元の録音はそのまま残し、コピーにだけ書き込んで、以後はそのコピーを編集します\n"
                     "・元のファイルに書き込む: この録音そのものに書き込みます (音声データは変わりません)"),
        WT_U8 ("Raw にコピーして書き込む"), WT_U8 ("元のファイルに書き込む"), WT_U8 ("キャンセル"), this,
        juce::ModalCallbackFunction::create ([safe, source] (int result) {
            if (safe == nullptr || result == 0)
                return;

            if (result == 2)
            {
                safe->embedNow (source, source, false);
                return;
            }

            const auto copyIn = [safe, source] {
                if (safe != nullptr)
                    safe->embedNow (source, safe->uniqueRawDestination (source), true);
            };

            if (safe->rawDir.getFullPathName().isEmpty())
                safe->chooseRawFolder (copyIn);
            else
                copyIn();
        }));
}

void MainComponent::embedNow (const juce::File& source, const juce::File& dest, bool switchToDest)
{
    embedding = true;
    const auto request = buildEmbedRequest (cycles, audio->sampleRate, JUCE_APPLICATION_VERSION_STRING);
    fileLabel.setText (WT_U8 ("WAV に書き込み中... ") + dest.getFileName(), juce::dontSendNotification);

    juce::Component::SafePointer<MainComponent> safe (this);
    loader.addJob ([safe, source, dest, request, switchToDest] {
        if (dest != source)
            dest.getParentDirectory().createDirectory();

        const auto result = embedMarkers (source, dest, request);
        juce::MessageManager::callAsync ([safe, result, dest, switchToDest] {
            if (safe != nullptr)
                safe->embedFinished (result, dest, switchToDest);
        });
    });
}

void MainComponent::embedFinished (const EmbedResult& result, const juce::File& dest, bool switchToDest)
{
    embedding = false;
    fileLabel.setText (audio != nullptr ? audio->file.getFullPathName() : juce::String(), juce::dontSendNotification);

    if (! result.ok)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, WT_U8 ("WAV に書き込めませんでした"),
                                                result.error + WT_U8 ("\n\n元のファイルは変更されていません。"));
        return;
    }

    recordLabels ("editor");
    inboxQueue.markReviewed (dest.getFullPathName());
    inboxQueue.save();
    updateInboxBadge();

    const auto message = juce::String (result.regionsWritten) + WT_U8 (" 件のマーカーを書き込みました。\n") + dest.getFullPathName()
                         + (switchToDest ? WT_U8 ("\n\n元の録音は変更していません。以後は、このコピーを編集しています。")
                                         : WT_U8 ("\n\n音声データは変わっていません (書き込み後に照合済み)。RX など他のソフトでも、リージョンとして見えます。"));

    if (switchToDest)
        openFile (dest); // the cycles come back from the wtag chunk

    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, WT_U8 ("WAV に書き込みました"), message);
}

/** Puts the cycles stored in a WAV into the editor. Types are matched to the palette by NAME (slot numbers may differ
    between PCs / sessions); a type the palette does not have yet takes the first free slot so nothing is lost. */
int MainComponent::applyEmbeddedState (const WtagData& data)
{
    int added = 0;
    std::vector<Cycle> restored;

    for (const auto& c : data.cycles)
    {
        int slot = -1;
        if (c.typeName.isNotEmpty())
        {
            for (int i = 0; i < CycleModel::numSlots && slot < 0; ++i)
                if (cycles.getType (i).name.equalsIgnoreCase (c.typeName))
                    slot = i;

            for (int i = 0; i < CycleModel::numSlots && slot < 0; ++i)
                if (cycles.getType (i).name.isEmpty())
                {
                    cycles.setTypeName (i, c.typeName);
                    slot = i;
                    ++added;
                }
        }
        Cycle restoredCycle;
        restoredCycle.start = c.start;
        restoredCycle.end = c.end;
        restoredCycle.type = slot;
        restoredCycle.autoType = c.autoType;
        restoredCycle.autoConf = c.autoConf;
        restoredCycle.confirmed = c.confirmed;
        restoredCycle.rating = c.rating;
        restoredCycle.ng = c.ng;
        restoredCycle.attributes = c.attributes;
        restored.push_back (restoredCycle);
    }

    cycles.replaceAll (std::move (restored), false);
    return added;
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
    inboxButton.setBounds (bar.removeFromLeft (92));
    bar.removeFromLeft (12);
    importButton.setBounds (bar.removeFromLeft (90));
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

    // the conditioning controls sit in the empty middle of the transport bar, next to the play controls
    auto tb = transport.getBounds().withTrimmedLeft (430).withHeight (28).withY (transport.getY() + (transportHeight - 28) / 2);
    conditionButton.setBounds (tb.removeFromLeft (70));
    tb.removeFromLeft (8);
    abToggle.setBounds (tb.removeFromLeft (160));
    conditionButton.toFront (false);
    abToggle.toFront (false);
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
        case Action::ImportMarkers: doImportMarkers(); break;
        case Action::SaveWav:       saveMarkersToWav(); break;
        case Action::AcceptSuggestion:
            acceptSuggestions (list.getSelectedRows());
            break;
        case Action::ToggleConditioned:
            abToggle.setToggleState (! abToggle.getToggleState(), juce::dontSendNotification);
            refreshListening();
            break;
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
