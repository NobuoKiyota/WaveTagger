#include "Core/Catalog.h"
#include "Core/TextDecode.h"
#include "Core/EmbeddedState.h"
#include "Core/Clustering.h"
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
        const auto isSnapshotMode = args.size() >= 1 && (args[0].startsWith ("--snapshot") || args[0].startsWith ("--selftest"));
        if (isSnapshotMode && juce::SystemStats::getEnvironmentVariable ("WT_APPDATA", {}).isEmpty())
        {
            std::fprintf (stderr, "snapshot modes require the WT_APPDATA environment variable\n");
            setApplicationReturnValue (2);
            quit();
            return;
        }

        if (args.size() >= 1 && (args[0] == "--selftest-catalog" || args[0] == "--selftest-catalog-load" || args[0] == "--selftest-prefs"
                                 || args[0] == "--selftest-wav" || args[0] == "--selftest-inbox" || args[0] == "--selftest-watcher" || args[0] == "--selftest-organize"))
        {
            setApplicationReturnValue ((args[0] == "--selftest-catalog" ? selfTestCatalog()
                                        : args[0] == "--selftest-prefs" ? selfTestPrefs()
                                        : args[0] == "--selftest-wav" ? selfTestWav()
                                        : args[0] == "--selftest-inbox" ? selfTestInbox()
                                        : args[0] == "--selftest-watcher" ? selfTestWatcher()
                                        : args[0] == "--selftest-organize" ? selfTestOrganize()
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
