#include "Core/Catalog.h"
#include "Core/TextDecode.h"
#include "Core/EmbeddedState.h"
#include "Core/Checks.h"
#include "Core/LabelStore.h"
#include "Core/Suggester.h"
#include "Core/Clustering.h"
#include "Core/Attributes.h"
#include "Core/Conditioning.h"
#include "Core/Dataset.h"
#include "Core/Dictionary.h"
#include "Core/Exporter.h"
#include "Core/FeatureStore.h"
#include "Core/HardLinks.h"
#include "Core/InboxQueue.h"
#include "Core/ReclassDb.h"
#include "Core/WavChunks.h"
#include "Core/WavMarkers.h"
#include "Core/Xlsx.h"
#include "UI/AttributeEditor.h"
#include "UI/CategoryPicker.h"
#include "UI/DictionaryWindow.h"
#include "UI/InboxWindow.h"
#include "UI/OrganizerWindow.h"
#include "UI/InputSettings.h"
#include "UI/MainComponent.h"
#include "UI/Theme.h"
#include "Util/Text.h"

namespace wt
{
class MainWindow : public juce::DocumentWindow
{
public:
    MainWindow()
        : DocumentWindow ("WaveTagger", theme::background, DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        auto* content = new MainComponent();
        content->onTitleChanged = [this] (const juce::String& title) { setName (title); };
        setContentOwned (content, true);
        setResizable (true, false);
        setResizeLimits (1180, 600, 10000, 10000);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    MainComponent& getMain() { return *static_cast<MainComponent*> (getContentComponent()); }

    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
};

class WaveTaggerApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise (const juce::String&) override
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

        const auto args = getCommandLineParameterArray();
        auto toFile = [] (const juce::String& s) { return juce::File::getCurrentWorkingDirectory().getChildFile (s.unquoted()); };

        // Automated visual checks must never touch the real settings / CSV folder
        const auto isSnapshotMode = args.size() >= 1 && (args[0].startsWith ("--snapshot") || args[0].startsWith ("--selftest") || args[0].startsWith ("--report"));
        if (isSnapshotMode && juce::SystemStats::getEnvironmentVariable ("WT_APPDATA", {}).isEmpty())
        {
            std::fprintf (stderr, "snapshot modes require the WT_APPDATA environment variable\n");
            setApplicationReturnValue (2);
            quit();
            return;
        }

        if (args.size() >= 1 && (args[0] == "--selftest-catalog" || args[0] == "--selftest-catalog-load" || args[0] == "--selftest-prefs"
                                 || args[0] == "--selftest-wav" || args[0] == "--selftest-inbox" || args[0] == "--selftest-watcher" || args[0] == "--selftest-organize" || args[0] == "--selftest-checks" || args[0] == "--report-checks" || args[0] == "--selftest-learn" || args[0] == "--selftest-condition" || args[0] == "--selftest-rating" || args[0] == "--selftest-dataset" || args[0] == "--selftest-dictionary" || args[0] == "--selftest-export" || args[0] == "--report-learn"))
        {
            setApplicationReturnValue ((args[0] == "--selftest-catalog" ? selfTestCatalog()
                                        : args[0] == "--selftest-prefs" ? selfTestPrefs()
                                        : args[0] == "--selftest-wav" ? selfTestWav()
                                        : args[0] == "--selftest-inbox" ? selfTestInbox()
                                        : args[0] == "--selftest-watcher" ? selfTestWatcher()
                                        : args[0] == "--selftest-organize" ? selfTestOrganize()
                                        : args[0] == "--selftest-checks" ? selfTestChecks()
                                        : args[0] == "--selftest-learn" ? selfTestLearn()
                                        : args[0] == "--selftest-condition" ? selfTestCondition()
                                        : args[0] == "--selftest-rating" ? selfTestRating()
                                        : args[0] == "--selftest-dataset" ? selfTestDataset()
                                        : args[0] == "--selftest-dictionary" ? selfTestDictionary()
                                        : args[0] == "--selftest-export" ? selfTestExport()
                                        : args[0] == "--report-learn" ? reportLearn()
                                        : args[0] == "--report-checks" ? reportChecks()
                                                                       : selfTestCatalogLoad()) ? 0 : 1);
            quit();
            return;
        }

        if (args.size() >= 2 && (args[0] == "--snapshot-picker" || args[0] == "--snapshot-conditioning" || args[0] == "--snapshot-attributes" || args[0] == "--snapshot-dictionary" || args[0] == "--snapshot-input" || args[0] == "--snapshot-inbox" || args[0] == "--snapshot-organizer"))
        {
            snapshotComponent (args[0], toFile (args[1]));
            return;
        }

        mainWindow = std::make_unique<MainWindow>();

        if (args.size() >= 3 && args[0] == "--snapshot")
            mainWindow->getMain().runSnapshot (toFile (args[1]), toFile (args[2]));
        else if (args.size() >= 1 && toFile (args[0]).existsAsFile())
            mainWindow->getMain().openFile (toFile (args[0]));
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override { quit(); }

private:
    /** Renders the category accordion or the input-settings dialog to a PNG, then quits. */
    void snapshotComponent (const juce::String& mode, const juce::File& output)
    {
        const auto picker = mode == "--snapshot-picker";
        catalog.setDirectory (Catalog::defaultDirectory());
        for (const char* used : { "TOOLS", "TOOLS", "TOOLS", "PAPER", "PAPER", "METAL" })
            catalog.noteUsed (used);

        juce::AudioDeviceManager devices;
        std::unique_ptr<juce::Component> content;
        std::unique_ptr<FeatureStore> demoStore;
        std::unique_ptr<Dictionary> demoDictionary;
        std::unique_ptr<juce::AudioFormatManager> demoFormats;
        if (mode == "--snapshot-organizer")
        {
            // 24 synthetic sounds of three kinds, deliberately mixed under neutral names
            const auto lib = Catalog::defaultDirectory().getParentDirectory().getChildFile ("organizer_demo").getChildFile ("ImpactSound");
            lib.deleteRecursively();
            lib.createDirectory();
            demoFormats = std::make_unique<juce::AudioFormatManager>();
            demoFormats->registerBasicFormats();
            int n = 0;
            for (int variant = 0; variant < 8; ++variant)
                for (int kind = 0; kind < 3; ++kind)
                {
                    juce::Random rnd (500 + variant * 13 + kind * 71);
                    const auto seconds = kind == 0 ? 0.5 + 0.03 * variant : kind == 1 ? 2.0 + 0.1 * variant : 3.0;
                    juce::AudioBuffer<float> b (1, (int) (seconds * 44100.0));
                    float lp = 0.0f;
                    for (int i = 0; i < b.getNumSamples(); ++i)
                    {
                        const auto t = i / 44100.0;
                        float v = kind == 0 ? (rnd.nextFloat() * 2.0f - 1.0f) * (float) std::exp (-t * (18.0 + variant))
                                : kind == 1 ? 0.4f * (float) (std::sin (6.2831853 * (330.0 + 55.0 * variant) * t) * std::min (1.0, t * 8.0))
                                            : (lp += 0.015f * ((rnd.nextFloat() * 2.0f - 1.0f) - lp), lp * 8.0f);
                        b.setSample (0, i, juce::jlimit (-1.0f, 1.0f, v));
                    }
                    juce::WavAudioFormat wav;
                    std::unique_ptr<juce::OutputStream> stream (lib.getChildFile ("ImpactSound_" + juce::String (++n).paddedLeft ('0', 2) + ".wav").createOutputStream());
                    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions {}.withSampleRate (44100.0).withNumChannels (1).withBitsPerSample (16));
                    writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
                }
            demoStore = std::make_unique<FeatureStore> (lib.getParentDirectory().getChildFile ("features_demo.bin"));
        }

        InboxQueue demoQueue;
        if (mode == "--snapshot-inbox")
        {
            InboxOutcome a, b, c;
            a.ok = true; a.original = "D:\\pcm\\_Inbox\\TOOLS\\Open\\2026-10-23_14-22-36.wav"; a.rawFile = "G:\\SFX_Library\\Raw\\2026\\2026-10-23_14-22-36.wav";
            a.cycles = 26; a.categoryHint = "TOOLS"; a.typeHint = "Open";
            b.ok = true; b.original = "D:\\pcm\\_Inbox\\HeavyRain.wav"; b.rawFile = "G:\\SFX_Library\\Raw\\2026\\HeavyRain.wav"; b.cycles = 4;
            c.ok = false; c.original = "D:\\pcm\\_Inbox\\broken.wav"; c.error = "Unsupported or unreadable file: broken.wav";
            demoQueue.add (a);
            demoQueue.add (b);
            demoQueue.add (c);
            demoQueue.markReviewed (b.rawFile.getFullPathName());
        }

        if (mode == "--snapshot-organizer")
        {
            OrganizerHooks hooks;
            const auto lib = Catalog::defaultDirectory().getParentDirectory().getChildFile ("organizer_demo");
            hooks.getRoots = [lib] { return juce::StringArray { lib.getFullPathName() }; };
            hooks.getOutputDir = [lib] { return lib.getParentDirectory().getChildFile ("organised_view"); };
            hooks.setOutputDir = [] (const juce::File&) {};
            auto panel = std::make_unique<OrganizerPanel> (catalog, *demoStore, *demoFormats, hooks);
            panel->setSize (1180, 700);
            panel->runDemo (lib.getChildFile ("ImpactSound"), "TOOLS", "Hit");
            content = std::move (panel);
        }
        else if (mode == "--snapshot-inbox")
        {
            InboxPanel::Hooks hooks;
            hooks.getInbox = [] { return juce::File (juce::String::fromUTF8 ("D:\\マイドライブ\\pcm録音\\_Inbox")); };
            hooks.getRaw = [] { return juce::File ("G:\\SFX_Library\\Raw"); };
            hooks.isWatching = [] { return true; };
            hooks.getStatus = [] { return juce::String ("watching"); };
            content = std::make_unique<InboxPanel> (demoQueue, hooks);
        }
        else if (mode == "--snapshot-dictionary")
        {
            // a throw-away dictionary with a few user entries, so the list shows both kinds
            const auto demoFile = Catalog::defaultDirectory().getParentDirectory().getChildFile ("dictionary_demo/dictionary.csv");
            demoFile.getParentDirectory().deleteRecursively();
            demoDictionary = std::make_unique<Dictionary>();
            demoDictionary->setFile (demoFile);
            demoDictionary->setUserEntry ("cardboard box", WT_U8 ("段ボール箱"));
            demoDictionary->setUserEntry ("rough", WT_U8 ("ざらざら"));
            demoDictionary->setUserEntry ("strong", WT_U8 ("力強い"));
            DictionaryPanel::Hooks hooks;
            hooks.untranslated = [] { return juce::StringArray { "texture", WT_U8 ("かなづち") }; };
            auto panel = std::make_unique<DictionaryPanel> (*demoDictionary, std::move (hooks));
            panel->prepare ("texture");
            content = std::move (panel);
        }
        else if (mode == "--snapshot-attributes")
        {
            AttributeVocabulary vocab;
            vocab.learn ("tool=hammer tool=hammer speaker=left");
            auto editor = std::make_unique<AttributeEditor> (vocab, "strength=strong material=\"cardboard box\"  outdoor", 3, [] (const juce::String&) {});
            content = std::move (editor);
        }
        else if (mode == "--snapshot-conditioning")
        {
            ConditioningPanel::Hooks hooks;
            hooks.humStatus = [] { return WT_U8 ("60 Hz  (倍音 4 本を除去)"); };
            hooks.panStatus = [] { return WT_U8 ("L が R より 1.8 dB 大きい → ±0.9 dB ずつ補正"); };
            hooks.selectionInfo = [] { return WT_U8 ("選択: ピーク -12.3 -> -1.0 dBFS  (+11.3 dB)"); };
            ConditionSettings demo;
            demo.preset = ConditionPreset::standard;
            demo.options = presetOptions (ConditionPreset::standard);
            content = std::make_unique<ConditioningPanel> (demo, std::move (hooks));
        }
        else if (picker)
            content = std::make_unique<CategoryPicker> (catalog, "TOOLS", [] (const juce::String&) {}, [] {});
        else
        {
            auto input = std::make_unique<InputSettingsComponent> (wheelMap, keyMap, devices, [] {}, [this] { return catalog.getDirectory(); }, [] (const juce::File&) { return juce::String(); });
            if (const auto tab = juce::SystemStats::getEnvironmentVariable ("WT_DEMO_TAB", {}); tab.isNotEmpty())
                input->setCurrentTabIndex (tab.getIntValue());
            content = std::move (input);
        }
        if (mode != "--snapshot-organizer" && mode != "--snapshot-conditioning" && mode != "--snapshot-attributes" && mode != "--snapshot-dictionary")
            content->setSize (mode == "--snapshot-inbox" ? 1000 : picker ? 400 : 800, mode == "--snapshot-inbox" ? 460 : picker ? 560 : 580);

        const auto image = content->createComponentSnapshot (content->getLocalBounds());
        output.deleteFile();
        juce::FileOutputStream stream (output);
        juce::PNGImageFormat png;
        setApplicationReturnValue (stream.openedOk() && png.writeImageToStream (image, stream) ? 0 : 1);
        content = nullptr;
        quit();
    }

    /** Exercises the workbook / CSV sync in the isolated data folder (see CLAUDE.md). */
    bool selfTestCatalog()
    {
        // belt and braces: only ever wipe a folder that lives inside the WT_APPDATA test area
        const auto testRoot = juce::File (juce::SystemStats::getEnvironmentVariable ("WT_APPDATA", {}));
        const auto dir = Catalog::defaultDirectory();
        if (testRoot.getFullPathName().isEmpty() || ! dir.isAChildOf (testRoot))
        {
            std::printf ("FAIL  refusing to run: config folder is not inside WT_APPDATA\n");
            return false;
        }
        dir.deleteRecursively();
        (testRoot.getChildFile ("catalog_state.json")).deleteFile();

        bool ok = true;
        const auto check = [&ok] (bool cond, const char* what) {
            std::printf ("%s  %s\n", cond ? "PASS" : "FAIL", what);
            ok = ok && cond;
        };
        const auto jp = [] (const char* s) { return juce::String::fromUTF8 (s); };
        const auto csvText = [&] (const juce::File& f) {
            juce::MemoryBlock b;
            f.loadFileAsData (b);
            return decodeTextBytes (b);
        };

        // 1. first start
        catalog.setDirectory (dir);
        check (catalog.getWorkbookFile().existsAsFile() && catalog.getCategoriesFile().existsAsFile() && catalog.getTypesFile().existsAsFile(),
               "first start creates catalog.xlsx and both CSV files");
        const auto baseCats = catalog.getCategories().size();
        check (baseCats > 50, "seed categories round-trip through the workbook");
        check (catalog.getTypes ("TOOLS").size() > catalog.getTypes ("TOOLS", false).size(), "TOOLS has own + common types");
        check (catalog.getCategories()[0].jp.isNotEmpty() && csvText (catalog.getCategoriesFile()).contains (catalog.getCategories()[0].jp),
               "Japanese is intact in the workbook and in the generated CSV");

        // 2. app-side additions go into the workbook and flow out to the CSV
        check (catalog.addCategory ("ark", jp ("方舟")), "addCategory writes to the workbook");
        check (catalog.getCategories().size() == baseCats + 1 && catalog.getCategories().back().name == "ARK"
                   && catalog.getCategories().back().jp == jp ("方舟"),
               "new category reloaded (upper-cased, Japanese preserved)");
        check (csvText (catalog.getCategoriesFile()).contains (jp ("ARK,方舟")), "generated categories.csv contains the new row");
        check (catalog.addCategory ("ARK", "dup") && catalog.getCategories().size() == baseCats + 1, "duplicate category not added twice");

        const auto before = catalog.getTypes ("TOOLS", false).size();
        check (catalog.addType ("TOOLS", "Pry", jp ("こじ開ける, 剥がす")), "addType with a comma in the Japanese");
        const auto types = catalog.getTypes ("TOOLS", false);
        check (types.size() == before + 1 && types.back().name == "Pry" && types.back().jp == jp ("こじ開ける, 剥がす"),
               "comma-containing field survives the workbook round trip");
        check (csvText (catalog.getTypesFile()).contains (jp ("\"こじ開ける, 剥がす\"")), "CSV quoting of that field is correct");

        // 3. other sheets and formatting survive an app-side append
        {
            std::vector<xlsx::Sheet> sheets;
            juce::String error;
            xlsx::read (catalog.getWorkbookFile(), sheets, error);
            sheets.push_back ({ "Notes", { { "memo" }, { jp ("消えてはいけないメモ") } } });
            xlsx::write (catalog.getWorkbookFile(), sheets, error);
            catalog.reload();
            catalog.addType ("PAPER", "Flutter", jp ("はためく"));
            sheets.clear();
            xlsx::read (catalog.getWorkbookFile(), sheets, error);
            check (sheets.size() == 3 && sheets[2].name == "Notes" && sheets[2].rows.size() == 2 && sheets[2].rows[1][0] == jp ("消えてはいけないメモ"),
                   "appending a row leaves the other sheet untouched");
        }

        // 4. Excel's plain "CSV" (Shift-JIS) saved over categories.csv is read correctly and merged into the workbook
        {
            juce::MemoryBlock sjis = encodeShiftJis (jp ("Category,Japanese,Note\r\nSJIS_TEST,日本語テスト,\r\n"));
            catalog.getCategoriesFile().replaceWithData (sjis.getData(), sjis.getSize());
            check (! isUtf8Text (sjis), "test file really is Shift-JIS");
            check (catalog.reloadIfChanged(), "Shift-JIS CSV edit detected");

            bool found = false;
            for (const auto& c : catalog.getCategories())
                found = found || (c.name == "SJIS_TEST" && c.jp == jp ("日本語テスト"));
            check (found, "Japanese read correctly from the Shift-JIS file");

            juce::MemoryBlock regenerated;
            catalog.getCategoriesFile().loadFileAsData (regenerated);
            check (isUtf8Text (regenerated), "CSV normalised back to UTF-8");

            int backups = 0;
            for (const auto& f : dir.findChildFiles (juce::File::findFiles, false, "catalog.xlsx.bak-*"))
                ++backups, juce::ignoreUnused (f);
            check (backups >= 1, "previous workbook kept as a .bak file");

            std::vector<xlsx::Sheet> sheets;
            juce::String error;
            xlsx::read (catalog.getWorkbookFile(), sheets, error);
            bool inWorkbook = false;
            for (const auto& r : sheets[0].rows)
                inWorkbook = inWorkbook || (r.size() > 1 && r[0] == "SJIS_TEST");
            check (inWorkbook, "edited CSV content merged into the workbook");
        }

        // 5. editing the workbook (as Excel would) regenerates the CSVs
        {
            std::vector<xlsx::Sheet> sheets;
            juce::String error;
            xlsx::read (catalog.getWorkbookFile(), sheets, error);
            sheets[1].rows.push_back ({ "TOOLS", "ExcelRow", jp ("エクセルで追加"), "" });
            juce::Thread::sleep (1100); // make sure the modification time differs
            xlsx::write (catalog.getWorkbookFile(), sheets, error);
            check (catalog.reloadIfChanged(), "workbook edit detected");
            check (csvText (catalog.getTypesFile()).contains (jp ("TOOLS,ExcelRow,エクセルで追加")), "types.csv regenerated from the workbook");
        }

        // 6. Drive housekeeping: conflict copies are reported, old backups are pruned
        {
            dir.getChildFile ("catalog (1).xlsx").replaceWithText ("x");
            for (int i = 1; i <= 8; ++i)
                dir.getChildFile ("types.csv.bak-2026010" + juce::String (i) + "-000000").replaceWithText ("old");
            catalog.reload();

            bool flagged = false;
            for (const auto& w : catalog.getWarnings())
                flagged = flagged || w.contains ("catalog (1).xlsx");
            check (flagged, "Drive conflict copy is reported");
            check (dir.findChildFiles (juce::File::findFiles, false, "types.csv.bak-*").size() == 5, "old backups pruned to the newest 5");
            check (dir.getChildFile ("types.csv.bak-20260108-000000").existsAsFile() && ! dir.getChildFile ("types.csv.bak-20260101-000000").existsAsFile(),
                   "the newest backups are the ones kept");
        }

        // 7. copying a config folder never overwrites
        {
            const auto other = testRoot.getChildFile ("copy_target");
            other.deleteRecursively();
            other.createDirectory();
            other.getChildFile ("types.csv").replaceWithText ("KEEP");
            const auto copied = Catalog::copyConfigFiles (dir, other);
            check (copied >= 2 && other.getChildFile ("catalog.xlsx").existsAsFile(), "missing files are copied");
            check (other.getChildFile ("types.csv").loadFileAsString() == "KEEP", "an existing file is not overwritten");
            check (Catalog::copyConfigFiles (dir, other) == 0, "second copy has nothing left to copy");
        }
        return ok;
    }

