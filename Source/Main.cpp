#include "Core/Catalog.h"
#include "Core/TextDecode.h"
#include "Core/EmbeddedState.h"
#include "Core/Checks.h"
#include "Core/LabelStore.h"
#include "Core/Suggester.h"
#include "Core/Clustering.h"
#include "Core/Conditioning.h"
#include "Core/Exporter.h"
#include "Core/FeatureStore.h"
#include "Core/HardLinks.h"
#include "Core/InboxQueue.h"
#include "Core/ReclassDb.h"
#include "Core/WavChunks.h"
#include "Core/WavMarkers.h"
#include "Core/Xlsx.h"
#include "UI/CategoryPicker.h"
#include "UI/InboxWindow.h"
#include "UI/OrganizerWindow.h"
#include "UI/InputSettings.h"
#include "UI/MainComponent.h"
#include "UI/Theme.h"

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
                                 || args[0] == "--selftest-wav" || args[0] == "--selftest-inbox" || args[0] == "--selftest-watcher" || args[0] == "--selftest-organize" || args[0] == "--selftest-checks" || args[0] == "--report-checks" || args[0] == "--selftest-learn" || args[0] == "--selftest-condition" || args[0] == "--report-learn"))
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
                                        : args[0] == "--report-learn" ? reportLearn()
                                        : args[0] == "--report-checks" ? reportChecks()
                                                                       : selfTestCatalogLoad()) ? 0 : 1);
            quit();
            return;
        }

        if (args.size() >= 2 && (args[0] == "--snapshot-picker" || args[0] == "--snapshot-input" || args[0] == "--snapshot-inbox" || args[0] == "--snapshot-organizer"))
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
        else if (picker)
            content = std::make_unique<CategoryPicker> (catalog, "TOOLS", [] (const juce::String&) {}, [] {});
        else
        {
            auto input = std::make_unique<InputSettingsComponent> (wheelMap, keyMap, devices, [] {}, [this] { return catalog.getDirectory(); }, [] (const juce::File&) { return juce::String(); });
            if (const auto tab = juce::SystemStats::getEnvironmentVariable ("WT_DEMO_TAB", {}); tab.isNotEmpty())
                input->setCurrentTabIndex (tab.getIntValue());
            content = std::move (input);
        }
        if (mode != "--snapshot-organizer")
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

            const auto again = exportCycles (data, items2, base, 24, 2.0f, o);
            check (again.written == 0 && again.skippedExisting.size() == 2, "existing files are never overwritten");

            const std::vector<ExportItem> items3 { { 12000, 60000, "C", "sixteen", "" } };
            const auto r16 = exportCycles (data, items3, base, 16, 2.0f, o);
            const auto s16 = load (base.getChildFile ("C/sixteen.wav"), bits);
            check (r16.written == 1 && bits == 16 && std::abs (db (samplePeak (s16)) - (-1.0)) < 0.01, "16-bit export (with dither) works and still lands on the target");
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
