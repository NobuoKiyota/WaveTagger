#pragma once

#include "Core/AutoSplit.h"
#include "Core/Catalog.h"
#include "Core/FeatureStore.h"
#include "Core/InboxProcessor.h"
#include "Core/InboxQueue.h"
#include "Core/LabelStore.h"
#include "Core/Suggester.h"
#include "Core/CategoryProfiles.h"
#include "Core/ConditioningPreview.h"
#include "Core/CycleModel.h"
#include "Core/EmbeddedState.h"
#include "Core/LibraryIndex.h"
#include "Core/PlaybackEngine.h"
#include "Core/SpectrogramRenderer.h"
#include "ConditioningPanel.h"
#include "CycleList.h"
#include "InputMap.h"
#include "InboxWindow.h"
#include "InputSettings.h"
#include "OrganizerWindow.h"
#include "KeyMap.h"
#include "LibraryPanel.h"
#include "OverviewBar.h"
#include "PalettePanel.h"
#include "TransportBar.h"
#include "WaveView.h"
#include <juce_audio_utils/juce_audio_utils.h>

namespace wt
{
class MainComponent : public juce::Component,
                      public juce::FileDragAndDropTarget,
                      private juce::ChangeListener
{
public:
    MainComponent();
    ~MainComponent() override;

    void openFile (const juce::File& file);

    /** Loads a file, auto-splits it into demo cycles, writes a PNG of the window and quits (for automated visual checks). */
    void runSnapshot (const juce::File& input, const juce::File& output);

    std::function<void (const juce::String&)> onTitleChanged;

    // hooks for the isolated self tests (Main.cpp --selftest-prefs)
    void testSaveNow() { saveNow(); }
    bool testPrefsLoadFailed() const { return prefsLoadFailed; }
    juce::String testChangeFolder (const juce::File& dir) { return changeConfigFolder (dir); }
    const Catalog& testCatalog() const { return catalog; }
    int testApplyEmbedded (const WtagData& d) { return applyEmbeddedState (d); }
    LabelStore& testLabelStore() { return labelStore; }
    void testLoadAudio (AudioDataPtr d) { audioLoaded (std::move (d), {}); }
    void testSuggestTick() { suggestTick(); }
    void testRecord (const juce::String& source) { recordLabels (source); }
    void testAccept (const juce::Array<int>& rows) { acceptSuggestions (rows); }
    int testApplyAll (float threshold) { return applyAllSuggestions (threshold); }
    void testChooseCategoryNoDialog (const juce::String& name) { catalog.noteUsed (name); applyCategoryProfile (name, resolveCategoryProfile (name)); }
    const CategoryProfiles& testProfiles() const { return profiles; }
    CycleModel& testCyclesMutable() { return cycles; }
    const CycleModel& testCycles() const { return cycles; }
    const ConditionSettings& testConditioning() const { return conditioning; }
    void testSetConditioning (const ConditionSettings& s) { setConditioning (s); }
    ConditioningPreview& testPreview() { return conditionPreview; }
    juce::String testHumStatus() const { return humStatusText(); }
    juce::String testSelectionGain() { return selectionGainText(); }
    juce::String testSummary() const { return conditioningSummary(); }
    void testRefreshListening() { refreshListening(); }

    void paint (juce::Graphics& g) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    void audioLoaded (AudioDataPtr data, const juce::String& error);
    void togglePlay();
    void stopPlayback();
    void onVBlank();
    void refreshTransport();
    void showOpenDialog();
    void showAudioSettings();
    void takeSnapshot();

    // cycles
    void handleTypeKey (int slot);
    void addCycleFromSelection (int slot);
    void previewCycle (int index);
    void deleteSelectedCycles();
    void applyType (const juce::Array<int>& rows, int type);
    void showAutoSplitDialog();
    void runAutoSplit (const AutoSplitParams& params);
    int importMarkers();
    void startExport();
    void showLibrary();
    void refreshLibraryInfo();
    void setLibraryRoots (const juce::StringArray& roots);
    void afterExport (const juce::File& destFolder);
    void showInputSettings();
    void showConditioning();
    void setConditioning (const ConditionSettings& settings);
    void refreshListening();
    void playRange (juce::int64 start, juce::int64 end, bool loop);
    juce::String conditioningSummary() const;
    juce::String humStatusText() const;
    juce::String selectionGainText();
    void applySharedPreferences (const juce::var& prefs);
    void chooseCategory (const juce::String& name);
    void applyCategoryProfile (const juce::String& name, const CategoryProfile& profile);
    CategoryProfile resolveCategoryProfile (const juce::String& name) const;
    void showInbox();
    void showOrganizer();
    void configureInbox();
    void updateInboxBadge();
    void chooseInboxFolder (std::function<void()> then);
    void setInboxWatching (bool on);
    void handleInboxOutcome (const InboxOutcome& outcome);
    InboxPanel::Hooks makeInboxHooks();
    void setupLearning();
    void suggestTick();
    void recordLabels (const juce::String& source);
    int slotForTypeName (const juce::String& name, bool createIfMissing);
    void acceptSuggestions (const juce::Array<int>& rows);
    int applyAllSuggestions (float minConfidence);
    bool featuresFor (juce::int64 start, juce::int64 end, FeatureVector& out);
    void doImportMarkers();
    void showMarkersMenu();
    void saveMarkersToWav();
    void chooseRawFolder (std::function<void()> then);
    bool isInRawFolder (const juce::File& f) const;
    juce::File uniqueRawDestination (const juce::File& source) const;
    void embedNow (const juce::File& source, const juce::File& dest, bool switchToDest);
    void embedFinished (const EmbedResult& result, const juce::File& dest, bool switchToDest);
    int applyEmbeddedState (const WtagData& data);
    juce::String changeConfigFolder (const juce::File& newDir);
    void offerConfigMigration();
    void updateHint();
    bool confirmReselect (int type);

    // persistence
    void loadSettings();
    void scheduleSave();
    void saveNow();

    juce::AudioFormatManager formats;
    InboxWatcher inboxWatcher { formats };
    InboxQueue inboxQueue;
    std::unique_ptr<InboxWindow> inboxWindow;
    FeatureStore featureStore { FeatureStore::defaultFile() };
    std::unique_ptr<OrganizerWindow> organizerWindow;
    ViewState view;
    SpectrogramRenderer spectrogram;
    PlaybackEngine playback;
    CycleModel cycles;
    Catalog catalog;
    CategoryProfiles profiles;
    LabelStore labelStore;
    Suggester suggester;
    int suggesterRevision = -1;
    std::map<std::pair<juce::int64, juce::int64>, FeatureVector> featureCache; // per cycle range of the open file
    std::set<std::pair<juce::int64, juce::int64>> suggestedRanges;
    bool applyingProfile = false;
    KeyMap keyMap;
    LibraryIndex libraryIndex;
    std::unique_ptr<LibraryWindow> libraryWindow;
    AudioDataPtr audio;
    ConditionSettings conditioning;
    ConditioningPreview conditionPreview;
    std::unique_ptr<ConditioningWindow> conditioningWindow;
    juce::int64 playStart = 0, playEnd = -1; // the range last handed to the player
    struct
    {
        juce::int64 start = -1, end = -1;
        const AudioData* source = nullptr;
        ConditionOptions options;
        juce::String text;
    } selectionGainCache;

    juce::TextButton openButton { "Open" }, inboxButton { "Inbox" }, inputButton { "Settings..." };
    juce::TextButton importButton { "Import markers" }, splitButton { "Auto split" }, exportButton { "Export..." }, libraryButton { "Library" };
    juce::TextButton conditionButton { "Tone" };
    juce::ToggleButton autoPreviewToggle { "Auto preview" }, continueToggle { "Continue numbering" }, abToggle { "B" };
    juce::Label fileLabel, blendLabel, floorLabel, hintLabel;
    juce::ComboBox schemeCombo;
    WheelMap wheelMap;
    juce::Slider blendSlider, floorSlider;
    PalettePanel palette { cycles, catalog };
    OverviewBar overview { view };
    WaveView waveView { view, spectrogram };
    CycleList list { cycles };
    TransportBar transport;

    juce::ThreadPool loader { juce::ThreadPoolOptions {}.withNumberOfThreads (1) };
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::VBlankAttachment> vblank;
    juce::String statusText;
    bool wasPlaying = false;
    juce::File snapshotOutput, exportDir, rawDir, inboxDir, organizedDir;
    bool inboxWatch = false;
    juce::String backlogDecidedFor;
    bool embedding = false;
    int saveGeneration = 0;
    juce::String lastInfoKey;
    int snapshotWaits = 0;
    int frameCounter = 0;
    bool prefsLoadFailed = false, prefsBackedUp = false, migrationDeclined = false;
    int editingCycle = -1; // cycle whose range is being re-selected (double click)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
} // namespace wt