    /** Shared vs per-PC settings, the unreadable-file guard and folder moves, using real MainComponents in the isolated folder. */
    bool selfTestPrefs()
    {
        const auto testRoot = juce::File (juce::SystemStats::getEnvironmentVariable ("WT_APPDATA", {}));
        if (testRoot.getFullPathName().isEmpty())
        {
            std::printf ("FAIL  refusing to run: WT_APPDATA is not set\n");
            return false;
        }

        bool ok = true;
        const auto check = [&ok] (bool cond, const char* what) {
            std::printf ("%s  %s\n", cond ? "PASS" : "FAIL", what);
            ok = ok && cond;
        };

        const auto reset = [&] {
            testRoot.getChildFile ("config").deleteRecursively();
            testRoot.getChildFile ("config2").deleteRecursively();
            testRoot.getChildFile ("config3").deleteRecursively();
            testRoot.getChildFile ("catalog_state.json").deleteFile();
        };
        const auto writeLocal = [&] (const juce::String& json) {
            testRoot.getChildFile ("settings.json").replaceWithText (json);
        };
        const juce::String roots = "\"libraryRoots\":[\"Z:\\\\none\"]"; // a folder that does not exist: no slow library scan

        // 1. an old settings.json (everything in one file) is split into shared + per-PC
        reset();
        writeLocal ("{\"cycle\":{\"prefix\":\"Legacy\",\"pattern\":\"{prefix}_{type}{num:02}\",\"category\":\"\",\"typeNames\":[\"a\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\"]},"
                    "\"keys\":{\"open\":[\"ctrl + P\"]},\"scheme\":2,\"exportDir\":\"X:\\\\out\"," + roots + "}");
        {
            MainComponent m;
            check (m.testCycles().getPrefix() == "Legacy", "legacy settings.json is still read on first start");
            m.testSaveNow();
        }
        const auto prefs = juce::JSON::parse (testRoot.getChildFile ("config").getChildFile ("preferences.json"));
        const auto local = juce::JSON::parse (testRoot.getChildFile ("settings.json"));
        check (prefs.getProperty ("cycle", {}).getProperty ("prefix", "").toString() == "Legacy" && (int) prefs.getProperty ("scheme", 0) == 2,
               "shared settings written to preferences.json in the config folder");
        check (prefs.getProperty ("keys", {}).getProperty ("open", {}).getArray() != nullptr, "shortcuts are in the shared file");
        check (local.getProperty ("exportDir", "").toString() == "X:\\out" && local.hasProperty ("libraryRoots") && local.hasProperty ("configDir"),
               "per-PC settings.json keeps export folder, library roots and config folder");
        check (! local.hasProperty ("keys") && ! local.hasProperty ("cycle") && ! local.hasProperty ("scheme"), "shared items are no longer in the per-PC file");

        // 2. a preferences file that cannot be read (Drive mid-sync) is never overwritten by defaults
        const auto prefsFile = testRoot.getChildFile ("config").getChildFile ("preferences.json");
        prefsFile.replaceWithText ("{ broken json");
        {
            MainComponent m;
            check (m.testPrefsLoadFailed(), "unreadable preferences.json is detected");
            m.testSaveNow();
        }
        check (prefsFile.loadFileAsString() == "{ broken json", "and it is left untouched");

        // 3. moving to another folder copies, adopts what is already there, and keeps the old folder
        reset();
        writeLocal ("{" + roots + "}");
        {
            MainComponent m;
            m.testSaveNow();
            const auto oldDir = m.testCatalog().getDirectory();
            const auto newDir = testRoot.getChildFile ("config2");
            const auto message = m.testChangeFolder (newDir);
            check (m.testCatalog().getDirectory() == newDir, "config folder switched");
            check (newDir.getChildFile ("catalog.xlsx").existsAsFile() && newDir.getChildFile ("preferences.json").existsAsFile(), "workbook and preferences copied");
            check (oldDir.getChildFile ("catalog.xlsx").existsAsFile(), "old folder keeps its files");
            check (message.isNotEmpty(), "a message is returned for the dialog");

            // a folder that another PC already set up wins
            Catalog other;
            other.setDirectory (testRoot.getChildFile ("config3"));
            other.addCategory ("ONLYTHERE", "x");
            m.testChangeFolder (testRoot.getChildFile ("config3"));
            bool adopted = false;
            for (const auto& c : m.testCatalog().getCategories())
                adopted = adopted || c.name == "ONLYTHERE";
            check (adopted, "an existing workbook in the chosen folder is adopted, not overwritten");
        }

        // 4. choosing a Category brings back that Category's Prefix and Type names
        reset();
        writeLocal ("{" + roots + "}");
        {
            check (CategoryProfiles::defaultPrefix ("TOOLS") == "Tools" && CategoryProfiles::defaultPrefix ("SCI_FI") == "SciFi"
                       && CategoryProfiles::defaultPrefix ("UI") == "Ui",
                   "default prefix is the Category name in Title case");
            CategoryProfiles empty;
            check (empty.resolve ("X", "Custom", {}).prefix == "Custom" && empty.resolve ("X", "", {}).prefix == "X",
                   "a Prefix column value in the workbook wins over the default");

            MainComponent m;
            const auto names = [&m] {
                juce::StringArray out;
                for (int i = 0; i < CycleModel::numSlots; ++i)
                    out.add (m.testCycles().getType (i).name);
                return out;
            };

            m.testChooseCategoryNoDialog ("TOOLS");
            const auto toolsTypes = m.testCatalog().getTypes ("TOOLS");
            check (m.testCycles().getPrefix() == "Tools" && m.testCycles().getCategory() == "TOOLS", "first use: Prefix = Tools, Category set");
            check (names()[0] == toolsTypes[0].name && names()[8] == toolsTypes[8].name, "first use: Type slots filled with the first nine candidates");

            m.testCyclesMutable().setPrefix ("TL");
            m.testCyclesMutable().setTypeName (0, "Mine");
            check (m.testProfiles().find ("TOOLS") != nullptr && m.testProfiles().find ("TOOLS")->prefix == "TL"
                       && m.testProfiles().find ("TOOLS")->types[0] == "Mine",
                   "edits are remembered under the current Category");

            m.testChooseCategoryNoDialog ("PAPER");
            const auto paperTypes = m.testCatalog().getTypes ("PAPER");
            check (m.testCycles().getPrefix() == "Paper" && names()[0] == paperTypes[0].name, "switching Category switches Prefix and Types");
            check (m.testProfiles().find ("TOOLS")->prefix == "TL" && m.testProfiles().find ("TOOLS")->types[0] == "Mine",
                   "the previous Category's settings were not overwritten by the switch");

            m.testChooseCategoryNoDialog ("TOOLS");
            check (m.testCycles().getPrefix() == "TL" && names()[0] == "Mine", "coming back restores what was used before");
            m.testSaveNow();
        }
        {
            MainComponent m2; // a new session (or another PC) reads the shared preferences
            check (m2.testProfiles().find ("TOOLS") != nullptr && m2.testProfiles().find ("TOOLS")->prefix == "TL"
                       && m2.testProfiles().find ("PAPER") != nullptr && m2.testProfiles().find ("PAPER")->prefix == "Paper",
                   "Category settings survive a restart (shared preferences)");
        }
        return ok;
    }

