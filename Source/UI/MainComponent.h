#pragma once

#include "Core/AutoSplit.h"
#include "Core/Catalog.h"
#include "Core/CycleModel.h"
#include "Core/EmbeddedState.h"
#include "Core/LibraryIndex.h"
#include "Core/PlaybackEngine.h"
#include "Core/SpectrogramRenderer.h"
#include "CycleList.h"
#include "InputMap.h"
#include "InputSettings.h"
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
    CycleModel& testCyclesMutable() { return cycles; }
    const CycleModel& testCycles() const { return cycles; }

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
    void applySharedPreferences (const juce::var& prefs);
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
    ViewState view;
    SpectrogramRenderer spectrogram;
    PlaybackEngine playback;
    CycleModel cycles;
    Catalog catalog;
    KeyMap keyMap;
    LibraryIndex libraryIndex;
    std::unique_ptr<LibraryWindow> libraryWindow;
    AudioDataPtr audio;

    juce::TextButton openButton { "Open" }, audioButton { "Audio..." }, inputButton { "Input..." };
    juce::TextButton importButton { "Import markers" }, splitButton { "Auto split" }, exportButton { "Export..." }, libraryButton { "Library" };
    juce::ToggleButton autoPreviewToggle { "Auto preview" }, continueToggle { "Continue numbering" };
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
    juce::File snapshotOutput, exportDir, rawDir;
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