    /** Marker embedding: audio untouched, other chunks kept, markers readable, bad files refused. */
    bool selfTestWav()
    {
        const auto testRoot = juce::File (juce::SystemStats::getEnvironmentVariable ("WT_APPDATA", {}));
        if (testRoot.getFullPathName().isEmpty())
        {
            std::printf ("FAIL  refusing to run: WT_APPDATA is not set\n");
            return false;
        }

        bool ok = true;
        const auto check = [&ok] (bool cond, const char* what) {
            std::printf ("%s  %s\n", cond ? "PASS" : "FAIL", what);
            ok = ok && cond;
        };

        const auto dir = testRoot.getChildFile ("wav");
        dir.deleteRecursively();
        dir.createDirectory();

        // a 24-bit stereo test file that already carries an INFO comment (must survive)
        const auto src = dir.getChildFile ("src.wav");
        {
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> stream (src.createOutputStream());
            std::unordered_map<juce::String, juce::String> meta;
            meta[juce::WavAudioFormat::riffInfoComment] = "original comment";
            auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions {}.withSampleRate (48000.0).withNumChannels (2)
                                                          .withBitsPerSample (24).withMetadataValues (meta));
            juce::AudioBuffer<float> buf (2, 48000 * 2 + 7); // odd length on purpose
            juce::Random rnd (1234);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < buf.getNumSamples(); ++i)
                    buf.setSample (ch, i, rnd.nextFloat() * 1.6f - 0.8f);
            writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
        }
        const auto srcBackup = dir.getChildFile ("src_before.wav");
        src.copyFileTo (srcBackup);
        check (listChunkIds (src).contains ("LIST:INFO"), "test file has an INFO chunk to preserve");

        // cycles through the real model so buildEmbedRequest is covered too
        CycleModel model;
        model.setTypeName (0, "push");
        model.setTypeName (1, "pull");
        model.setPrefix ("pen");
        model.add (4800, 24000, 0);
        model.add (30000, 41001, 1);
        model.add (50001, 60000, -1);
        const auto request = buildEmbedRequest (model, 48000.0, "test");

        const auto readAll = [] (const juce::File& f, juce::AudioBuffer<float>& out, juce::StringPairArray* meta = nullptr) {
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatReader> r (wav.createReaderFor (f.createInputStream().release(), true));
            if (r == nullptr)
                return false;
            out.setSize ((int) r->numChannels, (int) r->lengthInSamples);
            r->read (&out, 0, (int) r->lengthInSamples, 0, true, true);
            if (meta != nullptr)
                *meta = r->metadataValues;
            return true;
        };

        // 1. copy + embed
        const auto dest = dir.getChildFile ("dest.wav");
        const auto result = embedMarkers (src, dest, request);
        check (result.ok && result.regionsWritten == 3, "embed into a copy succeeds");
        const auto ids = listChunkIds (dest);
        check (ids.contains ("fmt ") && ids.contains ("data") && ids.contains ("cue ") && ids.contains ("LIST:adtl") && ids.contains ("wtag"),
               "cue, adtl and wtag chunks are present");
        check (ids.contains ("LIST:INFO"), "the existing INFO chunk is kept");
        check (src.hasIdenticalContentTo (srcBackup), "the source file was not touched");

        juce::AudioBuffer<float> a, b;
        juce::StringPairArray metaSrc, meta;
        check (readAll (src, a, &metaSrc) && readAll (dest, b, &meta) && a.getNumSamples() == b.getNumSamples(), "same length after embedding");
        bool identical = a.getNumChannels() == b.getNumChannels();
        for (int ch = 0; identical && ch < a.getNumChannels(); ++ch)
            for (int i = 0; identical && i < a.getNumSamples(); ++i)
                identical = a.getSample (ch, i) == b.getSample (ch, i);
        check (identical, "every audio sample is identical");
        bool allKept = metaSrc.size() > 0;
        for (const auto& key : metaSrc.getAllKeys()) // the reader also lists the new cue points, so compare what was there before
            allKept = allKept && meta.getValue (key, "<missing>") == metaSrc.getValue (key, "");
        check (allKept, "every metadata entry the file had before still reads back the same");

        // 2. markers are readable like RX would (cue + labl + ltxt)
        const auto markers = readWavMarkers (dest);
        check (markers.size() == 3 && markers[0].label == "pen_push01" && markers[0].start == 4800 && markers[0].length == 19200
                   && markers[1].label == "pen_pull01" && markers[1].start == 30000 && markers[1].length == 11001
                   && markers[2].label == "pen_untyped01" && markers[2].length == 9999,
               "regions come back with names, starts and lengths (including odd-length labels)");

        // 3. the wtag chunk restores the full state
        juce::var json;
        WtagData data;
        check (readWtag (dest, json) && parseWtag (json, data) && data.cycles.size() == 3 && data.cycles[0].typeName == "push"
                   && data.cycles[2].typeName.isEmpty() && data.schema == currentWtagSchema && data.savedAt > 0,
               "wtag chunk parses: 3 cycles, type names, schema, timestamp");
        dest.copyFileTo (dir.getChildFile ("keep_for_python.wav"));

        // 4. embedding again replaces, never duplicates; in place is allowed
        EmbedRequest second;
        second.regions = { { 100, 200, "x" }, { 1000, 50, "yy" } };
        second.wtagJson = "{\"schema\":1,\"cycles\":[]}";
        check (embedMarkers (dest, dest, second).ok, "embedding in place succeeds");
        const auto ids2 = listChunkIds (dest);
        const auto countOf = [] (const juce::StringArray& a, const juce::String& id) {
            int n = 0;
            for (const auto& x : a)
                n += x == id ? 1 : 0;
            return n;
        };
        check (countOf (ids2, "cue ") == 1 && countOf (ids2, "wtag") == 1 && countOf (ids2, "LIST:adtl") == 1 && readWavMarkers (dest).size() == 2,
               "old markers were replaced, not duplicated");
        juce::AudioBuffer<float> c;
        readAll (dest, c);
        identical = c.getNumSamples() == a.getNumSamples();
        for (int ch = 0; identical && ch < a.getNumChannels(); ++ch)
            for (int i = 0; identical && i < a.getNumSamples(); ++i)
                identical = a.getSample (ch, i) == c.getSample (ch, i);
        check (identical, "audio still identical after the in-place rewrite");

        // 5. an empty request removes the markers (and still verifies)
        check (embedMarkers (dest, dest, {}).ok && ! listChunkIds (dest).contains ("cue ") && ! listChunkIds (dest).contains ("wtag"),
               "an empty request cleanly removes markers");

        // 6. files that must be refused are left alone
        {
            const auto rf64 = dir.getChildFile ("rf64.wav");
            juce::MemoryOutputStream o;
            o.write ("RF64", 4);
            o.writeInt (-1);
            o.write ("WAVE", 4);
            o.write ("ds64", 4);
            rf64.replaceWithData (o.getData(), o.getDataSize());
            const auto out = dir.getChildFile ("rf64_out.wav");
            const auto r = embedMarkers (rf64, out, request);
            check (! r.ok && ! out.exists(), "RF64 is refused and nothing is written");

            const auto trunc = dir.getChildFile ("trunc.wav");
            src.copyFileTo (trunc);
            {
                juce::FileOutputStream t (trunc);
                t.setPosition (trunc.getSize() - 50);
                t.truncate();
            }
            const auto before = dir.getChildFile ("trunc_before.wav");
            trunc.copyFileTo (before);
            const auto r2 = embedMarkers (trunc, trunc, request);
            check (! r2.ok && trunc.hasIdenticalContentTo (before), "a truncated file is refused and left exactly as it was");
            check (dir.findChildFiles (juce::File::findFiles, false, "*.wtmp~").isEmpty(), "no temporary files are left behind");

            const auto notWav = dir.getChildFile ("note.wav");
            notWav.replaceWithText ("hello");
            check (! embedMarkers (notWav, notWav, request).ok && notWav.loadFileAsString() == "hello", "a non-WAV file is refused and unchanged");
        }

        // 6b. WAV vs local project: the later save wins
        check (preferEmbedded (true, 2000, false, 0) && preferEmbedded (true, 2000, true, 1000) && ! preferEmbedded (true, 1000, true, 2000)
                   && ! preferEmbedded (false, 5000, true, 1000) && ! preferEmbedded (false, 0, false, 0),
               "WAV information wins only when it is newer than this PC's project (or there is no project)");

        // 7. restoring into the palette matches types by NAME and never loses one
        {
            testRoot.getChildFile ("settings.json").replaceWithText ("{\"libraryRoots\":[\"Z:\\\\none\"]}");
            MainComponent m;
            auto& cy = m.testCyclesMutable();
            cy.setTypeName (0, "pull");
            cy.setTypeName (1, "open");
            cy.setTypeName (2, "write");
            for (int i = 3; i < CycleModel::numSlots; ++i)
                cy.setTypeName (i, {});

            WtagData d;
            d.schema = 1;
            d.cycles = { { 0, 100, 2, "write", "" }, { 200, 300, 0, "PULL", "" }, { 400, 500, 7, "Hammer", "" }, { 600, 700, -1, "", "" } };
            const auto added = m.testApplyEmbedded (d);
            check (added == 1, "a type the palette lacks is added (once)");
            check (m.testCycles()[0].type == 2 && m.testCycles()[1].type == 0 && m.testCycles()[2].type == 3 && m.testCycles()[3].type == -1,
                   "types are matched by name (case-insensitive), not by the stored slot number");
            check (m.testCycles().getType (3).name == "Hammer", "the new type name took the first free slot");
        }
        return ok;
    }

    /** Inbox: scanning, import pipeline, labels from folders, failure handling, queue. */
    bool selfTestInbox()
    {
        const auto testRoot = juce::File (juce::SystemStats::getEnvironmentVariable ("WT_APPDATA", {}));
        if (testRoot.getFullPathName().isEmpty())
        {
            std::printf ("FAIL  refusing to run: WT_APPDATA is not set\n");
            return false;
        }

        bool ok = true;
        const auto check = [&ok] (bool cond, const char* what) {
            std::printf ("%s  %s\n", cond ? "PASS" : "FAIL", what);
            ok = ok && cond;
        };

        const auto base = testRoot.getChildFile ("inbox_test");
        base.deleteRecursively();
        const auto inbox = base.getChildFile ("inbox");
        const auto raw = base.getChildFile ("raw");
        inbox.getChildFile ("TOOLS/Open").createDirectory();
        inbox.getChildFile ("_Done").createDirectory();
        raw.createDirectory();

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();

        // N noise bursts (0.3 s each) over a very quiet floor, stereo 24-bit
        const auto makeWav = [&] (const juce::File& f, int bursts, bool flac = false) {
            juce::AudioBuffer<float> buf (2, 48000 * (bursts + 1));
            juce::Random rnd (99);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < buf.getNumSamples(); ++i)
                    buf.setSample (ch, i, (rnd.nextFloat() - 0.5f) * 0.001f);
            for (int b = 0; b < bursts; ++b)
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 14400; ++i)
                        buf.setSample (ch, 28800 + b * 48000 + i, (rnd.nextFloat() - 0.5f) * 1.0f);

            std::unique_ptr<juce::OutputStream> stream (f.createOutputStream());
            std::unique_ptr<juce::AudioFormat> format (flac ? static_cast<juce::AudioFormat*> (new juce::FlacAudioFormat()) : new juce::WavAudioFormat());
            auto writer = format->createWriterFor (stream, juce::AudioFormatWriterOptions {}.withSampleRate (48000.0).withNumChannels (2)
                                                              .withBitsPerSample (flac ? 16 : 24));
            writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
        };
        const auto readAll = [&] (const juce::File& f, juce::AudioBuffer<float>& out) {
            std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (f));
            if (r == nullptr)
                return false;
            out.setSize ((int) r->numChannels, (int) r->lengthInSamples);
            return r->read (&out, 0, (int) r->lengthInSamples, 0, true, true);
        };

        const auto take1 = inbox.getChildFile ("TOOLS/Open/take1.wav");
        const auto plain = inbox.getChildFile ("plain.wav");
        const auto music = inbox.getChildFile ("music.flac");
        const auto bad = inbox.getChildFile ("bad.wav");
        makeWav (take1, 3);
        makeWav (plain, 2);
        makeWav (music, 1, true);
        bad.replaceWithText (juce::String::repeatedString ("this is not audio ", 20));
        inbox.getChildFile ("~lock.wav").replaceWithText (juce::String::repeatedString ("x", 100));
        inbox.getChildFile ("_Done/old.wav").replaceWithText (juce::String::repeatedString ("x", 100));
        inbox.getChildFile ("half.wav.tmp").replaceWithText (juce::String::repeatedString ("x", 100));

        // 1. candidates and stability
        const auto candidates = listInboxCandidates (inbox);
        check (candidates.size() == 4, "temp files, ~ files and _Done are not candidates (4 real recordings)");

        InboxScanState state;
        check (scanForReady (inbox, state).empty() && scanForReady (inbox, state).empty(), "a file is not ready after 1 or 2 scans");
        plain.appendText ("");           // same content, but touch the timestamp so it counts as changed
        juce::Thread::sleep (30);
        plain.setLastModificationTime (juce::Time::getCurrentTime() + juce::RelativeTime::seconds (5));
        const auto ready = scanForReady (inbox, state);
        check (ready.size() == 3 && std::find (ready.begin(), ready.end(), plain) == ready.end(), "a file that changed between scans starts waiting again");
        scanForReady (inbox, state); // the touched file needs three unchanged scans in a row, like every other
        check (scanForReady (inbox, state).size() == 4, "all four are ready once they stay unchanged");
        state.ignored.insert (InboxScanState::signature (music));
        check (scanForReady (inbox, state).size() == 3, "files on the ignore list are skipped");
        state.ignored.clear();

        // 2. the full pipeline with folder labels
        InboxOptions options;
        options.inbox = inbox;
        options.raw = raw;
        options.categoryNames = { "TOOLS", "PAPER" };

        const auto original = juce::File (take1);
        juce::AudioBuffer<float> before;
        readAll (original, before);

        const auto r1 = processInboxFile (take1, options, formats);
        check (r1.ok && r1.cycles == 3, "take1.wav imported: 3 bursts found");
        check (r1.categoryHint == "TOOLS" && r1.typeHint == "Open", "folders TOOLS/Open became Category and Type hints");
        check (r1.rawFile.existsAsFile() && r1.rawFile.getParentDirectory().getParentDirectory() == raw, "a copy exists in Raw/<year>/");
        check (! take1.existsAsFile() && r1.archived.existsAsFile() && r1.archived.getFullPathName().contains ("_Done"), "the original moved to _Done only after success");
        check (r1.archived.getFullPathName().replaceCharacter ('\\', '/').contains ("_Done/TOOLS/Open/"), "_Done keeps the folder structure");

        juce::AudioBuffer<float> after;
        bool same = readAll (r1.rawFile, after) && after.getNumSamples() == before.getNumSamples();
        for (int ch = 0; same && ch < before.getNumChannels(); ++ch)
            for (int i = 0; same && i < before.getNumSamples(); ++i)
                same = before.getSample (ch, i) == after.getSample (ch, i);
        check (same, "the Raw copy's audio is identical to the recording");

        juce::var wtag;
        WtagData data;
        check (readWtag (r1.rawFile, wtag) && parseWtag (wtag, data) && data.cycles.size() == 3 && data.cycles[0].typeName == "Open"
                   && data.cycles[0].name.startsWith ("Tools_Open") && wtag.getProperty ("category", "").toString() == "TOOLS",
               "markers are embedded: 3 cycles typed Open, named Tools_Open01..., Category TOOLS");
        check (readWavMarkers (r1.rawFile).size() == 3, "and visible as ordinary cue regions");

        // 3. a second file with the same name never overwrites the first
        makeWav (take1, 1);
        const auto r2 = processInboxFile (take1, options, formats);
        check (r2.ok && r2.rawFile != r1.rawFile && r1.rawFile.existsAsFile(), "same name again: stored as take1_1.wav, nothing overwritten");

        // 4. other formats are converted to WAV; plain files have no hints
        const auto r3 = processInboxFile (music, options, formats);
        check (r3.ok && r3.rawFile.hasFileExtension ("wav") && r3.typeHint.isEmpty() && r3.categoryHint.isEmpty() && r3.cycles == 1,
               "a FLAC is converted to a 24-bit WAV, no hints without folders, 1 burst found");

        // 5. failures leave everything where it was
        const auto rawFilesBefore = raw.findChildFiles (juce::File::findFiles, true, "*").size();
        const auto r4 = processInboxFile (bad, options, formats);
        check (! r4.ok && r4.error.isNotEmpty() && bad.existsAsFile(), "an unreadable file is reported and stays in the inbox");
        check (raw.findChildFiles (juce::File::findFiles, true, "*").size() == rawFilesBefore, "a failed import leaves nothing behind in Raw");
        check (raw.findChildFiles (juce::File::findFiles, true, "*.part~").isEmpty() && raw.findChildFiles (juce::File::findFiles, true, "*.wtmp~").isEmpty(),
               "no temporary files remain");

        InboxOptions noRaw = options;
        noRaw.raw = juce::File();
        check (! processInboxFile (plain, noRaw, formats).ok && plain.existsAsFile(), "without a Raw folder nothing is moved");

        // 6. queue
        InboxQueue queue;
        queue.add (r1);
        queue.add (r4);
        check (queue.size() == 2 && queue.countNew() == 1 && queue.getItems()[0].status == "failed", "queue: newest first, one new, one failed");
        queue.markReviewed (r1.rawFile.getFullPathName());
        check (queue.countNew() == 0 && queue.getItems()[1].status == "reviewed", "marking reviewed by Raw path");
        const auto queueFile = base.getChildFile ("queue.json");
        queue.save (queueFile);
        InboxQueue loaded;
        loaded.load (queueFile);
        check (loaded.size() == 2 && loaded.getItems()[1].category == "TOOLS" && loaded.getItems()[1].cycles == 3, "queue survives a restart");

        // 7. auto-typing with what has been learned, and the order a person should review in
        {
            const auto auto1 = inbox.getChildFile ("auto1.wav");
            makeWav (auto1, 3);
            juce::AudioBuffer<float> mono;
            readAll (auto1, mono);
            AudioData ad;
            ad.sampleRate = 48000.0;
            ad.buffer = mono;
            const auto segs = detectSegments (ad, 0, ad.getNumSamples(), options.split);
            bool gotFeatures = segs.size() == 3;
            LabelStore labels (base.getChildFile ("labels"), "t");
            for (size_t si = 0; si < segs.size(); ++si)
                for (int i = 0; i < 3; ++i)
                {
                    LabelSample ls;
                    FeatureVector fv {};
                    gotFeatures = gotFeatures && computeSegmentFeatures (mono, 48000.0, segs[si].first, segs[si].second, fv);
                    ls.key = "k" + juce::String ((int) si * 3 + i);
                    ls.fileName = "k.wav";
                    ls.v = fv;
                    for (auto& x : ls.v)
                        x += 0.001f * (float) i;
                    ls.category = "TOOLS";
                    ls.type = "Burst";
                    labels.upsert (ls);
                }
            auto sug = std::make_shared<Suggester>();
            sug->rebuild (labels);

            InboxOptions learned = options;
            learned.suggester = sug;
            const auto ra = processInboxFile (auto1, learned, formats);
            check (gotFeatures && ra.ok && ra.cycles == 3 && ra.autoTyped == 3 && ra.uncertain == 0 && ra.minConfidence > 0.0f,
                   "with learned examples the 3 bursts are typed automatically (and reported)");
            juce::var wt;
            WtagData wd;
            check (readWtag (ra.rawFile, wt) && parseWtag (wt, wd) && wd.cycles.size() == 3 && wd.cycles[0].typeName == "Burst" && ! wd.cycles[0].confirmed,
                   "they are embedded as type Burst, marked not yet looked at");

            const auto none = inbox.getChildFile ("auto2.wav");
            makeWav (none, 2);
            const auto rb = processInboxFile (none, options, formats);
            check (rb.ok && rb.autoTyped == 0 && rb.uncertain == 2 && rb.minConfidence < 0.0f, "without examples nothing is typed: everything is left for a person");

            InboxQueue q2;
            auto done = ra;
            done.minConfidence = 0.95f;
            auto unsure = ra;
            unsure.minConfidence = 0.4f;
            unsure.uncertain = 1;
            unsure.rawFile = raw.getChildFile ("u.wav");
            auto nothing = rb;
            nothing.rawFile = raw.getChildFile ("n.wav");
            q2.add (done);     // index 2 after all three are added
            q2.add (unsure);   // 1
            q2.add (nothing);  // 0 (newest)
            const auto order = q2.reviewOrder();
            check (order.size() == 3 && q2.getItems()[(size_t) order[0]].raw == nothing.rawFile.getFullPathName() && q2.getItems()[(size_t) order[1]].raw == unsure.rawFile.getFullPathName()
                       && q2.getItems()[(size_t) order[2]].raw == done.rawFile.getFullPathName(),
                   "review order: nothing known first, then least certain, then fully typed");
        }
        return ok;
    }

    /** The real watcher thread: ignores the existing backlog (also after a restart), picks up a new recording on its own. */
    bool selfTestWatcher()
    {
        const auto testRoot = juce::File (juce::SystemStats::getEnvironmentVariable ("WT_APPDATA", {}));
        if (testRoot.getFullPathName().isEmpty())
        {
            std::printf ("FAIL  refusing to run: WT_APPDATA is not set\n");
            return false;
        }

        bool ok = true;
        const auto check = [&ok] (bool cond, const char* what) {
            std::printf ("%s  %s\n", cond ? "PASS" : "FAIL", what);
            ok = ok && cond;
        };

        const auto base = testRoot.getChildFile ("watcher_test");
        base.deleteRecursively();
        const auto inbox = base.getChildFile ("inbox");
        const auto raw = base.getChildFile ("raw");
        inbox.getChildFile ("TOOLS/Open").createDirectory();
        raw.createDirectory();

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();

        const auto makeWav = [] (const juce::File& f, int bursts) {
            juce::AudioBuffer<float> buf (2, 48000 * (bursts + 1));
            juce::Random rnd (7);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < buf.getNumSamples(); ++i)
                    buf.setSample (ch, i, (rnd.nextFloat() - 0.5f) * 0.001f);
            for (int b = 0; b < bursts; ++b)
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 14400; ++i)
                        buf.setSample (ch, 28800 + b * 48000 + i, rnd.nextFloat() - 0.5f);
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> stream (f.createOutputStream());
            auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions {}.withSampleRate (48000.0).withNumChannels (2).withBitsPerSample (24));
            writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
        };

        const auto stateFile = base.getChildFile ("ignored.json");
        const auto old = inbox.getChildFile ("old_backlog.wav");
        makeWav (old, 2);

        const auto waitFor = [] (const std::function<bool()>& done, juce::RelativeTime limit, const std::function<void()>& poke) {
            const auto end = juce::Time::getMillisecondCounterHiRes() + limit.inMilliseconds();
            while (! done() && juce::Time::getMillisecondCounterHiRes() < end)
            {
                juce::MessageManager::getInstance()->runDispatchLoopUntil (250);
                poke();
            }
            return done();
        };

        std::vector<InboxOutcome> outcomes;
        {
            InboxWatcher watcher (formats);
            watcher.onOutcome = [&outcomes] (const InboxOutcome& o) { outcomes.push_back (o); };

            InboxWatcher::Config cfg;
            cfg.inbox = inbox;
            cfg.raw = raw;
            cfg.categoryNames = { "TOOLS" };
            cfg.stateFile = stateFile;
            cfg.enabled = false;
            watcher.configure (cfg);
            check (watcher.ignoreExisting() == 1, "the recording already in the inbox is put on the ignore list");
            cfg.enabled = true;
            watcher.configure (cfg);

            makeWav (inbox.getChildFile ("TOOLS/Open/new1.wav"), 3);
            const auto got = waitFor ([&] { return ! outcomes.empty(); }, juce::RelativeTime::seconds (40), [&] { watcher.scanNow(); });
            check (got && outcomes[0].ok && outcomes[0].cycles == 3 && outcomes[0].typeHint == "Open", "a new recording is imported by the watcher on its own");
            check (old.existsAsFile(), "the existing backlog file was not touched");
            check (outcomes.size() == 1, "and nothing else was imported");
            check (! inbox.getChildFile ("TOOLS/Open/new1.wav").existsAsFile() && outcomes[0].archived.existsAsFile(), "the new recording was archived to _Done");
        }

        check (stateFile.existsAsFile() && stateFile.loadFileAsString().contains ("old_backlog.wav"), "the ignore list is written to disk");

        // "restart": a fresh watcher with the same state file must not import the backlog either
        {
            std::vector<InboxOutcome> again;
            InboxWatcher watcher (formats);
            watcher.onOutcome = [&again] (const InboxOutcome& o) { again.push_back (o); };
            InboxWatcher::Config cfg;
            cfg.inbox = inbox;
            cfg.raw = raw;
            cfg.categoryNames = { "TOOLS" };
            cfg.stateFile = stateFile;
            cfg.enabled = true;
            watcher.configure (cfg);

            waitFor ([&] { return ! again.empty(); }, juce::RelativeTime::seconds (9), [&] { watcher.scanNow(); });
            check (again.empty() && old.existsAsFile(), "after a restart the backlog is still left alone");

            // a failed file is reported once, not retried in a loop
            inbox.getChildFile ("broken.wav").replaceWithText (juce::String::repeatedString ("not audio ", 30));
            waitFor ([&] { return ! again.empty(); }, juce::RelativeTime::seconds (30), [&] { watcher.scanNow(); });
            check (again.size() == 1 && ! again[0].ok, "a broken file is reported as failed");
            waitFor ([] { return false; }, juce::RelativeTime::seconds (7), [&] { watcher.scanNow(); });
            check (again.size() == 1 && inbox.getChildFile ("broken.wav").existsAsFile(), "and is neither retried in a loop nor removed");
        }
        return ok;
    }

    /** Library re-organisation: features, grouping, the re-classification database and hard-link views. */
    bool selfTestOrganize()
    {
        const auto testRoot = juce::File (juce::SystemStats::getEnvironmentVariable ("WT_APPDATA", {}));
        if (testRoot.getFullPathName().isEmpty())
        {
            std::printf ("FAIL  refusing to run: WT_APPDATA is not set\n");
            return false;
        }

        bool ok = true;
        const auto check = [&ok] (bool cond, const char* what) {
            std::printf ("%s  %s\n", cond ? "PASS" : "FAIL", what);
            ok = ok && cond;
        };

        const auto base = testRoot.getChildFile ("organize_test");
        base.deleteRecursively();
        const auto lib = base.getChildFile ("legacy_lib");
        lib.createDirectory();

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();

        // three very different kinds of sound, eight variations each, deliberately mixed under neutral names
        enum Kind { impact = 0, tone = 1, rumble = 2 };
        const auto synth = [] (Kind kind, int variant, double sr) {
            juce::Random rnd (1000 + variant * 17 + (int) kind * 101);
            const auto seconds = kind == impact ? 0.5 + 0.03 * variant : kind == tone ? 2.0 + 0.1 * variant : 3.0 + 0.1 * variant;
            juce::AudioBuffer<float> b (1, (int) (seconds * sr));
            float lp = 0.0f;
            const auto freq = 330.0 + 55.0 * variant;
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const auto t = (double) i / sr;
                float v = 0.0f;
                if (kind == impact)
                    v = (rnd.nextFloat() * 2.0f - 1.0f) * (float) std::exp (-t * (18.0 + variant));
                else if (kind == tone)
                    v = 0.4f * (float) (std::sin (juce::MathConstants<double>::twoPi * freq * t) * std::min (1.0, t * 8.0));
                else
                {
                    lp += 0.015f * ((rnd.nextFloat() * 2.0f - 1.0f) - lp);
                    v = lp * 8.0f;
                }
                b.setSample (0, i, juce::jlimit (-1.0f, 1.0f, v));
            }
            return b;
        };

        std::vector<juce::File> files;
        std::vector<int> truth;
        {
            int n = 0;
            for (int variant = 0; variant < 8; ++variant)
                for (int kind = 0; kind < 3; ++kind)
                {
                    const auto f = lib.getChildFile ("Sound_" + juce::String (++n).paddedLeft ('0', 2) + ".wav");
                    juce::WavAudioFormat wav;
                    std::unique_ptr<juce::OutputStream> stream (f.createOutputStream());
                    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions {}.withSampleRate (44100.0).withNumChannels (1).withBitsPerSample (16));
                    const auto buf = synth ((Kind) kind, variant, 44100.0);
                    writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
                    files.push_back (f);
                    truth.push_back (kind);
                }
        }

        // 1. features
        FeatureVector a {}, b {};
        check (computeFeatures (formats, files[0], a) && computeFeatures (formats, files[0], b) && a == b, "features are deterministic");
        bool finite = true;
        for (const auto& f : files)
        {
            FeatureVector v {};
            finite = finite && computeFeatures (formats, f, v);
            for (auto x : v)
                finite = finite && std::isfinite (x);
        }
        check (finite, "all 24 files give finite features");
        check (! computeFeatures (formats, base.getChildFile ("nope.wav"), a), "an unreadable file is reported, not guessed");

        // 2. analyzer + resumable store
        const auto storeFile = base.getChildFile ("features.bin");
        {
            FeatureStore store (storeFile);
            store.load();
            FeatureAnalyzer analyzer (formats, store);
            analyzer.start (files);
            for (int i = 0; i < 600 && analyzer.isRunning(); ++i)
                juce::Thread::sleep (100);
            check (! analyzer.isRunning() && analyzer.getDone() == 24 && store.size() == 24 && analyzer.getFailed() == 0, "analyzer processes all files in the background");
        }
        {
            FeatureStore store (storeFile);
            store.load();
            check (store.size() == 24 && store.isCurrent (files[3]), "features survive a restart (loaded from disk)");

            const auto before = store.size();
            FeatureAnalyzer analyzer (formats, store);
            analyzer.start (files);
            for (int i = 0; i < 100 && analyzer.isRunning(); ++i)
                juce::Thread::sleep (50);
            check (store.size() == before, "a second run skips files that are already current");

            files[3].setLastModificationTime (juce::Time::getCurrentTime() + juce::RelativeTime::seconds (30));
            check (! store.isCurrent (files[3]), "a changed file is no longer current (it will be re-analysed)");
            files[3].setLastModificationTime (juce::Time::getCurrentTime());
        }

        // 3. grouping
        std::vector<FeatureVector> vectors;
        {
            FeatureStore store (storeFile);
            store.load();
            for (const auto& f : files)
                vectors.push_back (store.find (f)->v);
        }
        const auto data = normalise (vectors);
        const auto clusters = kmeans (data, 3);

        bool pure = true;
        std::set<int> used;
        for (int kind = 0; kind < 3; ++kind)
        {
            std::set<int> in;
            for (size_t i = 0; i < truth.size(); ++i)
                if (truth[i] == kind)
                    in.insert (clusters.assignment[i]);
            pure = pure && in.size() == 1;
            used.insert (*in.begin());
        }
        check (pure && used.size() == 3, "k-means separates impacts, tones and rumbles perfectly");
        const auto k = suggestK (data);
        std::printf ("INFO  suggested k = %d (3 kinds were generated)\n", k);
        check (k == 3, "the suggested number of groups is 3");
        const auto sil = silhouette (data, clusters.assignment, 3);
        std::printf ("INFO  silhouette = %.3f\n", sil);
        check (clusters.distance.size() == data.size() && sil > 0.3, "groups are well separated (silhouette > 0.3)");
        const auto again = kmeans (data, 3);
        check (again.assignment == clusters.assignment, "grouping is repeatable");

        // 4. re-classification database (library files are never touched)
        const juce::StringArray roots { lib.getFullPathName() };
        ReclassDb db;
        const auto key0 = ReclassDb::makeKey (roots, files[0]);
        check (key0 == "legacy_lib/Sound_01.wav", "key = root folder name + relative path");
        db.assign (key0, "TOOLS", "Hit", "group 1");
        db.assign (ReclassDb::makeKey (roots, files[1]), "PAPER", "Tear", "group 2");
        const auto dbFile = base.getChildFile ("reclass.json");
        db.save (dbFile);
        ReclassDb loaded;
        loaded.load (dbFile);
        check (loaded.size() == 2 && loaded.find (key0) != nullptr && loaded.find (key0)->category == "TOOLS" && loaded.find (key0)->type == "Hit", "assignments survive save and load");

        const auto otherPc = base.getChildFile ("home_pc");
        otherPc.createDirectory();
        lib.copyDirectoryTo (otherPc.getChildFile ("legacy_lib"));
        check (ReclassDb::resolve ({ otherPc.getChildFile ("legacy_lib").getFullPathName() }, key0).existsAsFile(), "a key resolves on a PC where the library sits at another path");
        check (ReclassDb::resolve (roots, "legacy_lib/missing.wav") == juce::File(), "a missing file resolves to nothing");
        check (loaded.toRows().size() == 3 && loaded.toRows()[0][0] == "Key", "rows for Excel export (header + 2)");

        // 5. hard-link view
        const auto view = base.getChildFile ("organised");
        const auto manifest = base.getChildFile ("links.json");
        std::vector<AssignedFile> assigned;
        for (int i = 0; i < 5; ++i)
            assigned.push_back ({ files[(size_t) i], i < 3 ? "TOOLS" : "PAPER", i < 3 ? "Hit" : "Tear" });

        const auto sizeBefore = files[0].getSize();
        const auto plan = planLinks (view, assigned);
        check (plan.problems.isEmpty() && plan.items.size() == 5 && plan.items[0].target.getFileName() == "PAPER_Tear01.wav"
                   || plan.items[0].target.getFileName() == "TOOLS_Hit01.wav",
               "plan: <Category>_<Type>NN.wav under <Category>/<Type>/");
        const auto run = createLinks (plan, manifest);
        check (run.created == 5 && run.errors.isEmpty(), "five links created");
        bool linked = true;
        for (const auto& item : plan.items)
            linked = linked && isSameFile (item.source, item.target) && linkCount (item.source) == 2 && item.target.hasIdenticalContentTo (item.source);
        check (linked, "each link is the same data as its original (link count 2, identical content)");
        check (files[0].getSize() == sizeBefore && files[0].existsAsFile(), "originals are untouched");

        const auto rerun = createLinks (plan, manifest);
        check (rerun.created == 0 && rerun.alreadyThere == 5, "running again creates nothing new");

        // a different file already sitting on a planned name is never overwritten
        {
            auto conflictPlan = plan;
            const auto taken = view.getChildFile ("TOOLS/Hit/TOOLS_Hit99.wav");
            taken.replaceWithText ("someone else's file");
            conflictPlan.items.push_back ({ files[10], taken });
            const auto r = createLinks (conflictPlan, manifest);
            check (r.errors.size() == 1 && taken.loadFileAsString() == "someone else's file", "a name taken by a different file is reported and left alone");
            taken.deleteFile();
        }

        // an organised view on another drive is refused (E: is a different volume from the temp area on C:)
        if (juce::File ("E:\\").exists() && ! sameVolume (files[0], juce::File ("E:\\WaveTagger")))
        {
            const auto crossPlan = planLinks (juce::File ("E:\\WaveTagger\\_never_created_organised"), assigned);
            const auto r = createLinks (crossPlan, base.getChildFile ("cross.json"));
            check (! crossPlan.problems.isEmpty() && r.created == 0 && ! juce::File ("E:\\WaveTagger\\_never_created_organised").exists(),
                   "a different drive is refused before anything is created");
        }

        // undo removes only the links; a link that was replaced by someone's own file is not touched
        {
            const auto replaced = plan.items[1].target;
            replaced.deleteFile();
            replaced.replaceWithText ("my own file now");

            const auto undone = undoLinks (manifest);
            check (undone.removed == 4 && undone.kept == 1 && replaced.loadFileAsString() == "my own file now", "undo removes the links but leaves a replaced file alone");
            bool originalsOk = true;
            for (const auto& f : files)
                originalsOk = originalsOk && f.existsAsFile();
            check (originalsOk && linkCount (files[0]) == 1, "all 24 originals are still there with a single name");
            check (manifest.existsAsFile(), "the manifest keeps the entry that was left alone");
            replaced.deleteFile();
            const auto last = undoLinks (manifest);
            check (! manifest.existsAsFile() && last.removed == 1, "finally nothing is left to undo");
            check (! view.getChildFile ("TOOLS").exists() && ! view.getChildFile ("PAPER").exists(), "folders that became empty are removed");
        }
        return ok;
    }

    /** Spelling variants and duplicates: what must be found, and what must NOT be. */
    bool selfTestExport()
    {
        bool ok = true;
        const auto check = [&ok] (bool cond, const char* what) {
            std::printf ("%s  %s\n", cond ? "PASS" : "FAIL", what);
            ok = ok && cond;
        };

        check (exportSubFolder ("GLASS", "Glass", "Hit", false) == "GLASS", "a Prefix that repeats the Category (any case) adds no folder level");
        check (exportSubFolder ("TOOLS", "Pen", "Hit", false) == "TOOLS/Pen", "otherwise <Category>/<Prefix>");
        check (exportSubFolder ("TOOLS", "Pen", "Hit", true) == "TOOLS/Pen/Hit", "with Type folders: <Category>/<Prefix>/<Type>");
        check (exportSubFolder ("GLASS", "Glass", "Hit", true) == "GLASS/Hit" && exportSubFolder ("", "Pen", "Hit", true) == "Pen/Hit", "Type folders without a repeated Prefix or Category");

        const auto root = juce::File (juce::SystemStats::getEnvironmentVariable ("WT_APPDATA", {})).getChildFile ("export_test");
        root.deleteRecursively();
        root.getChildFile ("Pen/Hit").createDirectory();
        for (const auto* n : { "Pen_Hit01", "Pen_Hit02", "Pen_Hit04" })
            root.getChildFile ("Pen/Hit").getChildFile (juce::String (n) + ".wav").replaceWithText ("x");

        const auto nameFor = [] (int type, int number) { return juce::String (type == 0 ? "Pen_Hit" : "Pen_Drop") + juce::String (number).paddedLeft ('0', 2); };
        const auto make = [] (int type, int number) {
            ExportItem it;
            it.type = type;
            it.number = number;
            it.subFolder = "Pen/Hit";
            it.comment = "no=" + juce::String (number) + "; src=a";
            return it;
        };

        std::vector<ExportItem> items { make (0, 1), make (0, 2), make (0, 3), make (1, 1) };
        const auto changed = renumberForDestination (items, root, nameFor);
        check (items[0].fileName == "Pen_Hit03" && items[1].fileName == "Pen_Hit05" && items[2].fileName == "Pen_Hit06",
               "taken numbers (01, 02, 04) are skipped; the export keeps its order (03, 05, 06)");
        check (items[3].fileName == "Pen_Drop01" && changed == 3, "another Type is unaffected; the number of changes is reported");
        check (items[1].comment.contains ("no=5;"), "the stored number follows the new name");

        std::vector<ExportItem> again { make (0, 7), make (0, 7) };
        renumberForDestination (again, root, nameFor);
        check (again[0].fileName == "Pen_Hit07" && again[1].fileName == "Pen_Hit08", "two equal numbers inside one export never collide");

        std::vector<ExportItem> other { make (0, 1) };
        other[0].subFolder = "Pen/Other";
        check (renumberForDestination (other, root, nameFor) == 0 && other[0].fileName == "Pen_Hit01", "another folder has its own numbers");
        root.deleteRecursively();
        return ok;
    }

    bool selfTestChecks()
    {
        const auto testRoot = juce::File (juce::SystemStats::getEnvironmentVariable ("WT_APPDATA", {}));
        if (testRoot.getFullPathName().isEmpty())
        {
            std::printf ("FAIL  refusing to run: WT_APPDATA is not set\n");
            return false;
        }

        bool ok = true;
        const auto check = [&ok] (bool cond, const char* what) {
            std::printf ("%s  %s\n", cond ? "PASS" : "FAIL", what);
            ok = ok && cond;
        };

        const auto base = testRoot.getChildFile ("checks_test");
        base.deleteRecursively();

        const auto scan = [] (LibraryIndex& index, const juce::File& root) {
            index.setRoots (juce::StringArray { root.getFullPathName() });
            index.rescan();
            for (int i = 0; i < 300 && index.isScanning(); ++i)
                juce::Thread::sleep (50);
            juce::Thread::sleep (100);
            return index.getData();
        };

        // ---- spelling variants
        const auto names = base.getChildFile ("names");
        names.createDirectory();
        const auto make = [&] (const juce::String& stem, int count) {
            for (int i = 1; i <= count; ++i)
                names.getChildFile (stem + "_" + juce::String (i).paddedLeft ('0', 2) + ".wav").replaceWithText ("x");
        };
        make ("MasicTape", 13);
        make ("MaskingTape", 5);
        make ("ImpactSound", 9);
        make ("ImpactSoun", 1);
        make ("Door_Open", 3);
        make ("Door_Opne", 1);
        make ("Rain_Heavy", 2);
        make ("RainHeavy", 1);
        make ("Whoosh", 4);
        make ("Whoosh_B", 3);
        make ("Zap", 2);
        make ("Funky", 3);
        make ("Fonky", 1);
        make ("SynthA", 4);
        make ("SynthB", 3);
        make ("SynthC", 2);
        make ("SynthD", 1);
        make ("Synth", 5);
        make ("Piano", 6);
        make ("Pianoa", 2);
        make ("Pianob", 2);
        make ("Pianoc", 2);
        make ("Eliot_1_V", 3);
        make ("Eliot_2_V", 3);
        make ("Eliot_3_V", 3);

        LibraryIndex nameIndex;
        const auto data = scan (nameIndex, names);

        const auto hasGroup = [] (const std::vector<NameGroup>& groups, const juce::String& a, const juce::String& b) {
            for (const auto& g : groups)
            {
                bool hasA = false, hasB = false;
                for (const auto& v : g.variants)
                {
                    hasA = hasA || v.display == a;
                    hasB = hasB || v.display == b;
                }
                if (hasA && hasB)
                    return true;
            }
            return false;
        };

        const auto normal = findSimilarNames (*data, NameSensitivity::Normal);
        check (normal.size() == 3, "normal sensitivity finds exactly three groups (series and short names excluded)");
        check (hasGroup (normal, "ImpactSound", "ImpactSoun") && hasGroup (normal, "Door_Open", "Door_Opne") && hasGroup (normal, "Rain_Heavy", "RainHeavy"),
               "ImpactSoun, Door_Opne (swapped letters) and RainHeavy (missing underscore) are caught");
        bool commonFirst = true;
        for (const auto& g : normal)
            commonFirst = commonFirst && g.variants[0].count > g.variants[1].count;
        check (commonFirst && normal[0].variants[0].display == "ImpactSound" && normal[0].variants[0].count == 9, "the most-used spelling is listed first as the likely correct one");
        check (! hasGroup (normal, "Whoosh", "Whoosh_B") && ! hasGroup (normal, "MasicTape", "MaskingTape"), "genuinely different names (Whoosh / Whoosh_B) are not flagged");
        check (normal[0].variants[0].example.existsAsFile(), "each variant carries an example file to look at");

        const auto strict = findSimilarNames (*data, NameSensitivity::Strict);
        const auto loose = findSimilarNames (*data, NameSensitivity::Loose);
        check (strict.size() <= normal.size() && normal.size() <= loose.size() && hasGroup (strict, "ImpactSound", "ImpactSoun"),
               "strict reports a subset of normal, which reports a subset of loose");
        check (hasGroup (loose, "MasicTape", "MaskingTape") && hasGroup (loose, "Whoosh", "Whoosh_B") && hasGroup (loose, "Funky", "Fonky"),
               "loose also reports MasicTape / MaskingTape, Whoosh / Whoosh_B and the short Funky / Fonky");
        check (! hasGroup (normal, "Funky", "Fonky"), "short names (5 letters) are not flagged unless loose: they are often different words");
        bool seriesFlagged = false;
        for (const auto& g : normal)
            for (const auto& v : g.variants)
                seriesFlagged = seriesFlagged || v.display.startsWith ("SynthA") || v.display.startsWith ("SynthB") || v.display.startsWith ("Pianoa")
                                || v.display.startsWith ("Pianob") || v.display.startsWith ("Eliot_");
        check (! seriesFlagged, "lettered / numbered series (SynthA-D, Pianoa-c, Eliot_1_V ...) are not reported as typos");
        const auto skipped = findSimilarNames (*data, NameSensitivity::Normal, { ignoreKey ("impactsoun", "impactsound") });
        check (skipped.size() == 2 && ! hasGroup (skipped, "ImpactSound", "ImpactSoun"), "a pair marked as 'different names' is no longer reported");
        check (ignoreKey ("b", "a") == ignoreKey ("a", "b"), "the ignore key does not depend on order");

        // ---- exact duplicates
        const auto dupes = base.getChildFile ("dupes");
        dupes.getChildFile ("sub").createDirectory();
        const auto randomBytes = [] (int seed, int n) {
            juce::Random rnd (seed);
            juce::MemoryBlock b ((size_t) n);
            for (int i = 0; i < n; ++i)
                static_cast<juce::uint8*> (b.getData())[i] = (juce::uint8) rnd.nextInt (256);
            return b;
        };
        const auto put = [] (const juce::File& f, const juce::MemoryBlock& b) { f.replaceWithData (b.getData(), b.getSize()); };

        const auto a = randomBytes (1, 5000);
        put (dupes.getChildFile ("A.wav"), a);
        put (dupes.getChildFile ("A_copy.wav"), a);
        put (dupes.getChildFile ("sub/A_again.wav"), a);
        put (dupes.getChildFile ("B.wav"), randomBytes (2, 5000));   // same size as A, different content
        put (dupes.getChildFile ("C.wav"), randomBytes (3, 3000));
        put (dupes.getChildFile ("D.wav"), randomBytes (4, 3000));   // same size as C, different content
        put (dupes.getChildFile ("Unique.wav"), randomBytes (5, 777));

        auto big = randomBytes (6, 300 * 1024);
        put (dupes.getChildFile ("E.wav"), big);
        put (dupes.getChildFile ("G.wav"), big);                       // identical copy of E
        auto almost = big;                                             // same first and last 64 KB, different middle
        static_cast<juce::uint8*> (almost.getData())[150 * 1024] ^= 0x5A;
        put (dupes.getChildFile ("F.wav"), almost);

        LibraryIndex dupIndex;
        auto dupData = scan (dupIndex, dupes);
        HashStore hashes (base.getChildFile ("hashes.bin"));
        hashes.load();

        const auto groups = findExactDuplicates (*dupData, hashes);
        const auto nameSet = [] (const DuplicateGroup& g) {
            std::set<juce::String> s;
            for (const auto& f : g.files)
                s.insert (f.getFileName());
            return s;
        };
        std::set<std::set<juce::String>> found;
        for (const auto& g : groups)
            found.insert (nameSet (g));

        check (groups.size() == 2 && found.count ({ "A.wav", "A_copy.wav", "A_again.wav" }) == 1 && found.count ({ "E.wav", "G.wav" }) == 1,
               "exactly the two real duplicate sets are found (A x3, E x2), across sub-folders");
        check (found.count ({ "E.wav", "F.wav", "G.wav" }) == 0 && found.count ({ "B.wav", "A.wav" }) == 0 && found.count ({ "C.wav", "D.wav" }) == 0,
               "same size, or same ends but a different middle, is NOT reported as a duplicate");
        check (hashes.hashesComputed() > 0, "hashes were computed on the first run");

        HashStore again (base.getChildFile ("hashes.bin"));
        again.load();
        const auto rerun = findExactDuplicates (*dupData, again);
        check (rerun.size() == 2 && again.hashesComputed() == 0, "a second run reads nothing: all hashes come from the saved store");

        // a file that changes (same size, new content) is hashed again and drops out of its group
        put (dupes.getChildFile ("A_copy.wav"), randomBytes (99, 5000));
        juce::Thread::sleep (1100);
        dupData = scan (dupIndex, dupes);
        const auto afterEdit = findExactDuplicates (*dupData, again);
        std::set<std::set<juce::String>> found2;
        for (const auto& g : afterEdit)
            found2.insert (nameSet (g));
        check (again.hashesComputed() >= 1 && found2.count ({ "A.wav", "A_again.wav" }) == 1 && found2.count ({ "A.wav", "A_copy.wav", "A_again.wav" }) == 0,
               "an edited file is re-hashed and leaves its duplicate set");

        int calls = 0;
        const auto cancelled = findExactDuplicates (*dupData, hashes, [&calls] (int, int) { return ++calls < 3; });
        check (cancelled.size() <= 2, "a run can be cancelled from the progress callback");

        // ---- near duplicates: the same sound stored differently
        const auto tones = base.getChildFile ("tones");
        tones.createDirectory();
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        const auto tone = [] (double freq, int samples) {
            juce::AudioBuffer<float> b (1, samples);
            for (int i = 0; i < samples; ++i)
                b.setSample (0, i, i < 44100 ? 0.4f * (float) std::sin (6.2831853 * freq * i / 44100.0) : 0.0f);
            return b;
        };
        const auto save = [&] (const juce::File& f, const juce::AudioBuffer<float>& b, bool flac) {
            std::unique_ptr<juce::AudioFormat> format (flac ? static_cast<juce::AudioFormat*> (new juce::FlacAudioFormat()) : new juce::WavAudioFormat());
            std::unique_ptr<juce::OutputStream> stream (f.createOutputStream());
            auto w = format->createWriterFor (stream, juce::AudioFormatWriterOptions {}.withSampleRate (44100.0).withNumChannels (1).withBitsPerSample (16));
            w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        };
        save (tones.getChildFile ("tone.wav"), tone (440.0, 44100), false);
        save (tones.getChildFile ("tone_flac.flac"), tone (440.0, 44100), true);
        save (tones.getChildFile ("tone_padded.wav"), tone (440.0, 44100 + 882), false); // 20 ms of extra silence
        save (tones.getChildFile ("other_tone.wav"), tone (1250.0, 44100), false);
        save (tones.getChildFile ("long_tone.wav"), tone (440.0, 44100 * 3), false);

        FeatureStore featureStore (base.getChildFile ("features.bin"));
        std::vector<juce::File> toneFiles;
        for (const auto& f : tones.findChildFiles (juce::File::findFiles, false, "*"))
        {
            FeatureVector v {};
            if (computeFeatures (formats, f, v))
                featureStore.put (f, v);
            toneFiles.push_back (f);
        }

        const auto near = findNearDuplicates (toneFiles, featureStore);
        check (near.size() == 1 && ! near[0].exact && nameSet (near[0]) == std::set<juce::String> { "tone.wav", "tone_flac.flac", "tone_padded.wav" },
               "the same tone as WAV, FLAC and with 20 ms extra silence is grouped as near-duplicates");
        check (nameSet (near[0]).count ("other_tone.wav") == 0 && nameSet (near[0]).count ("long_tone.wav") == 0, "a different pitch or a much longer take is not");
        return ok;
    }

    /** Read-only report on a real library: how many spelling groups, and how much reading a duplicate scan would need. */
    bool reportChecks()
    {
        const auto root = juce::String ("G:\\SE");
        if (! juce::File (root).isDirectory())
        {
            std::printf ("no %s\n", root.toRawUTF8());
            return false;
        }

        LibraryIndex index;
        index.setRoots (juce::StringArray { root });
        for (int i = 0; i < 3600 && (index.isScanning() || index.getData()->entries.empty()); ++i)
            juce::Thread::sleep (50);
        const auto data = index.getData();
        std::printf ("files=%d names=%d\n", (int) data->entries.size(), (int) data->bases.size());

        for (auto [label, sens] : { std::pair<const char*, NameSensitivity> { "strict", NameSensitivity::Strict },
                                    std::pair<const char*, NameSensitivity> { "normal", NameSensitivity::Normal },
                                    std::pair<const char*, NameSensitivity> { "loose", NameSensitivity::Loose } })
        {
            const auto t0 = juce::Time::getMillisecondCounter();
            const auto groups = findSimilarNames (*data, sens);
            std::printf ("%s: %d groups (%u ms)\n", label, (int) groups.size(), juce::Time::getMillisecondCounter() - t0);
            if (sens == NameSensitivity::Normal)
                for (size_t i = 0; i < groups.size() && i < 40; ++i)
                {
                    juce::String line = "  ";
                    for (const auto& v : groups[i].variants)
                        line += v.display + "(" + juce::String (v.count) + ")  ";
                    std::printf ("%s\n", line.toRawUTF8());
                }
        }

        std::unordered_map<juce::int64, int> bySize;
        for (const auto& e : data->entries)
            if (e.size >= 512)
                ++bySize[e.size];
        int groups = 0, files = 0;
        double bytes = 0.0;
        for (const auto& [size, n] : bySize)
            if (n >= 2)
            {
                ++groups;
                files += n;
                bytes += (double) size * n;
            }
        std::printf ("size-collision groups=%d files=%d (stage 1 reads ~%.0f MB; the whole of these files is %.1f GB)\n", groups, files, files * 128.0 / 1024.0, bytes / 1.0e9);
        return true;
    }

    /** The learning data and the suggestion engine. */
    bool selfTestLearn()
    {
        const auto testRoot = juce::File (juce::SystemStats::getEnvironmentVariable ("WT_APPDATA", {}));
        if (testRoot.getFullPathName().isEmpty())
        {
            std::printf ("FAIL  refusing to run: WT_APPDATA is not set\n");
            return false;
        }

        bool ok = true;
        const auto check = [&ok] (bool cond, const char* what) {
            std::printf ("%s  %s\n", cond ? "PASS" : "FAIL", what);
            ok = ok && cond;
        };

        const auto dir = testRoot.getChildFile ("learn_test");
        dir.deleteRecursively();

        // four kinds of sound; each variant is a little different, and sits inside a longer, quiet buffer
        enum Kind { hit = 0, rub = 1, click = 2, tone = 3 };
        const char* typeNames[] = { "Hit", "Rub", "Click", "Tone" };
        const char* categories[] = { "TOOLS", "PAPER", "TOOLS", "PAPER" };

        struct Made
        {
            juce::AudioBuffer<float> buffer;
            juce::int64 start, end;
        };
        const auto make = [] (Kind kind, int variant) {
            const double sr = 48000.0;
            juce::Random rnd (5000 + variant * 31 + (int) kind * 977);
            Made m { juce::AudioBuffer<float> (1, (int) (3.0 * sr)), 0, 0 };
            for (int i = 0; i < m.buffer.getNumSamples(); ++i)
                m.buffer.setSample (0, i, (rnd.nextFloat() - 0.5f) * 0.0006f);

            const auto length = kind == hit ? (int) ((0.35 + 0.02 * (variant % 5)) * sr) : kind == rub ? (int) ((1.2 + 0.05 * (variant % 6)) * sr)
                              : kind == click ? (int) (0.008 * sr) : (int) ((0.8 + 0.05 * (variant % 5)) * sr);
            m.start = (juce::int64) (0.4 * sr + variant * 400);
            m.end = m.start + length;

            float lp = 0.0f;
            const auto coeff = 0.25f + 0.03f * (float) (variant % 5);
            for (int i = 0; i < length; ++i)
            {
                const auto t = (double) i / sr;
                float v = 0.0f;
                if (kind == hit)
                    v = (rnd.nextFloat() * 2.0f - 1.0f) * 0.7f * (float) std::exp (-t * (12.0 + variant % 6));
                else if (kind == rub)
                {
                    lp += coeff * ((rnd.nextFloat() * 2.0f - 1.0f) - lp);
                    v = lp * 1.4f * (0.6f + 0.4f * (float) std::sin (t * (20.0 + variant)));
                }
                else if (kind == click)
                    v = (rnd.nextFloat() * 2.0f - 1.0f) * 0.9f * (float) std::exp (-t * 900.0);
                else
                    v = 0.4f * (float) (std::sin (6.2831853 * (300.0 + 40.0 * variant) * t) * std::min (1.0, t * 12.0));
                m.buffer.setSample (0, (int) m.start + i, v);
            }
            return m;
        };

        const auto sampleOf = [&] (Kind kind, int variant, bool human) {
            const auto m = make (kind, variant);
            LabelSample s;
            s.fileName = juce::String (typeNames[kind]) + juce::String (variant) + ".wav";
            s.start = m.start;
            s.end = m.end;
            s.key = LabelStore::makeKey (s.fileName, 12345, s.start, s.end);
            computeSegmentFeatures (m.buffer, 48000.0, m.start, m.end, s.v);
            s.category = categories[kind];
            s.type = typeNames[kind];
            s.source = "editor";
            s.human = human;
            return s;
        };

        // 1. segment features
        {
            const auto m = make (hit, 2);
            FeatureVector a {}, b {};
            std::vector<float> mono;
            for (auto i = m.start; i < m.end; ++i)
                mono.push_back (m.buffer.getSample (0, (int) i));
            check (computeSegmentFeatures (m.buffer, 48000.0, m.start, m.end, a) && computeFeaturesFromMono (mono, 48000.0, (double) mono.size() / 48000.0, b) && a == b,
                   "segment features equal the features of the same samples taken out");
            FeatureVector whole {};
            computeSegmentFeatures (m.buffer, 48000.0, 0, m.buffer.getNumSamples(), whole);
            check (std::abs (a[45] - whole[45]) > 0.3f, "a segment is described by its own length, not the length of the file it sits in");
            check (! computeSegmentFeatures (m.buffer, 48000.0, 100, 120, a), "a few samples are refused, not guessed");
        }

        // 2. the label store
        LabelStore store (dir, "PC-A");
        std::vector<LabelSample> training, held;
        for (int kind = 0; kind < 4; ++kind)
            for (int variant = 0; variant < 12; ++variant)
                (variant < 8 ? training : held).push_back (sampleOf ((Kind) kind, variant, true));
        const auto trainingCopy = training;
        store.upsert (std::move (training));
        check (store.size() == 32 && store.humanLabelCount() == 32 && dir.getChildFile ("labels_PC-A.jsonl").existsAsFile(), "labels are kept and written to this PC's own file");

        LabelStore reopened (dir, "PC-A");
        reopened.load();
        bool same = reopened.size() == 32;
        for (const auto& s : trainingCopy)
        {
            const auto* r = reopened.find (s.key);
            same = same && r != nullptr && r->type == s.type && r->category == s.category && r->start == s.start && std::abs (r->v[3] - s.v[3]) < 1.0e-3f;
        }
        check (same, "everything comes back after a restart (labels, ranges, features)");
        check (LabelStore::makeKey ("Pen_Push01.WAV", 99, 5, 10) == "pen_push01.wav|99|5|10", "keys ignore case and drive letters (file name + size + range)");

        // two PCs write separate files; the newest entry per key wins; damaged lines do not matter
        {
            LabelStore a (dir, "PC-A"), b (dir, "PC-B");
            LabelSample x = sampleOf (hit, 0, true);
            x.key = "shared|1|0|0";
            x.type = "FromA";
            x.updatedAt = 1000;
            a.upsert (x);
            x.type = "FromB";
            x.updatedAt = 2000;
            b.upsert (x);
            dir.getChildFile ("labels_PC-B.jsonl").appendText ("{ this line is damaged\n", false, false);

            LabelStore merged (dir, "PC-C");
            merged.load();
            check (merged.find ("shared|1|0|0") != nullptr && merged.find ("shared|1|0|0")->type == "FromB" && merged.size() == 33,
                   "two PCs' files merge: the newest entry wins and a damaged line is skipped");
        }

        // 3. the suggestion engine
        Suggester empty;
        empty.rebuild (LabelStore());
        check (! empty.isReady() && ! empty.suggest (held[0].v).valid(), "with too few labels it makes no suggestion at all");

        // unconfirmed automatic labels (human = false) and NG sounds must never be learned from
        LabelStore store2 (dir.getChildFile ("second"), "PC-A");
        store2.upsert (trainingCopy);
        for (int i = 0; i < 10; ++i)
        {
            auto bad = sampleOf (rub, i, false);
            bad.key += "|auto";
            bad.type = "Wrong";
            bad.category = "NOPE";
            store2.upsert (bad);
        }
        auto ng = sampleOf (tone, 9, true);
        ng.key += "|ng";
        ng.type = "BadTake";
        ng.ng = true;
        store2.upsert (ng);

        Suggester engine;
        engine.rebuild (store2);
        check (engine.isReady() && engine.sampleCount() == 32, "only human-confirmed, non-NG labels are used (32 of 43)");

        int right = 0, categoryRight = 0;
        double confRight = 0.0;
        for (const auto& h : held)
        {
            const auto sug = engine.suggest (h.v);
            if (sug.type == h.type)
            {
                ++right;
                confRight += sug.confidence;
            }
            categoryRight += sug.category == h.category ? 1 : 0;
        }
        std::printf ("INFO  held-out accuracy: %d / %d types, %d / %d categories, mean confidence when right = %.2f\n",
                     right, (int) held.size(), categoryRight, (int) held.size(), right > 0 ? confRight / right : 0.0);
        check (right >= (int) held.size() * 9 / 10, "sounds it has never heard are typed correctly (>= 90 %)");
        check (right > 0 && confRight / right > 0.6, "and with high confidence when it is right");

        // something unlike anything labelled: loud, long, wide-band noise
        juce::AudioBuffer<float> odd (1, 48000 * 3);
        juce::Random rnd (4242);
        for (int i = 0; i < odd.getNumSamples(); ++i)
            odd.setSample (0, i, (rnd.nextFloat() * 2.0f - 1.0f) * 0.9f);
        FeatureVector oddFeatures {};
        computeSegmentFeatures (odd, 48000.0, 0, odd.getNumSamples(), oddFeatures);
        const auto oddSuggestion = engine.suggest (oddFeatures);
        check (oddSuggestion.confidence < (float) (confRight / right) * 0.8f, "a sound unlike everything labelled gets a clearly lower confidence");

        // category hint: PAPER only has Rub and Tone, so a Hit can only be one of those
        const auto hinted = engine.suggest (held[0].v, "PAPER"); // held[0] is a Hit (TOOLS)
        check (hinted.category == "PAPER" && (hinted.type == "Rub" || hinted.type == "Tone"), "with a Category chosen, only that Category's Types are offered");

        const auto top = engine.suggestTop (held[1].v, 3);
        bool sorted = ! top.empty();
        for (size_t i = 1; i < top.size(); ++i)
            sorted = sorted && top[i - 1].confidence >= top[i].confidence;
        check (sorted && top.size() >= 1 && top[0].type == held[1].type, "the top-3 list is ordered by confidence");

        // more labels help: a type that was not known is learned by one more saved example set
        store2.upsert (sampleOf (hit, 11, true));
        engine.rebuild (store2);
        check (engine.sampleCount() == 33, "a newly saved label is used as soon as the engine is rebuilt");

        // ---- the editor end to end: save -> learn -> suggest -> accept / apply all / undo -> log what was decided
        {
            testRoot.getChildFile ("settings.json").replaceWithText ("{\"libraryRoots\":[\"Z:\\\\none\"]}");

            // one session: 12 labelled sounds (3 per kind) followed by 4 new ones the user has not typed yet
            struct Plan
            {
                Kind kind;
                int variant;
                bool typed;
            };
            std::vector<Plan> plan;
            for (int v = 0; v < 3; ++v)
                for (int k = 0; k < 4; ++k)
                    plan.push_back ({ (Kind) k, v, true });
            for (int k = 0; k < 4; ++k)
                plan.push_back ({ (Kind) k, 7, false });

            auto data = std::make_shared<AudioData>();
            data->sampleRate = 48000.0;
            data->file = testRoot.getChildFile ("learn_test").getChildFile ("session.wav");
            data->buffer.setSize (1, (int) (plan.size() * 2.5 * 48000.0));
            data->buffer.clear();
            std::vector<std::pair<juce::int64, juce::int64>> ranges;
            for (size_t i = 0; i < plan.size(); ++i)
            {
                const auto m = make (plan[i].kind, plan[i].variant);
                const auto base = (juce::int64) (i * 2.5 * 48000.0) + 24000;
                for (auto s = m.start; s < m.end; ++s)
                    data->buffer.setSample (0, (int) (base + (s - m.start)), m.buffer.getSample (0, (int) s));
                ranges.emplace_back (base, base + (m.end - m.start));
            }
            data->peaks.build (data->buffer);

            LabelStore* seen = nullptr;
            int learnedTotal = 0;
            {
                MainComponent m;
                m.testLoadAudio (data);
                auto& cy = m.testCyclesMutable();
                for (int k = 0; k < 4; ++k)
                    cy.setTypeName (k, typeNames[k]);
                cy.setCategory ("TOOLS");
                for (size_t i = 0; i < plan.size(); ++i)
                    cy.add (ranges[i].first, ranges[i].second, plan[i].typed ? (int) plan[i].kind : -1);

                check (m.testLabelStore().size() == 0, "nothing is learned just from typing: only from saving");
                m.testRecord ("editor");
                check (m.testLabelStore().size() == 12 && m.testLabelStore().humanLabelCount() == 12, "saving records the 12 typed cycles as confirmed examples");

                for (int i = 0; i < 40; ++i)
                    m.testSuggestTick();
                int suggested = 0, correct = 0;
                for (int i = 12; i < 16; ++i)
                {
                    suggested += cy[i].autoType.isNotEmpty() ? 1 : 0;
                    correct += cy[i].autoType == typeNames[(int) plan[(size_t) i].kind] ? 1 : 0;
                }
                check (suggested == 4 && correct == 4 && cy[12].type == -1, "the four untyped cycles each get the right suggestion, and stay untyped");
                check (cy[12].autoConf > 0.5f, "with a sensible confidence");

                m.testAccept ({ 12 });
                check (cy[12].type == (int) plan[12].kind && cy[12].confirmed, "accepting a suggestion types the cycle (confirmed: a person chose it)");

                const auto applied = m.testApplyAll (0.7f);
                check (applied >= 1 && applied <= 3 && cy[13].type == (applied >= 1 ? (int) plan[13].kind : -1) && ! cy[13].confirmed,
                       "applying all confident suggestions types them as 'automatic, not yet looked at'");
                cy.undo();
                check (cy[13].type == -1 && cy[12].type == (int) plan[12].kind, "and that is a single undo step");
                m.testApplyAll (0.7f);

                // the user corrects one suggestion, then saves: the log keeps both the suggestion and the final answer
                const auto wrongSlot = (int) plan[14].kind == 0 ? 1 : 0;
                cy.setType ({ 14 }, wrongSlot);
                m.testRecord ("editor");
                const auto key14 = LabelStore::makeKey ("session.wav", data->getNumSamples(), ranges[14].first, ranges[14].second);
                const auto key12 = LabelStore::makeKey ("session.wav", data->getNumSamples(), ranges[12].first, ranges[12].second);
                const auto* corrected = m.testLabelStore().find (key14);
                const auto* accepted = m.testLabelStore().find (key12);
                check (corrected != nullptr && corrected->type == typeNames[wrongSlot] && corrected->suggestedType == typeNames[(int) plan[14].kind],
                       "a corrected suggestion is logged as 'suggested X, person chose Y'");
                check (accepted != nullptr && accepted->type == accepted->suggestedType && accepted->suggestedConfidence > 0.0f, "an accepted one is logged as agreeing with the suggestion");
                int typedNow = 0;
                for (int i = 0; i < cy.size(); ++i)
                    typedNow += cy[i].type >= 0 ? 1 : 0;
                learnedTotal = (int) m.testLabelStore().size();
                check (learnedTotal == typedNow && typedNow >= 14 && cy[15].confirmed,
                       "saving confirmed every cycle; every typed one is a stored example (untyped ones are not learned)");
                seen = nullptr;
            }
            juce::ignoreUnused (seen);

            // a new session (or another PC reading the same folder) starts with everything learned so far
            MainComponent again;
            check ((int) again.testLabelStore().size() == learnedTotal && learnedTotal > 0, "the next start (or another PC) has every example already");
        }
        return ok;
    }

    bool selfTestCondition()
    {
        const auto testRoot = juce::File (juce::SystemStats::getEnvironmentVariable ("WT_APPDATA", {}));
        if (testRoot.getFullPathName().isEmpty())
        {
            std::printf ("FAIL  refusing to run: WT_APPDATA is not set\n");
            return false;
        }

        bool ok = true;
        const auto check = [&ok] (bool cond, const char* what) {
            std::printf ("%s  %s\n", cond ? "PASS" : "FAIL", what);
            ok = ok && cond;
        };

        constexpr double pi = juce::MathConstants<double>::pi;
        const auto db = [] (double x) { return 20.0 * std::log10 (juce::jmax (x, 1.0e-12)); };
        const auto sine = [&] (int channels, double sr, double seconds, double freq, double amp, double dc = 0.0) {
            juce::AudioBuffer<float> b (channels, (int) (sr * seconds));
            for (int ch = 0; ch < channels; ++ch)
                for (int i = 0; i < b.getNumSamples(); ++i)
                    b.setSample (ch, i, (float) (amp * std::sin (2.0 * pi * freq * i / sr) + dc * (ch == 0 ? 1.0 : -0.5)));
            return b;
        };
        const auto rms = [] (const juce::AudioBuffer<float>& b, int from, int to) {
            double s = 0.0;
            for (int i = from; i < to; ++i)
                s += (double) b.getSample (0, i) * b.getSample (0, i);
            return std::sqrt (s / (to - from));
        };
        const auto mean = [] (const juce::AudioBuffer<float>& b, int ch) {
            double s = 0.0;
            for (int i = 0; i < b.getNumSamples(); ++i)
                s += b.getSample (ch, i);
            return s / b.getNumSamples();
        };
        const auto identical = [] (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b) {
            if (a.getNumChannels() != b.getNumChannels() || a.getNumSamples() != b.getNumSamples())
                return false;
            for (int ch = 0; ch < a.getNumChannels(); ++ch)
                if (std::memcmp (a.getReadPointer (ch), b.getReadPointer (ch), sizeof (float) * (size_t) a.getNumSamples()) != 0)
                    return false;
            return true;
        };

        ConditionReport rep;

        // ---- defaults are inert
        {
            const ConditionOptions off;
            auto b = sine (2, 48000.0, 1.0, 440.0, 0.3, 0.05);
            const auto before = b;
            conditionFile (b, 48000.0, off, rep);
            conditionCycle (b, 48000.0, off, rep);
            check (! off.active() && ! off.needsFile() && identical (b, before), "default options change nothing (bit-exact)");
        }

        // ---- DC removal
        {
            ConditionOptions o;
            o.dcRemove = true;
            auto b = sine (2, 48000.0, 2.0, 1000.0, 0.5, 0.1);
            const auto len = b.getNumSamples();
            conditionFile (b, 48000.0, o, rep);
            check (std::abs (mean (b, 0)) < 1.0e-5 && std::abs (mean (b, 1)) < 1.0e-5, "DC offset is removed from each channel separately");
            check (std::abs (rep.dcOffset - 0.1) < 1.0e-4, "the report carries the largest offset that was removed");
            check (std::abs (db (rms (b, 0, len)) - db (0.5 / std::sqrt (2.0))) < 0.01 && b.getNumSamples() == len, "the tone is untouched and the length is the same");
        }

        // ---- low cut
        {
            const auto attenuation = [&] (double freq, float cut, int order) {
                ConditionOptions o;
                o.lowCutHz = cut;
                o.lowCutOrder = order;
                auto b = sine (1, 48000.0, 4.0, freq, 0.5);
                conditionFile (b, 48000.0, o, rep);
                return db (rms (b, 48000 * 2, 48000 * 4)) - db (0.5 / std::sqrt (2.0));
            };
            check (std::abs (attenuation (40.0, 40.0f, 2) - (-3.01)) < 0.2, "2nd order: -3 dB at the cut-off");
            check (std::abs (attenuation (20.0, 40.0f, 2) - (-12.3)) < 0.6, "2nd order: -12 dB an octave below (12 dB/oct)");
            check (std::abs (attenuation (20.0, 40.0f, 4) - (-24.1)) < 1.0, "4th order: -24 dB an octave below (24 dB/oct)");
            check (std::abs (attenuation (2000.0, 40.0f, 4)) < 0.01, "the pass band is flat");

            ConditionOptions o;
            o.lowCutHz = 20.0f;
            juce::AudioBuffer<float> step (1, 48000);
            for (int i = 0; i < step.getNumSamples(); ++i)
                step.setSample (0, i, 0.3f);
            conditionFile (step, 48000.0, o, rep);
            check (step.getMagnitude (0, 0, 48000) < 1.0e-5f, "a recording that starts away from zero does not click");
        }

        // ---- hum removal
        {
            const double sr = 48000.0;
            // stereo: mains hum (+ harmonics) and a little noise throughout, two loud 1 kHz bursts
            const auto humSignal = [&] (double seconds, double f0, const std::vector<double>& amps, bool bursts) {
                juce::AudioBuffer<float> b (2, (int) (sr * seconds));
                juce::Random rnd (31);
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < b.getNumSamples(); ++i)
                    {
                        double v = (rnd.nextFloat() - 0.5) * 0.004;
                        for (size_t h = 0; h < amps.size(); ++h)
                            v += amps[h] * std::sin (2.0 * pi * f0 * (double) (h + 1) * i / sr + 0.7 * (double) h);
                        const auto t = i / sr;
                        if (bursts && ((t >= 3.0 && t < 3.5) || (t >= 6.0 && t < 6.5)))
                            v += 0.5 * std::sin (2.0 * pi * 1000.0 * i / sr);
                        b.setSample (ch, i, (float) v);
                    }
                return b;
            };
            const auto level = [&] (const juce::AudioBuffer<float>& b, double freq, double fromSec, double toSec) {
                double re = 0.0, im = 0.0;
                const auto from = (int) (fromSec * sr), to = (int) (toSec * sr);
                for (int i = from; i < to; ++i)
                {
                    re += b.getSample (0, i) * std::cos (2.0 * pi * freq * i / sr);
                    im += b.getSample (0, i) * std::sin (2.0 * pi * freq * i / sr);
                }
                return 2.0 * std::sqrt (re * re + im * im) / (to - from);
            };
            const auto near = [] (const std::vector<double>& v, double hz, double tol) {
                return std::any_of (v.begin(), v.end(), [&] (double x) { return std::abs (x - hz) < tol; });
            };

            ConditionOptions o;
            o.humMode = HumMode::automatic;

            const auto hum60 = humSignal (10.0, 60.0, { 0.01, 0.006, 0.004, 0.002 }, true);
            const auto found = detectHum (hum60, sr, o);
            check (found.mainsHz == 60.0 && found.notchHz.size() >= 4 && near (found.notchHz, 60.0, 0.3) && near (found.notchHz, 120.0, 0.3)
                       && near (found.notchHz, 180.0, 0.3) && near (found.notchHz, 240.0, 0.3),
                   "60 Hz hum and its harmonics are found automatically (not mistaken for 50 Hz)");

            auto cleaned = hum60;
            conditionFile (cleaned, sr, o, rep);
            bool allDown = rep.humHz == 60.0 && rep.humNotches >= 4;
            for (int h = 1; h <= 4; ++h)
                allDown = allDown && level (hum60, 60.0 * h, 4.0, 10.0) / level (cleaned, 60.0 * h, 4.0, 10.0) > 31.6; // 30 dB
            check (allDown, "every harmonic comes down by at least 30 dB");
            check (std::abs (db (rms (cleaned, (int) (3.1 * sr), (int) (3.4 * sr))) - db (rms (hum60, (int) (3.1 * sr), (int) (3.4 * sr)))) < 0.05
                       && cleaned.getNumSamples() == hum60.getNumSamples(),
                   "the sound itself is untouched (< 0.05 dB) and the length is the same");
            check (level (cleaned, 60.0, 0.0, 1.0) < level (hum60, 60.0, 0.0, 1.0) * 0.1, "it works from the very first second (no run-in at the start)");

            const auto hum50 = humSignal (10.0, 50.0, { 0.008, 0.01, 0.005 }, true);
            check (detectHum (hum50, sr, o).mainsHz == 50.0, "50 Hz hum is recognised as 50 Hz");

            const auto drift = humSignal (10.0, 59.7, { 0.01, 0.006, 0.004 }, true);
            auto driftClean = drift;
            conditionFile (driftClean, sr, o, rep);
            check (rep.humHz == 60.0 && level (drift, 59.7, 4.0, 10.0) / level (driftClean, 59.7, 4.0, 10.0) > 17.8,
                   "hum that is not exactly 60 Hz is notched where it really is (-25 dB or better)");

            auto noHum = humSignal (10.0, 60.0, {}, true);
            const auto noHumBefore = noHum;
            conditionFile (noHum, sr, o, rep);
            check (rep.humHz == 0.0 && identical (noHum, noHumBefore), "no hum: nothing is found and the audio is bit-exact");

            auto tone = humSignal (10.0, 100.0, { 0.05 }, true);
            const auto toneBefore = tone;
            conditionFile (tone, sr, o, rep);
            check (rep.humHz == 0.0 && identical (tone, toneBefore), "a single steady tone is not taken for hum (needs two harmonics)");

            auto shortOne = humSignal (0.2, 60.0, { 0.01, 0.006, 0.004 }, false);
            const auto shortBefore = shortOne;
            conditionFile (shortOne, sr, o, rep);
            check (identical (shortOne, shortBefore), "a recording too short to analyse is left alone");

            ConditionOptions forced;
            forced.humMode = HumMode::mains60;
            check (detectHum (hum50, sr, forced).mainsHz == 0.0, "forcing 60 Hz does not touch 50 Hz hum");
            forced.humMode = HumMode::mains50;
            const auto lonely = humSignal (10.0, 50.0, { 0.0, 0.01 }, true); // only the 100 Hz harmonic
            check (detectHum (lonely, sr, forced).mainsHz == 50.0 && detectHum (lonely, sr, o).mainsHz == 0.0,
                   "forcing 50 Hz needs a single clear harmonic, automatic needs two");

            // the mains hum is noticed even when the recording is mostly sound: only the quietest fifth is looked at
            // (the loud part buries this hum completely: only the quiet start can reveal it)
            auto busy = humSignal (10.0, 60.0, { 0.003, 0.002, 0.0015 }, false);
            juce::Random rnd (3);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = (int) (2.8 * sr); i < busy.getNumSamples(); ++i)
                    busy.setSample (ch, i, busy.getSample (ch, i) + (rnd.nextFloat() - 0.5f) * 2.0f);
            check (detectHum (busy, sr, o).mainsHz == 60.0, "hum is found from the quiet start of a mostly noisy recording");
        }

        // ---- left / right balance
        {
            const double sr = 48000.0;
            // eight 0.3 s noise bursts over a quiet floor; the bursts have the given gains per channel
            const auto events = [&] (double gainLeft, double gainRight, double floorLeft, double floorRight, bool moving, double rumbleLeft) {
                juce::AudioBuffer<float> b (2, (int) (sr * 8.0));
                juce::Random rnd (61);
                for (int i = 0; i < b.getNumSamples(); ++i)
                {
                    const auto t = i / sr;
                    const auto inBurst = std::fmod (t, 1.0) >= 0.2 && std::fmod (t, 1.0) < 0.5;
                    const auto k = (int) t;
                    const auto sweep = moving ? (k - 3.5) * 1.7 : 0.0; // dB, left minus right, changing from burst to burst
                    const auto gl = gainLeft * std::pow (10.0, sweep / 40.0), gr = gainRight * std::pow (10.0, -sweep / 40.0);
                    double l = (rnd.nextFloat() - 0.5) * 2.0 * floorLeft, r = (rnd.nextFloat() - 0.5) * 2.0 * floorRight;
                    if (inBurst)
                    {
                        l += (rnd.nextFloat() - 0.5) * 0.6 * gl;
                        r += (rnd.nextFloat() - 0.5) * 0.6 * gr;
                    }
                    l += rumbleLeft * std::sin (2.0 * pi * 25.0 * t);
                    b.setSample (0, i, (float) l);
                    b.setSample (1, i, (float) r);
                }
                return b;
            };
            const auto channelDb = [&] (const juce::AudioBuffer<float>& b, int ch) {
                double sum = 0.0;
                for (int i = 0; i < b.getNumSamples(); ++i)
                    sum += (double) b.getSample (ch, i) * b.getSample (ch, i);
                return 10.0 * std::log10 (sum / b.getNumSamples());
            };

            ConditionOptions o;
            o.panMode = PanMode::automatic;

            const auto quietRight = 1.0 / std::pow (10.0, 3.1 / 20.0);
            auto lopsided = events (1.0, quietRight, 0.002, 0.002, false, 0.0);
            const auto found = detectPanBalance (lopsided, sr, o);
            check (found.outcome == PanOutcome::applied && std::abs (found.differenceDb - 3.1) < 0.25, "a steady 3 dB difference is measured (left louder)");

            const auto before = lopsided;
            conditionFile (lopsided, sr, o, rep);
            check (rep.panApplied && std::abs (rep.panDb - found.differenceDb) < 1.0e-9
                       && std::abs ((channelDb (lopsided, 0) - channelDb (before, 0)) - (-0.5 * found.differenceDb)) < 0.05
                       && std::abs ((channelDb (lopsided, 1) - channelDb (before, 1)) - (0.5 * found.differenceDb)) < 0.05,
                   "each channel moves half of it towards the other (the overall level stays)");
            const auto after = detectPanBalance (lopsided, sr, o);
            check (after.outcome == PanOutcome::balanced && std::abs (after.differenceDb) < 0.25, "afterwards the two sides match");

            // a steady sound (no floor between events) is judged over everything that is loud enough
            juce::AudioBuffer<float> rain (2, (int) (sr * 5.0));
            juce::Random rainRnd (81);
            for (int i = 0; i < rain.getNumSamples(); ++i)
            {
                rain.setSample (0, i, (rainRnd.nextFloat() - 0.5f) * 0.2f);
                rain.setSample (1, i, (rainRnd.nextFloat() - 0.5f) * 0.2f * (float) quietRight);
            }
            const auto steady = detectPanBalance (rain, sr, o);
            check (steady.outcome == PanOutcome::applied && std::abs (steady.differenceDb - 3.1) < 0.25, "a steady sound such as rain is judged too");

            const auto rightLouder = detectPanBalance (events (quietRight, 1.0, 0.002, 0.002, false, 0.0), sr, o);
            check (rightLouder.outcome == PanOutcome::applied && rightLouder.differenceDb < -2.8, "it works the other way round too");

            const auto leaves = [&] (juce::AudioBuffer<float> b, PanOutcome expected, const char* what) {
                const auto copy = b;
                conditionFile (b, sr, o, rep);
                check (detectPanBalance (copy, sr, o).outcome == expected && ! rep.panApplied && identical (b, copy), what);
            };
            leaves (events (1.0, 1.0 / std::pow (10.0, 0.2 / 20.0), 0.002, 0.002, false, 0.0), PanOutcome::balanced, "a difference under 0.5 dB is left alone (bit-exact)");
            leaves (events (1.0, 1.0, 0.002, 0.002, true, 0.0), PanOutcome::imageMoves, "a stereo image that moves during the recording is left alone");
            leaves (events (1.0, 0.25, 0.002, 0.002, false, 0.0), PanOutcome::tooLarge, "a 12 dB difference is taken for an intended pan or a bad channel and left alone");
            leaves (events (1.0, 1.0, 0.02, 0.005, false, 0.0), PanOutcome::balanced, "a lopsided noise floor does not count: only the sound is compared");
            leaves (events (1.0, 1.0, 0.002, 0.002, false, 0.3), PanOutcome::balanced, "low-frequency rumble on one side does not count");

            juce::AudioBuffer<float> mono (1, (int) (sr * 3));
            mono.clear();
            leaves (mono, PanOutcome::notStereo, "mono is not touched");
            juce::AudioBuffer<float> silent (2, (int) (sr * 5));
            silent.clear();
            leaves (silent, PanOutcome::tooShort, "silence cannot be judged and is left alone");
            juce::AudioBuffer<float> blip (2, 4800);
            blip.clear();
            leaves (blip, PanOutcome::tooShort, "a very short recording is left alone");

            check (presetOptions (ConditionPreset::standard).panMode == PanMode::automatic, "the standard preset includes the balance correction");
        }

        // ---- Peak / ceiling / max gain
        {
            ConditionOptions o;
            o.normalize = NormalizeMode::peak;
            o.targetDb = -1.0;

            juce::AudioBuffer<float> b (2, 48000);
            juce::Random rnd (7);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < b.getNumSamples(); ++i)
                    b.setSample (ch, i, (rnd.nextFloat() - 0.5f) * 0.4f);
            conditionCycle (b, 48000.0, o, rep);
            check (std::abs (db (samplePeak (b)) - (-1.0)) < 0.001 && ! rep.gainLimited, "Peak normalisation lands on the target");

            o.targetDb = 0.0;
            auto c = sine (1, 48000.0, 0.5, 1000.0, 0.1);
            conditionCycle (c, 48000.0, o, rep);
            check (std::abs (db (samplePeak (c)) - (-1.0)) < 0.001 && rep.gainLimited, "a target above the ceiling stops at the ceiling (and says so)");

            o.targetDb = -1.0;
            auto quiet = sine (1, 48000.0, 0.5, 1000.0, 1.0e-4);
            conditionCycle (quiet, 48000.0, o, rep);
            check (std::abs (rep.gainDb - 30.0) < 1.0e-9 && rep.gainLimited, "a near-silent cycle is not boosted beyond the maximum gain");

            juce::AudioBuffer<float> silent (2, 4800);
            silent.clear();
            conditionCycle (silent, 48000.0, o, rep);
            check (samplePeak (silent) == 0.0f && rep.gainDb == 0.0, "digital silence is left alone");

            o.targetDb = -20.0;
            auto loud = sine (1, 48000.0, 0.5, 1000.0, 0.9);
            conditionCycle (loud, 48000.0, o, rep);
            check (std::abs (db (samplePeak (loud)) - (-20.0)) < 0.001 && rep.gainDb < 0.0, "loud cycles are brought down as well");
        }

        // ---- LUFS (EBU Tech 3341: a 1 kHz sine of -23 dBFS peak in both channels reads -23.0 LUFS)
        {
            const auto stereo48 = sine (2, 48000.0, 5.0, 1000.0, std::pow (10.0, -23.0 / 20.0));
            const auto stereo44 = sine (2, 44100.0, 5.0, 1000.0, std::pow (10.0, -23.0 / 20.0));
            const auto mono = sine (1, 48000.0, 5.0, 1000.0, std::pow (10.0, -23.0 / 20.0));
            check (std::abs (measureLoudness (stereo48, 48000.0) - (-23.0)) < 0.1, "reference sine reads -23 LUFS at 48 kHz");
            check (std::abs (measureLoudness (stereo44, 44100.0) - (-23.0)) < 0.1, "... and at 44.1 kHz");
            check (std::abs (measureLoudness (mono, 48000.0) - (-26.0)) < 0.1, "a single channel reads 3 dB lower");

            // 3 s of sound, then 10 s of silence: ungated this would read about -26.4; the blocks that straddle the edges
            // still count, so the gated figure sits a fraction of a dB under the sound itself
            auto gated = sine (2, 48000.0, 13.0, 1000.0, std::pow (10.0, -20.0 / 20.0));
            for (int ch = 0; ch < 2; ++ch)
                gated.clear (ch, 3 * 48000, 10 * 48000);
            const auto gatedLufs = measureLoudness (gated, 48000.0);
            check (gatedLufs > -20.8 && gatedLufs < -19.9, "silent stretches are gated out of the measurement");

            const auto shortOne = sine (2, 48000.0, 0.2, 1000.0, std::pow (10.0, -23.0 / 20.0));
            check (std::abs (measureLoudness (shortOne, 48000.0) - (-23.0)) < 0.2, "a sound shorter than one block is measured ungated");

            juce::AudioBuffer<float> zero (2, 48000);
            zero.clear();
            check (measureLoudness (zero, 48000.0) <= -240.0, "silence reads as silence");

            ConditionOptions o;
            o.normalize = NormalizeMode::lufs;
            o.targetDb = -16.0;
            auto b = sine (2, 48000.0, 5.0, 1000.0, std::pow (10.0, -33.0 / 20.0));
            conditionCycle (b, 48000.0, o, rep);
            check (std::abs (measureLoudness (b, 48000.0) - (-16.0)) < 0.05 && std::abs (rep.gainDb - 17.0) < 0.1, "LUFS normalisation lands on the target");

            o.targetDb = 0.0;
            auto c = sine (2, 48000.0, 5.0, 1000.0, std::pow (10.0, -23.0 / 20.0));
            conditionCycle (c, 48000.0, o, rep);
            check (std::abs (db (samplePeak (c)) - (-1.0)) < 0.001 && rep.gainLimited, "a loudness target that would clip stops at the peak ceiling");
        }

        // ---- dither
        {
            juce::AudioBuffer<float> z (1, 200000);
            z.clear();
            addTpdfDither (z, 16, 5);
            const auto lsb = 1.0 / 32768.0;
            check (std::abs (mean (z, 0)) < 0.02 * lsb && std::abs (rms (z, 0, z.getNumSamples()) / lsb - 0.4082) < 0.01
                       && z.getMagnitude (0, 0, z.getNumSamples()) < (float) lsb,
                   "TPDF dither: zero mean, 0.41 LSB rms, never more than 1 LSB");
        }

        // ---- settings: stored, read back, clamped
        {
            ConditionSettings s;
            s.preset = ConditionPreset::custom;
            s.options.dcRemove = true;
            s.options.humMode = HumMode::mains60;
            s.options.panMode = PanMode::automatic;
            s.options.lowCutHz = 35.0f;
            s.options.lowCutOrder = 4;
            s.options.normalize = NormalizeMode::lufs;
            s.options.targetDb = -23.0;
            s.options.ceilingDb = -2.0;
            s.options.dither = false;
            s.previewGain = true;
            const auto json = juce::JSON::toString (conditionSettingsToVar (s));
            check (conditionSettingsFromVar (juce::JSON::parse (json)) == s, "settings survive being written as JSON and read back");

            check (conditionSettingsFromVar ({}) == ConditionSettings {} && ! ConditionSettings {}.options.active(), "no stored settings means everything off");

            auto* wild = new juce::DynamicObject();
            wild->setProperty ("preset", "bogus");
            wild->setProperty ("hum", "bogus");
            wild->setProperty ("pan", "bogus");
            wild->setProperty ("lowCutHz", 99999.0);
            wild->setProperty ("lowCutOrder", 7);
            wild->setProperty ("normalize", "peak");
            wild->setProperty ("targetDb", 55.0);
            wild->setProperty ("ceilingDb", -100.0);
            const auto clamped = conditionSettingsFromVar (juce::var (wild));
            check (clamped.options.humMode == HumMode::off && clamped.options.panMode == PanMode::off && clamped.options.lowCutHz == 500.0f && clamped.options.lowCutOrder == 4
                       && clamped.options.targetDb == 0.0 && clamped.options.ceilingDb == -24.0 && clamped.preset == ConditionPreset::custom,
                   "unknown names fall back to off, numbers are clamped to sensible ranges");

            auto preset = conditionSettingsToVar ({ ConditionPreset::standard, presetOptions (ConditionPreset::standard), false });
            check (conditionSettingsFromVar (preset).preset == ConditionPreset::standard, "a preset that still matches its values stays that preset");
            preset.getDynamicObject()->setProperty ("lowCutHz", 80.0);
            check (conditionSettingsFromVar (preset).preset == ConditionPreset::custom, "a preset whose values were changed by hand becomes custom");
        }

        // ---- listening gain is the gain the exporter applies
        {
            ConditionOptions o;
            o.dcRemove = true;
            o.lowCutHz = 30.0f;
            o.normalize = NormalizeMode::peak;
            o.targetDb = -3.0;

            AudioData data;
            data.sampleRate = 48000.0;
            data.bitsPerSample = 24;
            data.buffer.setSize (2, 48000 * 4);
            juce::Random rnd (21);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < data.buffer.getNumSamples(); ++i)
                    data.buffer.setSample (ch, i, (rnd.nextFloat() - 0.5f) * 0.2f + 0.03f);

            auto stageA = data.buffer;
            conditionFile (stageA, 48000.0, o, rep);
            const auto pg = previewGainFor (stageA, 48000.0, 12000, 60000, o);

            const auto base = testRoot.getChildFile ("condition_preview_test");
            base.deleteRecursively();
            const auto res = exportCycles (data, { { 12000, 60000, "P", "p", "" } }, base, 24, 2.0f, o);
            check (pg.available && res.written == 1 && std::abs (db (pg.gain) - res.minGainDb) < 1.0e-3 && std::abs (pg.report.peakAfterDb - (-3.0)) < 1.0e-3,
                   "the gain heard in A/B listening is the one the export applies");

            juce::AudioBuffer<float> longOne (1, (int) (70.0 * 8000.0));
            longOne.clear();
            const auto tooLong = previewGainFor (longOne, 8000.0, 0, longOne.getNumSamples(), o);
            check (! tooLong.available && tooLong.gain == 1.0, "a range over a minute is not measured (plays at unity)");
            check (previewGainFor (stageA, 48000.0, 0, 48000, ConditionOptions {}).gain == 1.0, "without normalisation the listening gain is 1");
        }

        // ---- the conditioned copy is made in the background and never earlier than asked for
        {
            const double sr = 48000.0;
            auto source = std::make_shared<AudioData>();
            source->sampleRate = sr;
            source->bitsPerSample = 24;
            source->buffer.setSize (2, (int) (sr * 6));
            juce::Random rnd (41);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < source->buffer.getNumSamples(); ++i)
                    source->buffer.setSample (ch, i, (float) ((rnd.nextFloat() - 0.5) * 0.004 + 0.02 + 0.01 * std::sin (2.0 * pi * 60.0 * i / sr)
                                                              + 0.006 * std::sin (2.0 * pi * 120.0 * i / sr) + 0.004 * std::sin (2.0 * pi * 180.0 * i / sr)));
            const AudioDataPtr audioPtr = source;
            const auto untouched = source->buffer;

            const auto pump = [] (const std::function<bool()>& done, int maxMs) {
                for (int t = 0; t < maxMs && ! done(); t += 50)
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
                return done();
            };

            ConditioningPreview preview;
            int changes = 0;
            preview.onChanged = [&] { ++changes; };
            preview.setAudio (audioPtr);

            ConditionOptions o;
            o.dcRemove = true;
            o.humMode = HumMode::automatic;
            o.panMode = PanMode::automatic;
            preview.setOptions (o);
            check (pump ([&] { return preview.humReady(); }, 10000) && preview.getHum().mainsHz == 60.0, "the hum in the recording is looked for in the background");
            check (preview.getProcessed() == nullptr && ! preview.isBuilding(), "no copy is made until one is asked for");
            check (pump ([&] { return preview.panReady(); }, 10000) && preview.getPan().outcome == PanOutcome::balanced, "the left/right balance is measured in the background too");

            preview.requestProcessed();
            check (pump ([&] { return preview.getProcessed() != nullptr; }, 15000), "the conditioned copy arrives");
            const auto copy = preview.getProcessed();
            auto expected = untouched;
            conditionFile (expected, sr, o, rep);
            check (copy != nullptr && copy != audioPtr && identical (copy->buffer, expected) && identical (source->buffer, untouched) && preview.getFileReport().humHz == 60.0,
                   "it is the same as conditioning directly, and the original is untouched");

            // two quick changes: only the last one is built
            auto o2 = o;
            o2.humMode = HumMode::off;
            auto o3 = o;
            o3.humMode = HumMode::off;
            o3.lowCutHz = 100.0f;
            preview.setOptions (o2);
            preview.setOptions (o3);
            check (preview.getProcessed() == nullptr, "a change makes the old copy stale at once");
            check (pump ([&] { return preview.getProcessed() != nullptr; }, 15000), "and a new one is built (the processed copy stays wanted)");
            auto expected3 = untouched;
            conditionFile (expected3, sr, o3, rep);
            check (identical (preview.getProcessed()->buffer, expected3), "it reflects the last setting only");

            // nothing in stage A: no copy at all
            ConditionOptions o4;
            o4.normalize = NormalizeMode::peak;
            preview.setOptions (o4);
            check (pump ([&] { return preview.getProcessed() != nullptr; }, 5000) && preview.getProcessed() == audioPtr, "when stage A has nothing to do the original is used as it is (no copy)");

            preview.releaseProcessed();
            check (preview.getProcessed() == nullptr && changes > 0, "the copy can be dropped again");
        }

        // ---- player: another version of the same recording can be swapped in, anything else is refused
        {
            const auto make = [] (int samples, int channels) {
                auto d = std::make_shared<AudioData>();
                d->sampleRate = 48000.0;
                d->buffer.setSize (channels, samples);
                d->buffer.clear();
                return AudioDataPtr (d);
            };
            PlaybackEngine engine;
            engine.setAudio (make (48000, 2));
            check (engine.swapAudio (make (48000, 2)), "a conditioned version of the same length can be swapped in");
            check (! engine.swapAudio (make (47999, 2)) && ! engine.swapAudio (make (48000, 1)) && ! engine.swapAudio (nullptr),
                   "a different length, channel count or nothing is refused (the play range would not fit)");
        }

        // ---- the main window keeps the settings
        {
            const double sr = 48000.0;
            auto source = std::make_shared<AudioData>();
            source->sampleRate = sr;
            source->bitsPerSample = 24;
            source->buffer.setSize (1, (int) (sr * 3));
            juce::Random rnd (51);
            for (int i = 0; i < source->buffer.getNumSamples(); ++i)
                source->buffer.setSample (0, i, (float) ((rnd.nextFloat() - 0.5) * 0.004 + 0.01 * std::sin (2.0 * pi * 60.0 * i / sr)
                                                         + 0.006 * std::sin (2.0 * pi * 120.0 * i / sr) + 0.004 * std::sin (2.0 * pi * 180.0 * i / sr)));

            ConditionSettings wanted;
            wanted.preset = ConditionPreset::standard;
            wanted.options = presetOptions (ConditionPreset::standard);
            wanted.previewGain = true;

            {
                MainComponent main;
                main.testLoadAudio (source);
                check (main.testConditioning() == ConditionSettings {}, "a fresh start has conditioning off");
                main.testSetConditioning (wanted);
                for (int t = 0; t < 10000 && main.testHumStatus().contains ("..."); t += 50)
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
                check (main.testHumStatus().contains ("60 Hz"), "the panel can say which hum was found");
                check (main.testPanStatus().contains (WT_U8 ("ステレオではない")), "and what it makes of the balance (this one is mono)");
                check (main.testSummary().contains ("Peak -1.0 dBFS"), "the export dialog's summary names what will be applied");
                main.testSaveNow();
            }

            MainComponent again;
            check (again.testConditioning() == wanted, "the settings are there after a restart (shared preferences)");
        }

        // ---- exporter
        {
            const auto base = testRoot.getChildFile ("condition_test");
            base.deleteRecursively();

            AudioData data;
            data.sampleRate = 48000.0;
            data.bitsPerSample = 24;
            data.buffer.setSize (2, 48000 * 4);
            juce::Random rnd (11);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < data.buffer.getNumSamples(); ++i)
                    data.buffer.setSample (ch, i, (rnd.nextFloat() - 0.5f) * 0.1f + 0.05f);
            const auto original = data.buffer;

            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            const auto load = [&] (const juce::File& f, int& bits) {
                std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (f));
                juce::AudioBuffer<float> b;
                if (r != nullptr)
                {
                    bits = (int) r->bitsPerSample;
                    b.setSize ((int) r->numChannels, (int) r->lengthInSamples);
                    r->read (&b, 0, (int) r->lengthInSamples, 0, true, true);
                }
                return b;
            };

            const std::vector<ExportItem> items { { 12000, 60000, "A", "plain", "" }, { 100000, 150000, "A", "plain2", "" } };

            // default options: the same as before this stage existed (samples copied, 2 ms edge fades)
            const auto plain = exportCycles (data, items, base);
            int bits = 0;
            const auto p1 = load (base.getChildFile ("A/plain.wav"), bits);
            double worst = 0.0;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 200; i < p1.getNumSamples() - 200; ++i)
                    worst = juce::jmax (worst, (double) std::abs (p1.getSample (ch, i) - original.getSample (ch, 12000 + i)));
            check (plain.written == 2 && p1.getNumSamples() == 48000 && worst < 1.0e-6 && plain.gainLimited == 0, "default export keeps the samples as they are");

            ConditionOptions o;
            o.dcRemove = true;
            o.lowCutHz = 30.0f;
            o.normalize = NormalizeMode::peak;
            o.targetDb = -1.0;
            const std::vector<ExportItem> items2 { { 12000, 60000, "B", "cond1", "" }, { 100000, 150000, "B", "cond2", "" } };
            const auto res = exportCycles (data, items2, base, 24, 2.0f, o);
            const auto c1 = load (base.getChildFile ("B/cond1.wav"), bits);
            const auto c2 = load (base.getChildFile ("B/cond2.wav"), bits);
            check (res.written == 2 && std::abs (db (samplePeak (c1)) - (-1.0)) < 0.01 && std::abs (db (samplePeak (c2)) - (-1.0)) < 0.01,
                   "each exported file is normalised on its own");
            check (std::abs (mean (c1, 0)) < 0.002 && c1.getNumSamples() == 48000, "DC and low frequencies are gone and the length is as cut");
            check (identical (data.buffer, original), "the loaded recording is never modified");

            juce::MemoryBlock raw;
            base.getChildFile ("B/cond1.wav").loadFileAsData (raw);
            const std::string bytes ((const char*) raw.getData(), raw.getSize());
            check (bytes.find ("cond=dc,lc30Hz/2,norm=-1.0dBFS,gain=") != std::string::npos, "the applied chain is recorded in the WAV comment");

            {
                AudioData humData;
                humData.sampleRate = 48000.0;
                humData.bitsPerSample = 24;
                humData.buffer.setSize (2, 48000 * 6);
                juce::Random hr (5);
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < humData.buffer.getNumSamples(); ++i)
                        humData.buffer.setSample (ch, i, (float) ((hr.nextFloat() - 0.5) * 0.004 + 0.01 * std::sin (2.0 * pi * 60.0 * i / 48000.0)
                                                                  + 0.006 * std::sin (2.0 * pi * 120.0 * i / 48000.0) + 0.004 * std::sin (2.0 * pi * 180.0 * i / 48000.0)));

                ConditionOptions ho;
                ho.humMode = HumMode::automatic;
                const std::vector<ExportItem> humItems { { 0, 6 * 48000, "H", "hum", "" } };
                const auto hr1 = exportCycles (humData, humItems, base, 24, 2.0f, ho);
                juce::MemoryBlock hraw;
                base.getChildFile ("H/hum.wav").loadFileAsData (hraw);
                const std::string hbytes ((const char*) hraw.getData(), hraw.getSize());
                check (hr1.humHz == 60.0 && hr1.humNotches >= 3 && hbytes.find ("cond=hum60Hz/") != std::string::npos,
                       "the exporter reports the hum it removed and writes it into the comment");
            }

            {
                AudioData panData;
                panData.sampleRate = 48000.0;
                panData.bitsPerSample = 24;
                panData.buffer.setSize (2, 48000 * 8);
                juce::Random pr (71);
                for (int i = 0; i < panData.buffer.getNumSamples(); ++i)
                {
                    const auto burst = std::fmod (i / 48000.0, 1.0) >= 0.2 && std::fmod (i / 48000.0, 1.0) < 0.5;
                    panData.buffer.setSample (0, i, (float) ((pr.nextFloat() - 0.5) * (burst ? 0.4 : 0.004)));
                    panData.buffer.setSample (1, i, (float) ((pr.nextFloat() - 0.5) * (burst ? 0.28 : 0.004)));
                }

                ConditionOptions po;
                po.panMode = PanMode::automatic;
                const auto pres = exportCycles (panData, { { 0, 8 * 48000, "Pan", "pan", "" } }, base, 24, 2.0f, po);
                int pbits = 0;
                const auto pf = load (base.getChildFile ("Pan/pan.wav"), pbits);
                const auto channelEnergy = [&] (int ch) {
                    double sum = 0.0;
                    for (int i = 0; i < pf.getNumSamples(); ++i)
                        sum += (double) pf.getSample (ch, i) * pf.getSample (ch, i);
                    return 10.0 * std::log10 (sum);
                };
                juce::MemoryBlock praw;
                base.getChildFile ("Pan/pan.wav").loadFileAsData (praw);
                const std::string pbytes ((const char*) praw.getData(), praw.getSize());
                check (pres.panApplied && pres.panDb > 2.5 && std::abs (channelEnergy (0) - channelEnergy (1)) < 0.3 && pbytes.find ("cond=pan+") != std::string::npos,
                       "the exporter balances the two sides and says so in the result and in the comment");
            }

            const auto again = exportCycles (data, items2, base, 24, 2.0f, o);
            check (again.written == 0 && again.skippedExisting.size() == 2, "existing files are never overwritten");

            const std::vector<ExportItem> items3 { { 12000, 60000, "C", "sixteen", "" } };
            const auto r16 = exportCycles (data, items3, base, 16, 2.0f, o);
            const auto s16 = load (base.getChildFile ("C/sixteen.wav"), bits);
            check (r16.written == 1 && bits == 16 && std::abs (db (samplePeak (s16)) - (-1.0)) < 0.01, "16-bit export (with dither) works and still lands on the target");
        }

        return ok;
    }

    bool selfTestRating()
    {
        const auto testRoot = juce::File (juce::SystemStats::getEnvironmentVariable ("WT_APPDATA", {}));
        if (testRoot.getFullPathName().isEmpty())
        {
            std::printf ("FAIL  refusing to run: WT_APPDATA is not set\n");
            return false;
        }

        bool ok = true;
        const auto check = [&ok] (bool cond, const char* what) {
            std::printf ("%s  %s\n", cond ? "PASS" : "FAIL", what);
            ok = ok && cond;
        };

        // ---- attribute text
        {
            const auto tags = parseAttributes ("  strength=strong   material=\"cardboard box\" outdoor ");
            check (tags.size() == 3 && tags[0] == AttributeTag { "strength", "strong" } && tags[1] == AttributeTag { "material", "cardboard box" }
                       && tags[2] == AttributeTag { "", "outdoor" },
                   "attribute text is split into key=value words (quotes keep a space, a bare word has no key)");
            check (formatAttributes (tags) == "strength=strong material=\"cardboard box\" outdoor", "and written back with quotes only where they are needed");
            check (normaliseAttributes ("  a=1    b=2 ") == "a=1 b=2" && normaliseAttributes ("   ").isEmpty() && parseAttributes ("").empty(), "stray white space disappears, empty stays empty");
            check (normaliseAttributes (normaliseAttributes ("x=\"a b\" y=2")) == normaliseAttributes ("x=\"a b\" y=2"), "normalising twice changes nothing");

            auto text = withAttribute ("strength=weak speed=fast", "STRENGTH", "strong");
            check (text == "strength=strong speed=fast", "setting a key again replaces its value in place (case does not matter)");
            text = withAttribute (text, "mic", "contact");
            check (text == "strength=strong speed=fast mic=contact", "a new key is appended");
            check (withAttribute (text, "speed", "  ") == "strength=strong mic=contact", "an empty value removes the key");
        }

        // ---- vocabulary
        {
            AttributeVocabulary vocab;
            const auto keys = vocab.keys();
            check (keys.size() == 6 && keys[0] == "strength" && keys[5] == "distance", "the built-in keys are there from the start, in a fixed order");
            check (vocab.valuesFor ("strength") == juce::StringArray ({ "weak", "medium", "strong" }), "with their typical values in natural order");

            vocab.learn ("tool=hammer tool=hammer tool=saw speaker=left outdoor");
            vocab.learn ("Speaker=right strength=strong");
            const auto after = vocab.keys();
            check (after.size() == 7 && after[6] == "speaker" && after.contains ("tool"), "keys that were used before are offered after the built-in ones");
            check (vocab.valuesFor ("tool") == juce::StringArray ({ "hammer", "saw" }), "values come most used first");
            check (vocab.valuesFor ("strength")[0] == "strong" && vocab.valuesFor ("strength").size() == 3, "a used built-in value moves up, the others stay on offer");
            check (vocab.valuesFor ("speaker").size() == 2 && vocab.valuesFor ("nothing").isEmpty(), "keys are matched without regard to case; unknown keys have no values");
        }

        // ---- cycles: stars, NG, attributes, undo
        {
            CycleModel m;
            m.setTypeName (0, "push");
            m.add (1000, 2000, 0);
            m.add (3000, 4000, 0);
            m.add (5000, 6000, 0);

            m.setRating ({ 0, 2 }, 4);
            check (m[0].rating == 4 && m[1].rating == 0 && m[2].rating == 4, "stars go onto every selected cycle");
            m.setRating ({ 0 }, 9);
            check (m[0].rating == 5, "more than five stars is five");
            m.setRating ({ 0 }, -3);
            check (m[0].rating == 0, "and less than none is none");
            m.undo();
            check (m[0].rating == 5, "a rating change is one undo step");

            m.setNg ({ 1, 2 }, true);
            check (! m[0].ng && m[1].ng && m[2].ng, "NG goes onto every selected cycle");
            m.undo();
            check (! m[1].ng && ! m[2].ng, "and is one undo step");

            m.setAttributes ({ 0, 1, 2 }, "strength=strong");
            check (m[0].attributes == "strength=strong" && m[2].attributes == "strength=strong", "attributes can be set on several cycles at once");
            m.undo();
            check (m[0].attributes.isEmpty() && m[1].attributes.isEmpty(), "in one undo step");
            m.setAttributes ({ 0 }, "x=1");
            m.setAttributes ({ 0 }, "x=1");
            m.undo();
            check (m[0].attributes.isEmpty(), "setting the text a cycle already has adds no undo step of its own");

            m.setRating ({ 1 }, 3);
            m.setNg ({ 1 }, true);
            m.setAttributes (1, "material=wood outdoor");

            // survives the project file and the WAV's wtag chunk
            CycleModel fromProject;
            fromProject.setTypeName (0, "push");
            fromProject.cyclesFromVar (juce::JSON::parse (juce::JSON::toString (m.cyclesToVar())));
            check (fromProject.size() == 3 && fromProject[1].rating == 3 && fromProject[1].ng && fromProject[1].attributes == "material=wood outdoor"
                       && fromProject[0].rating == 5 && ! fromProject[0].ng,
                   "stars, NG and attributes survive the project file");

            const auto request = buildEmbedRequest (m, 48000.0, "test");
            WtagData wtag;
            check (parseWtag (juce::JSON::parse (request.wtagJson), wtag) && wtag.cycles.size() == 3 && wtag.cycles[1].rating == 3 && wtag.cycles[1].ng
                       && wtag.cycles[1].attributes == "material=wood outdoor",
                   "and the WAV's wtag chunk");
        }

        // ---- shortcuts
        {
            KeyMap keys;
            const auto ctrlKey = [] (int code) { return juce::KeyPress (code, juce::ModifierKeys::ctrlModifier, 0); };
            bool rates = true;
            for (int i = 0; i <= 5; ++i)
                rates = rates && keys.find (ctrlKey ('0' + i)) == (Action) ((int) Action::Rate0 + i);
            check (rates, "Ctrl+0..5 are the star keys");
            check (keys.find (juce::KeyPress ('N', juce::ModifierKeys(), 'N')) == Action::ToggleNg && keys.find (juce::KeyPress ('E', juce::ModifierKeys(), 'E')) == Action::EditAttributes,
                   "N toggles NG, E edits the attributes");
            check (keys.find (juce::KeyPress ('3', juce::ModifierKeys(), '3')) == Action::Type3 && keys.find (ctrlKey ('3')) == Action::Rate3,
                   "the digits alone are still the type keys");
        }

        // ---- the main window: keys act on the selection, export leaves NG out
        {
            const double sr = 48000.0;
            auto source = std::make_shared<AudioData>();
            source->sampleRate = sr;
            source->bitsPerSample = 24;
            source->buffer.setSize (1, (int) (sr * 6));
            source->buffer.clear();
            source->file = testRoot.getChildFile ("rating_test.wav");

            MainComponent main;
            main.testLoadAudio (source);
            auto& cycles = main.testCyclesMutable();
            cycles.setTypeName (0, "push");
            cycles.setCategory ("");
            for (int i = 0; i < 4; ++i)
                cycles.add ((juce::int64) (sr * (0.5 + i * 1.2)), (juce::int64) (sr * (1.0 + i * 1.2)), i == 3 ? -1 : 0);

            const auto press = [&] (int code, bool ctrl, juce::juce_wchar ch = 0) {
                return main.keyPressed (juce::KeyPress (code, ctrl ? juce::ModifierKeys::ctrlModifier : juce::ModifierKeys(), ch));
            };

            main.testSelectRows ({ 0, 1 });
            check (press ('3', true) && cycles[0].rating == 3 && cycles[1].rating == 3 && cycles[2].rating == 0, "Ctrl+3 gives the selected cycles three stars");
            press ('0', true);
            check (cycles[0].rating == 0 && cycles[1].rating == 0, "Ctrl+0 takes them off");

            press ('N', false, 'N');
            check (cycles[0].ng && cycles[1].ng && ! cycles[2].ng, "N marks the selection NG");
            press ('N', false, 'N');
            check (! cycles[0].ng && ! cycles[1].ng, "N again clears it");
            cycles.setNg ({ 0 }, true);
            press ('N', false, 'N');
            check (cycles[0].ng && cycles[1].ng, "with a mixed selection N makes all of it NG");

            main.testSelectRows ({});
            main.testSetCursor ((juce::int64) (sr * 3.2));
            press ('4', true);
            check (cycles[2].rating == 4 && cycles[0].rating == 0, "with nothing selected the keys act on the cycle under the cursor");

            cycles.setAttributes (2, "strength=strong material=\"cardboard box\"");
            int untyped = 0, ng = 0;
            const auto items = main.testExportItems (untyped, ng);
            check (items.size() == 1 && untyped == 1 && ng == 2, "export leaves out the two NG cycles and the untyped one, and counts them");
            check (items.size() == 1 && items[0].comment.contains ("rating=4") && items[0].comment.contains ("attrs=strength=strong material=\"cardboard box\""),
                   "the stars and attributes go into the exported file's comment");

            cycles.setNg ({ 0, 1 }, false);
            cycles.setRating ({ 0 }, 0);
            const auto all = main.testExportItems (untyped, ng);
            check (all.size() == 3 && ng == 0 && ! all[0].comment.contains ("rating=") && ! all[0].comment.contains ("attrs="),
                   "nothing is added to the comment when there are no stars or attributes");
        }

        return ok;
    }

    bool selfTestDataset()
    {
        const auto testRoot = juce::File (juce::SystemStats::getEnvironmentVariable ("WT_APPDATA", {}));
        if (testRoot.getFullPathName().isEmpty())
        {
            std::printf ("FAIL  refusing to run: WT_APPDATA is not set\n");
            return false;
        }

        bool ok = true;
        const auto check = [&ok] (bool cond, const char* what) {
            std::printf ("%s  %s\n", cond ? "PASS" : "FAIL", what);
            ok = ok && cond;
        };

        const auto base = testRoot.getChildFile ("dataset_test");
        base.deleteRecursively();

        // The main-window part below reads the catalogue and the project files in WT_APPDATA: start from a clean slate, so that what
        // other tests (the catalogue test rewrites it) or an earlier run left there cannot change the result. Test folder only.
        testRoot.getChildFile ("config").deleteRecursively();
        testRoot.getChildFile ("projects").deleteRecursively();
        testRoot.getChildFile ("catalog_state.json").deleteFile();

        // ---- ids and captions
        {
            const auto id = datasetClipId ("Take1.wav", 1000, 10, 20);
            check (id.length() == 32 && id == datasetClipId ("take1.WAV", 1000, 10, 20), "a clip id is 32 hex characters and ignores the case of the file name");
            check (id != datasetClipId ("take1.wav", 1001, 10, 20) && id != datasetClipId ("take1.wav", 1000, 11, 20) && id != datasetClipId ("take2.wav", 1000, 10, 20),
                   "and changes with the size, the range and the recording");

            DatasetClip c;
            c.category = "TOOLS";
            c.categoryJa = WT_U8 ("工具");
            c.type = "Open";
            c.typeJa = WT_U8 ("開く");
            c.attributes = "strength=strong material=\"cardboard box\" outdoor";
            check (datasetCaptionEn (c) == "Open (TOOLS), strength strong, material cardboard box, outdoor", "the English caption is built from type, category and attributes");
            check (datasetCaptionJa (c) == WT_U8 ("開く (工具)、strength strong、material cardboard box、outdoor"), "the Japanese one uses the catalogue's aliases");
            c.typeJa = {};
            c.categoryJa = {};
            c.attributes = {};
            check (datasetCaptionJa (c) == "Open (TOOLS)" && datasetCaptionEn (c) == "Open (TOOLS)", "without aliases or attributes both fall back to the plain names");
        }

        // a 6 s stereo recording with three noise bursts
        AudioData audio;
        audio.sampleRate = 48000.0;
        audio.bitsPerSample = 24;
        audio.buffer.setSize (2, 48000 * 6);
        {
            juce::Random rnd (91);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < audio.buffer.getNumSamples(); ++i)
                {
                    const auto burst = (i >= 24000 && i < 48000) || (i >= 120000 && i < 144000) || (i >= 216000 && i < 240000);
                    audio.buffer.setSample (ch, i, (rnd.nextFloat() - 0.5f) * (burst ? 0.4f : 0.002f));
                }
        }

        const auto clip = [] (const juce::String& file, juce::int64 start, juce::int64 end, const juce::String& name, const juce::String& type) {
            DatasetClip c;
            c.start = start;
            c.end = end;
            c.sourceFile = file;
            c.sourceFrames = 123456;
            c.name = name;
            c.category = "TOOLS";
            c.categoryJa = WT_U8 ("工具");
            c.type = type;
            c.typeJa = type == "Open" ? WT_U8 ("開く") : WT_U8 ("閉じる");
            return c;
        };

        auto openClip = clip ("take.wav", 24000, 48000, "Tools_Open01", "Open");
        openClip.rating = 4;
        openClip.attributes = "strength=strong material=cardboard";
        auto badClip = clip ("take.wav", 120000, 144000, "Tools_Open02", "Open");
        badClip.ng = true;
        auto closeClip = clip ("take.wav", 216000, 240000, "Tools_Close01", "Close");

        const auto folder = base.getChildFile ("set");
        const ConditionOptions none;

        const auto readLines = [&] {
            juce::StringArray lines;
            lines.addLines (folder.getChildFile ("metadata.jsonl").loadFileAsString());
            lines.removeEmptyStrings();
            return lines;
        };
        const auto bytesOf = [] (const juce::File& f) {
            juce::MemoryBlock m;
            f.loadFileAsData (m);
            return m;
        };
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();

        // ---- the first time
        auto r = addToDataset (audio, { openClip, badClip, closeClip }, folder, none, "test");
        check (r.added == 2 && r.skippedNg == 1 && r.errors.isEmpty(), "two clips are added, the new NG one is left out");

        const auto lines = readLines();
        check (lines.size() == 2, "metadata.jsonl has one line per clip");
        const auto first = juce::JSON::parse (lines[0]);
        check (first.getProperty ("id", {}).toString() == datasetClipId ("take.wav", 123456, 24000, 48000) && first.getProperty ("audio", {}).toString() == "audio/Tools_Open01.wav"
                   && first.getProperty ("category", {}).toString() == "TOOLS" && first.getProperty ("type", {}).toString() == "Open"
                   && (int) first.getProperty ("rating", 0) == 4 && ! (bool) first.getProperty ("ng", true),
               "a line carries id, audio path, category, type, rating and NG");
        check (first.getProperty ("caption_ja", {}).toString() == WT_U8 ("開く (工具)、strength strong、material cardboard")
                   && first.getProperty ("caption_en", {}).toString() == "Open (TOOLS), strength strong, material cardboard",
               "both captions are there (and the Japanese text comes back as it went in)");
        check (first.getProperty ("attributes", {}).getProperty ("strength", {}).toString() == "strong" && first.getProperty ("tags_en", {}).getArray() != nullptr
                   && first.getProperty ("tags_en", {}).getArray()->contains ("strong") && first.getProperty ("tags_ja", {}).getArray()->contains (WT_U8 ("工具")),
               "attributes are a key / value object and the tags are lists (English and Japanese)");
        check (std::abs ((double) first.getProperty ("duration", 0.0) - 0.5) < 1.0e-9 && (int) first.getProperty ("channels", 0) == 2
                   && (int) first.getProperty ("bits", 0) == 24 && first.getProperty ("original", {}).getProperty ("file", {}).toString() == "take.wav"
                   && (juce::int64) first.getProperty ("original", {}).getProperty ("start", 0) == 24000,
               "duration, format and the place in the original recording are recorded");

        const auto wavFile = folder.getChildFile ("audio/Tools_Open01.wav");
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (wavFile));
        check (reader != nullptr && reader->lengthInSamples == 24000 && reader->bitsPerSample == 24 && reader->numChannels == 2 && reader->sampleRate == 48000.0,
               "the clip's audio is written as a 24-bit WAV of the right length");
        reader.reset();
        check (folder.getChildFile ("dataset.json").existsAsFile() && ! folder.getChildFile ("audio/Tools_Open02.wav").exists(), "dataset.json describes the format; the NG clip has no audio");

        int stray = 0;
        for (const auto& f : folder.findChildFiles (juce::File::findFilesAndDirectories, false))
            stray += (f.getFileName() == "metadata.jsonl" || f.getFileName() == "dataset.json" || f.getFileName() == "audio") ? 0 : 1;
        check (stray == 0, "no temporary files are left behind");

        // ---- again: nothing changes
        const auto audioBefore = bytesOf (wavFile);
        const auto metaBefore = bytesOf (folder.getChildFile ("metadata.jsonl"));
        r = addToDataset (audio, { openClip, badClip, closeClip }, folder, none, "test");
        check (r.added == 0 && r.updated == 0 && r.ngMarked == 0 && r.unchanged == 2 && r.skippedNg == 1, "adding the same clips again changes nothing");
        check (bytesOf (wavFile) == audioBefore && bytesOf (folder.getChildFile ("metadata.jsonl")) == metaBefore, "audio and metadata are byte for byte the same");

        // ---- a change of mind about stars and attributes
        openClip.rating = 5;
        openClip.attributes = "strength=weak material=cardboard distance=close";
        r = addToDataset (audio, { openClip, closeClip }, folder, none, "test");
        const auto updated = juce::JSON::parse (readLines()[0]);
        check (r.updated == 1 && r.added == 0 && (int) updated.getProperty ("rating", 0) == 5 && updated.getProperty ("attributes", {}).getProperty ("distance", {}).toString() == "close"
                   && updated.getProperty ("exportedAt", {}).toString() == first.getProperty ("exportedAt", {}).toString() && updated.hasProperty ("updatedAt"),
               "new stars and attributes update the line (and say when), the audio is not written again");
        check (bytesOf (wavFile) == audioBefore, "the audio file was not touched");

        // ---- NG afterwards: flagged, nothing deleted
        openClip.ng = true;
        r = addToDataset (audio, { openClip, closeClip }, folder, none, "test");
        check (r.ngMarked == 1 && (bool) juce::JSON::parse (readLines()[0]).getProperty ("ng", false) && wavFile.existsAsFile(), "a clip that becomes NG is flagged in the metadata and its audio stays");
        openClip.ng = false;
        r = addToDataset (audio, { openClip, closeClip }, folder, none, "test");
        check (r.updated == 1 && ! (bool) juce::JSON::parse (readLines()[0]).getProperty ("ng", true), "and the flag goes away again when NG is taken off");

        // ---- another recording with the same output name
        auto other = clip ("other.wav", 24000, 48000, "Tools_Open01", "Open");
        r = addToDataset (audio, { other }, folder, none, "test");
        const auto otherId = datasetClipId ("other.wav", 123456, 24000, 48000);
        check (r.added == 1 && folder.getChildFile ("audio/Tools_Open01_" + otherId.substring (0, 8) + ".wav").existsAsFile() && bytesOf (wavFile) == audioBefore,
               "a name that is taken gets the id appended; the existing audio is not overwritten");

        // ---- lines that are not ours stay
        {
            const auto metaFile = folder.getChildFile ("metadata.jsonl");
            auto text = metaFile.loadFileAsString();
            text += "this is not json\n{\"other\": 1}\n";
            metaFile.replaceWithText (text, false, false, "\n");

            r = addToDataset (audio, { clip ("third.wav", 120000, 144000, "Tools_Open03", "Open") }, folder, none, "test");
            const auto after = readLines();
            check (r.added == 1 && after.contains ("this is not json") && after.contains ("{\"other\": 1}") && after.size() == 6,
                   "lines that are not clips are kept exactly as they were");
        }

        // ---- conditioning goes into the audio and is recorded
        {
            ConditionOptions o;
            o.normalize = NormalizeMode::peak;
            o.targetDb = -3.0;
            const auto cf = base.getChildFile ("conditioned");
            r = addToDataset (audio, { openClip }, cf, o, "test");
            std::unique_ptr<juce::AudioFormatReader> cr (formats.createReaderFor (cf.getChildFile ("audio/Tools_Open01.wav")));
            float lo = 0.0f, hi = 0.0f;
            juce::AudioBuffer<float> b (2, (int) cr->lengthInSamples);
            cr->read (&b, 0, (int) cr->lengthInSamples, 0, true, true);
            const auto peak = samplePeak (b);
            juce::ignoreUnused (lo, hi);
            check (r.added == 1 && std::abs (20.0 * std::log10 (peak) - (-3.0)) < 0.01
                       && juce::JSON::parse (cf.getChildFile ("metadata.jsonl").loadFileAsString().upToFirstOccurrenceOf ("\n", false, false)).getProperty ("conditioning", {}).toString().contains ("norm=-3.0dBFS"),
                   "the conditioning is applied to the clip and written into its metadata");
        }

        // ---- clips that cannot be added are reported
        {
            auto beyond = clip ("take.wav", 0, 48000 * 99, "Tools_Beyond01", "Open");
            auto untyped = clip ("take.wav", 0, 100, "Tools_X01", "");
            r = addToDataset (audio, { beyond, untyped }, folder, none, "test");
            check (r.added == 0 && r.errors.size() == 2, "a clip outside the recording or without a type is an error, not a line");
        }

        // ---- the main window prepares the clips (with the catalogue's Japanese)
        {
            auto source = std::make_shared<AudioData>();
            source->sampleRate = 48000.0;
            source->bitsPerSample = 24;
            source->buffer.setSize (1, 48000 * 4);
            source->buffer.clear();
            source->file = base.getChildFile ("window_take.wav");
            source->file.replaceWithText ("x");

            MainComponent main;
            main.testLoadAudio (source);
            auto& cycles = main.testCyclesMutable();
            cycles.setTypeName (0, "Hit");
            cycles.setCategory ("TOOLS");
            cycles.add (1000, 20000, 0);
            cycles.add (30000, 50000, 0);
            cycles.add (60000, 80000, -1);
            cycles.setRating ({ 0 }, 3);
            cycles.setNg ({ 1 }, true);
            cycles.setAttributes (0, "strength=strong");

            int untyped = 0;
            const auto clips = main.testDatasetClips (untyped);
            check (clips.size() == 2 && untyped == 1 && clips[0].rating == 3 && clips[1].ng && clips[0].attributes == "strength=strong",
                   "typed cycles become clips (NG ones too: the dataset flags them), untyped ones are counted");
            check (clips.size() == 2 && clips[0].categoryJa == WT_U8 ("工具") && clips[0].typeJa == WT_U8 ("叩く") && clips[0].sourceFile == "window_take.wav" && clips[0].sourceSize == 1,
                   "the Japanese names come from the catalogue");
        }

        // ---- the whole Raw folder
        {
            const auto raw = base.getChildFile ("raw");
            const auto sub = raw.getChildFile ("2026/sub");
            sub.createDirectory();

            const auto writeWav = [&] (const juce::File& f, double seconds) {
                juce::WavAudioFormat wav;
                std::unique_ptr<juce::OutputStream> stream (f.createOutputStream());
                auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions {}.withSampleRate (48000.0).withNumChannels (2).withBitsPerSample (24));
                juce::AudioBuffer<float> b (2, (int) (48000.0 * seconds));
                juce::Random r (f.getFileName().hashCode());
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < b.getNumSamples(); ++i)
                        b.setSample (ch, i, (r.nextFloat() - 0.5f) * 0.3f);
                writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
                return (juce::int64) b.getNumSamples();
            };

            // the tags a recording would carry after the editor saved it
            const auto tag = [&] (const juce::File& f, int rating, bool thirdIsNg, bool withUnconfirmed) {
                CycleModel m;
                m.setTypeName (0, "Open");
                m.setCategory ("TOOLS");
                m.add (4800, 24000, 0);
                m.add (28800, 48000, 0);
                m.add (52800, 72000, withUnconfirmed ? -1 : 0);
                m.setRating ({ 0 }, rating);
                m.setAttributes (0, "strength=strong");
                if (thirdIsNg)
                    m.setNg ({ 2 }, true);
                if (withUnconfirmed)
                {
                    m.setSuggestion (2, "Open", 0.9f, 0, true); // typed by a suggestion, never looked at
                    m.add (76800, 96000, -1);                    // and one that has no type at all
                }
                return embedMarkers (f, f, buildEmbedRequest (m, 48000.0, "t")).ok;
            };

            const auto fileA = raw.getChildFile ("2026/A.wav"), fileB = sub.getChildFile ("B.wav"), fileC = raw.getChildFile ("2026/C.wav"), fileD = raw.getChildFile ("2026/D.wav");
            writeWav (fileA, 4.0);
            writeWav (fileB, 4.0);
            writeWav (fileC, 3.0);
            fileD.replaceWithText ("not a wav");
            check (tag (fileA, 4, true, false) && tag (fileB, 2, false, true), "(test setup) two recordings are tagged, one has no tags, one is not audio");

            JapaneseNames names;
            names.category["tools"] = WT_U8 ("工具");
            names.type["tools|open"] = WT_U8 ("開く");

            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            const auto folder = base.getChildFile ("batch_set");
            const auto lines = [&] {
                juce::StringArray l;
                l.addLines (folder.getChildFile ("metadata.jsonl").loadFileAsString());
                l.removeEmptyStrings();
                return l;
            };
            const auto bytesOf = [] (const juce::File& f) {
                juce::MemoryBlock m;
                f.loadFileAsData (m);
                return m;
            };

            const auto rawBefore = bytesOf (fileA);
            int calls = 0, seenTotal = 0;
            auto r = addRawFolderToDataset (raw, folder, formats, names, ConditionOptions {}, "test", nullptr, [&] (int, int total, const juce::String&) {
                ++calls;
                seenTotal = total;
                return true;
            });
            check (r.wavFiles == 4 && r.withClips == 2 && r.noTags == 2 && r.audioRead == 2 && calls == 4 && seenTotal == 4 && ! r.cancelled,
                   "every WAV under the folder is looked at (sub folders too); only the two tagged ones count, and progress is reported");
            check (r.total.added == 4 && r.total.skippedNg == 1 && r.unconfirmed == 1 && r.untyped == 1 && r.total.errors.isEmpty(),
                   "confirmed clips are added; a new NG one, a suggestion nobody confirmed and an untyped cycle are not");
            check (lines().size() == 4 && bytesOf (fileA) == rawBefore, "four lines were written and the Raw file is untouched");

            const auto first = juce::JSON::parse (lines()[0]);
            check (first.getProperty ("category", {}).toString() == "TOOLS" && first.getProperty ("caption_ja", {}).toString().startsWith (WT_U8 ("開く (工具)"))
                       && (int) first.getProperty ("rating", 0) == 4 && first.getProperty ("original", {}).getProperty ("file", {}).toString() == "A.wav",
                   "category, stars and the Japanese names come from the file's tags and the catalogue");

            // the second run reads no audio at all and changes nothing
            const auto meta = bytesOf (folder.getChildFile ("metadata.jsonl"));
            r = addRawFolderToDataset (raw, folder, formats, names, ConditionOptions {}, "test", nullptr);
            check (r.audioRead == 0 && r.total.added == 0 && r.total.updated == 0 && r.total.unchanged == 4 && bytesOf (folder.getChildFile ("metadata.jsonl")) == meta,
                   "running it again reads no audio and changes nothing");

            // a change of stars is picked up without reading the recording
            check (tag (fileA, 5, true, false), "(test setup) stars changed in the tags");
            r = addRawFolderToDataset (raw, folder, formats, names, ConditionOptions {}, "test", nullptr);
            check (r.audioRead == 0 && r.total.updated == 1 && (int) juce::JSON::parse (lines()[0]).getProperty ("rating", 0) == 5, "a new rating is brought over without reading the audio");

            // adding from the editor and then writing markers must not make the same clips twice
            const auto fileE = raw.getChildFile ("2026/E.wav");
            const auto framesE = writeWav (fileE, 3.0);
            AudioData audioE;
            {
                juce::String error;
                const auto loaded = loadAudioFile (formats, fileE, error);
                audioE.sampleRate = loaded->sampleRate;
                audioE.buffer = loaded->buffer;
                audioE.file = fileE;
            }
            DatasetClip editorClip;
            editorClip.start = 4800;
            editorClip.end = 24000;
            editorClip.sourceFile = "E.wav";
            editorClip.sourceFrames = framesE;
            editorClip.sourceSize = fileE.getSize();
            editorClip.name = "Tools_Open09";
            editorClip.category = "TOOLS";
            editorClip.type = "Open";
            const auto linesBefore = lines().size();
            r.total = addToDataset (audioE, { editorClip }, folder, ConditionOptions {}, "test");
            check (r.total.added == 1 && lines().size() == linesBefore + 1, "(test setup) a clip is added from the editor");

            const auto sizeBefore = fileE.getSize();
            CycleModel me;
            me.setTypeName (0, "Open");
            me.setCategory ("TOOLS");
            me.add (4800, 24000, 0);
            check (embedMarkers (fileE, fileE, buildEmbedRequest (me, 48000.0, "t")).ok && fileE.getSize() != sizeBefore, "(test setup) markers are written, so the file is bigger");
            r = addRawFolderToDataset (raw, folder, formats, names, ConditionOptions {}, "test", nullptr);
            check (r.audioRead == 0 && r.total.added == 0 && lines().size() == linesBefore + 1,
                   "after the markers were written the same clip is recognised (the id does not depend on the file size)");

            // a line made when ids still contained the file size is recognised and brought up to date
            {
                const auto old = base.getChildFile ("legacy_set");
                old.createDirectory();
                DatasetClip c = editorClip;
                c.sourceSize = 777777;
                const auto legacy = datasetLegacyClipId (c.sourceFile, c.sourceSize, c.start, c.end);
                old.getChildFile ("metadata.jsonl").replaceWithText ("{\"schema\":1,\"id\":\"" + legacy + "\",\"audio\":\"audio/Tools_Open09.wav\",\"rating\":0,\"ng\":false}\n", false, false, "\n");

                int reads = 0;
                const auto result = addToDataset ([&] () -> AudioDataPtr { ++reads; return {}; }, { c }, old, ConditionOptions {}, "test");
                juce::StringArray l;
                l.addLines (old.getChildFile ("metadata.jsonl").loadFileAsString());
                l.removeEmptyStrings();
                check (result.added == 0 && result.updated == 1 && reads == 0 && l.size() == 1
                           && juce::JSON::parse (l[0]).getProperty ("id", {}).toString() == datasetClipId (c.sourceFile, c.sourceFrames, c.start, c.end),
                       "an older line (id with the file size) is found, its id is brought up to date, and no audio is read");
            }

            // it can be stopped
            {
                const auto stopped = base.getChildFile ("stopped_set");
                int seen = 0;
                const auto result = addRawFolderToDataset (raw, stopped, formats, names, ConditionOptions {}, "test", nullptr, [&] (int index, int, const juce::String&) {
                    seen = index;
                    return index < 1;
                });
                check (result.cancelled && seen == 1 && result.total.added == 2, "cancelling stops after the files done so far (here only the first recording)");
            }
        }

        return ok;
    }

    bool selfTestDictionary()
    {
        const auto testRoot = juce::File (juce::SystemStats::getEnvironmentVariable ("WT_APPDATA", {}));
        if (testRoot.getFullPathName().isEmpty())
        {
            std::printf ("FAIL  refusing to run: WT_APPDATA is not set\n");
            return false;
        }

        bool ok = true;
        const auto check = [&ok] (bool cond, const char* what) {
            std::printf ("%s  %s\n", cond ? "PASS" : "FAIL", what);
            ok = ok && cond;
        };

        const auto base = testRoot.getChildFile ("dictionary_test");
        base.deleteRecursively();
        base.createDirectory();

        const auto ja = [] (const char* s) { return juce::String::fromUTF8 (s); };

        // ---- built-in words
        {
            Dictionary d;
            check (d.toJapanese ("strong") == ja ("強い") && d.toEnglish (ja ("強い")) == "strong", "built-in words go both ways");
            check (d.toJapanese ("  Strong ") == ja ("強い") && d.toJapanese (ja ("ＳＴＲＯＮＧ")) == ja ("強い"), "case, outer white space and full-width letters do not matter");
            check (d.toJapanese ("cardboard") == ja ("段ボール") && d.toEnglish (ja ("ダンボール")) == "cardboard" && d.toEnglish (ja ("段ボール")) == "cardboard",
                   "several Japanese words can stand for one English word (the first is the one used going out)");
            check (! d.toJapanese ("weirdword") && ! d.toEnglish (ja ("ざらざら")), "an unknown word has no translation");
            check (d.toJapanese (ja ("段ボール")) == ja ("段ボール") && d.toEnglish ("cardboard") == "cardboard", "a word already in the target language is returned as it is");
            check (! d.toJapanese ("cardboard box") && ! d.toJapanese ("   "), "a phrase is never guessed from its parts");
            check (Dictionary::isJapanese (ja ("段ボール")) && Dictionary::isJapanese (ja ("ダンボールbox")) && ! Dictionary::isJapanese ("cardboard"), "kana and kanji make a word Japanese");
            check (Dictionary::isNeutral ("3m") && Dictionary::isNeutral ("12") && Dictionary::isNeutral ("-") && Dictionary::isNeutral ("-3.5dB")
                       && ! Dictionary::isNeutral ("m3") && ! Dictionary::isNeutral ("strong"),
                   "numbers and quantities such as 3m need no translation");
            check (d.userCount() == 0 && d.entries().size() > 100, "there are over a hundred built-in words and no user entries");
        }

        // ---- the user's words: dictionary.csv
        const auto file = base.getChildFile ("config/dictionary.csv");
        {
            Dictionary d;
            d.setFile (file);
            check (d.setUserEntry ("cardboard box", ja ("段ボール箱")), "a translation can be added (the folder is created)");
            juce::MemoryBlock bytes;
            file.loadFileAsData (bytes);
            const auto* b = static_cast<const juce::uint8*> (bytes.getData());
            check (bytes.getSize() > 3 && b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF && file.loadFileAsString().contains ("English,Japanese,Note"),
                   "dictionary.csv is UTF-8 with a BOM and a header (Excel opens it correctly)");
            check (d.toJapanese ("Cardboard Box") == ja ("段ボール箱") && d.toEnglish (ja ("段ボール箱")) == "cardboard box" && d.userCount() == 1, "it is used straight away, both ways");

            Dictionary other;
            other.setFile (file);
            check (other.toJapanese ("cardboard box") == ja ("段ボール箱") && other.userCount() == 1, "another session (or PC) reading the file sees it");

            check (d.setUserEntry ("strong", ja ("ゴツい")) && d.toJapanese ("strong") == ja ("ゴツい") && d.toEnglish (ja ("強い")) == "strong",
                   "a user translation overrides a built-in one (the built-in word stays for the other direction)");
            check (d.setUserEntry ("STRONG", ja ("力強い")) && d.userCount() == 2 && d.toJapanese ("strong") == ja ("力強い"), "the same English word again replaces the user's entry");
            check (d.removeUserEntry ("Strong") && d.toJapanese ("strong") == ja ("強い") && d.userCount() == 1, "removing it brings the built-in translation back");
            check (! d.removeUserEntry ("hammer") && ! d.removeUserEntry ("nothing at all"), "built-in words cannot be removed");
            check (! d.setUserEntry ("", ja ("あ")) && ! d.setUserEntry ("word", "  "), "an empty side is refused");

            check (d.setUserEntry ("a, b", "\"" + ja ("引用") + "\"") && Dictionary().entries().size() < d.entries().size(), "commas and quotes are allowed");
            Dictionary again;
            again.setFile (file);
            check (again.toJapanese ("a, b") == "\"" + ja ("引用") + "\"", "and survive the file");
        }

        // ---- files written by other tools
        {
            const auto sjis = base.getChildFile("sjis/dictionary.csv");
            sjis.getParentDirectory().createDirectory();
            const auto bytes = encodeShiftJis (ja ("English,Japanese\r\nfoo bar,ほげ\r\n"));
            sjis.replaceWithData (bytes.getData(), bytes.getSize());

            Dictionary d;
            d.setFile (sjis);
            check (d.toJapanese ("foo bar") == ja ("ほげ") && d.userCount() == 1, "a file saved by Excel as plain CSV (Shift-JIS) is read correctly");

            // somebody else edits the file while the app is running
            const auto before = d.userCount();
            sjis.replaceWithText (ja ("English,Japanese\r\nfoo bar,ほげ\r\nrough,ざらざら\r\n"), false, false, "\r\n");
            check (d.reloadIfChanged() && d.toJapanese ("rough") == ja ("ざらざら") && d.userCount() == before + 1, "a change to the file is noticed and read");
            check (! d.reloadIfChanged(), "and only once");

            Dictionary none;
            none.setFile (base.getChildFile ("missing/dictionary.csv"));
            check (none.userCount() == 0 && none.toJapanese ("strong") == ja ("強い"), "a missing file is just an empty dictionary");
        }

        // ---- in the dataset
        {
            Dictionary d;
            d.setFile (base.getChildFile ("dataset_config/dictionary.csv"));
            d.setUserEntry ("cardboard", ja ("段ボール"));

            DatasetClip c;
            c.category = "TOOLS";
            c.categoryJa = ja ("工具");
            c.type = "Open";
            c.typeJa = ja ("開く");
            c.attributes = ja ("strength=strong material=段ボール size=3m texture=ざらざら");

            check (datasetCaptionEn (c, &d) == ja ("Open (TOOLS), strength strong, material cardboard, size 3m, texture ざらざら")
                       && datasetCaptionJa (c, &d) == ja ("開く (工具)、強さ 強い、素材 段ボール、大きさ 3m、texture ざらざら"),
                   "both captions put the attribute words into their own language (what is unknown stays as typed)");
            check (datasetCaptionEn (c) == ja ("Open (TOOLS), strength strong, material 段ボール, size 3m, texture ざらざら"), "without a dictionary nothing is translated");

            // a recording of 2 s with one clip
            AudioData audio;
            audio.sampleRate = 48000.0;
            audio.bitsPerSample = 24;
            audio.buffer.setSize (1, 96000);
            juce::Random rnd (7);
            for (int i = 0; i < audio.buffer.getNumSamples(); ++i)
                audio.buffer.setSample (0, i, (rnd.nextFloat() - 0.5f) * 0.2f);
            c.start = 1000;
            c.end = 48000;
            c.sourceFile = "t.wav";
            c.sourceFrames = 99;
            c.name = "Tools_Open01";

            const auto folder = base.getChildFile ("set");
            auto r = addToDataset (audio, { c }, folder, ConditionOptions {}, "test", &d);
            juce::StringArray lines;
            lines.addLines (folder.getChildFile ("metadata.jsonl").loadFileAsString());
            auto line = juce::JSON::parse (lines[0]);

            check (r.added == 1 && line.getProperty ("caption_ja", {}).toString() == datasetCaptionJa (c, &d)
                       && line.getProperty ("tags_en", {}).getArray()->contains ("cardboard") && line.getProperty ("tags_ja", {}).getArray()->contains (ja ("段ボール"))
                       && line.getProperty ("tags_ja", {}).getArray()->contains (ja ("強い")),
                   "the dataset line has both captions and both tag lists");
            check (line.getProperty ("attributes", {}).getProperty ("material", {}).toString() == ja ("段ボール")
                       && line.getProperty ("attributes_en", {}).getProperty ("material", {}).toString() == "cardboard"
                       && line.getProperty ("attributes_ja", {}).getProperty (ja ("素材"), {}).toString() == ja ("段ボール"),
                   "the attributes stay as typed and come in English and Japanese as well");
            check (r.untranslated.contains ("texture") && r.untranslated.contains (ja ("ざらざら")) && r.untranslated.size() == 2 && ! r.untranslated.contains ("3m")
                       && line.getProperty ("untranslated", {}).getArray()->size() == 2,
                   "words without a translation are listed (numbers are not), in the result and on the line");

            // the user adds the missing words, adds the recording again: captions change, audio does not
            const auto wav = folder.getChildFile ("audio/Tools_Open01.wav");
            juce::MemoryBlock audioBefore;
            wav.loadFileAsData (audioBefore);

            d.setUserEntry ("texture", ja ("質感"));
            d.setUserEntry ("rough", ja ("ざらざら"));
            r = addToDataset (audio, { c }, folder, ConditionOptions {}, "test", &d);
            lines.clear();
            lines.addLines (folder.getChildFile ("metadata.jsonl").loadFileAsString());
            line = juce::JSON::parse (lines[0]);
            juce::MemoryBlock audioAfter;
            wav.loadFileAsData (audioAfter);
            check (r.updated == 1 && r.added == 0 && r.untranslated.isEmpty() && line.getProperty ("caption_en", {}).toString().endsWith ("texture rough")
                       && line.getProperty ("untranslated", {}).getArray()->isEmpty() && audioAfter == audioBefore,
                   "after the translations are added, adding the recording again updates the captions and leaves the audio alone");
        }

        // ---- the main window and the config folder
        {
            auto source = std::make_shared<AudioData>();
            source->sampleRate = 48000.0;
            source->bitsPerSample = 24;
            source->buffer.setSize (1, 48000 * 3);
            source->buffer.clear();
            source->file = base.getChildFile ("w.wav");

            MainComponent main;
            main.testLoadAudio (source);
            check (main.testDictionary().getFile() == main.testCatalog().getDirectory().getChildFile ("dictionary.csv"), "the dictionary lives in the config folder, next to catalog.xlsx");

            auto& cycles = main.testCyclesMutable();
            cycles.setTypeName (0, "Hit");
            cycles.add (1000, 20000, 0);
            cycles.add (30000, 50000, 0);
            cycles.setAttributes (0, ja ("strength=strong tool=かなづち 3m"));
            cycles.setAttributes (1, ja ("tool=かなづち material=wood odd=変"));
            cycles.setNg ({ 1 }, true);
            const auto missing = main.testUntranslated();
            check (missing.size() == 1 && missing.contains (ja ("かなづち")), "the words of this recording without a translation are found (NG cycles and numbers do not count)");
        }

        {
            const auto from = base.getChildFile ("old_config"), to = base.getChildFile ("new_config");
            from.createDirectory();
            from.getChildFile ("dictionary.csv").replaceWithText ("English,Japanese,Note\r\nfoo,bar,\r\n");
            check (Catalog::copyConfigFiles (from, to) == 1 && to.getChildFile ("dictionary.csv").existsAsFile(), "moving to another config folder copies the dictionary too");
            to.getChildFile ("dictionary.csv").replaceWithText ("English,Japanese,Note\r\nmine,mine2,\r\n");
            check (Catalog::copyConfigFiles (from, to) == 0 && to.getChildFile ("dictionary.csv").loadFileAsString().contains ("mine"), "and never over an existing one");
        }

        return ok;
    }

    /** Read-only: how well would the suggestion engine do on real sounds? Folder names of the legacy library act as the "right answer". */
    bool reportLearn()
    {
        const juce::String root ("G:\\SE");
        if (! juce::File (root).isDirectory())
            return false;

        LibraryIndex index;
        index.setRoots (juce::StringArray { root });
        for (int i = 0; i < 3600 && (index.isScanning() || index.getData()->entries.empty()); ++i)
            juce::Thread::sleep (50);
        const auto data = index.getData();

        std::map<juce::String, std::vector<juce::File>> byFolder;
        for (const auto& e : data->entries)
        {
            const juce::File f (e.path);
            if (e.size > 2000 && e.size < 20 * 1024 * 1024 && f.hasFileExtension ("wav"))
                byFolder[f.getParentDirectory().getFullPathName()].push_back (f);
        }

        std::vector<juce::String> folders;
        for (const auto& [name, files] : byFolder)
            if (files.size() >= 24)
                folders.push_back (name);
        std::printf ("folders with >= 24 wav files: %d\n", (int) folders.size());

        const auto stride = juce::jmax (1, (int) folders.size() / 30);
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();

        struct Item
        {
            FeatureVector v;
            juce::String label;
            bool test;
        };
        std::vector<Item> items;
        int used = 0;
        for (size_t i = 0; i < folders.size() && used < 30; i += (size_t) stride, ++used)
        {
            const auto& files = byFolder[folders[i]];
            const auto label = juce::File (folders[i]).getFileName();
            int taken = 0;
            for (size_t j = 0; j < files.size() && taken < 18; j += juce::jmax<size_t> (1, files.size() / 18), ++taken)
            {
                FeatureVector v {};
                if (computeFeatures (formats, files[j], v))
                    items.push_back ({ v, label, taken % 3 == 2 });
            }
        }

        LabelStore store;
        int n = 0;
        for (const auto& it : items)
            if (! it.test)
            {
                LabelSample s;
                s.key = "k" + juce::String (n++);
                s.v = it.v;
                s.type = it.label;
                s.category = "X";
                store.upsert (s);
            }

        Suggester engine;
        engine.rebuild (store);

        int total = 0, right = 0, top3 = 0;
        std::map<int, std::pair<int, int>> bucket; // confidence decile -> (right, total)
        for (const auto& it : items)
            if (it.test)
            {
                const auto list = engine.suggestTop (it.v, 3);
                if (list.empty())
                    continue;
                ++total;
                const auto ok1 = list[0].type == it.label;
                right += ok1 ? 1 : 0;
                for (const auto& sug : list)
                    top3 += sug.type == it.label ? 1 : 0;
                auto& b = bucket[juce::jmin (9, (int) (list[0].confidence * 10.0f))];
                ++b.second;
                b.first += ok1 ? 1 : 0;
            }

        std::printf ("classes=%d train=%d test=%d\n", used, n, total);
        std::printf ("top-1 accuracy = %.1f %%   top-3 accuracy = %.1f %%\n", 100.0 * right / juce::jmax (1, total), 100.0 * top3 / juce::jmax (1, total));
        for (const auto& [decile, rt] : bucket)
            std::printf ("  confidence %d0-%d0%%: %d samples, %.0f %% right\n", decile, decile + 1, rt.second, 100.0 * rt.first / rt.second);
        return true;
    }

    /** Loads whatever workbook is already in the isolated config folder (e.g. one written by openpyxl) and prints what was read. */
    bool selfTestCatalogLoad()
    {
        catalog.setDirectory (Catalog::defaultDirectory());
        std::printf ("categories=%d\n", (int) catalog.getCategories().size());
        for (size_t i = 0; i < juce::jmin<size_t> (3, catalog.getCategories().size()); ++i)
            std::printf ("cat[%d]=%s|%s\n", (int) i, catalog.getCategories()[i].name.toRawUTF8(), catalog.getCategories()[i].jp.toRawUTF8());
        std::printf ("types(TOOLS)=%d\n", (int) catalog.getTypes ("TOOLS", false).size());
        for (const auto& t : catalog.getTypes ("TOOLS", false))
            std::printf ("type=%s|%s\n", t.name.toRawUTF8(), t.jp.toRawUTF8());
        for (const auto& w : catalog.getWarnings())
            std::printf ("warning=%s\n", w.toRawUTF8());
        for (const auto& n : catalog.getNotices())
            std::printf ("notice=%s\n", n.toRawUTF8());
        return true;
    }

    Catalog catalog;
    WheelMap wheelMap;
    KeyMap keyMap;
    theme::LookAndFeel lookAndFeel;
    std::unique_ptr<MainWindow> mainWindow;
};
} // namespace wt

START_JUCE_APPLICATION (wt::WaveTaggerApplication)
