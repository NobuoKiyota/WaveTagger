#include "Core/Catalog.h"
#include "Core/TextDecode.h"
#include "Core/EmbeddedState.h"
#include "Core/WavChunks.h"
#include "Core/WavMarkers.h"
#include "Core/Xlsx.h"
#include "UI/CategoryPicker.h"
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
                                 || args[0] == "--selftest-wav"))
        {
            setApplicationReturnValue ((args[0] == "--selftest-catalog" ? selfTestCatalog()
                                        : args[0] == "--selftest-prefs" ? selfTestPrefs()
                                        : args[0] == "--selftest-wav" ? selfTestWav()
                                                                       : selfTestCatalogLoad()) ? 0 : 1);
            quit();
            return;
        }

        if (args.size() >= 2 && (args[0] == "--snapshot-picker" || args[0] == "--snapshot-input"))
        {
            snapshotComponent (args[0] == "--snapshot-picker", toFile (args[1]));
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
    void snapshotComponent (bool picker, const juce::File& output)
    {
        catalog.setDirectory (Catalog::defaultDirectory());
        for (const char* used : { "TOOLS", "TOOLS", "TOOLS", "PAPER", "PAPER", "METAL" })
            catalog.noteUsed (used);

        std::unique_ptr<juce::Component> content;
        if (picker)
            content = std::make_unique<CategoryPicker> (catalog, "TOOLS", [] (const juce::String&) {}, [] {});
        else
        {
            auto input = std::make_unique<InputSettingsComponent> (wheelMap, keyMap, [] {}, [this] { return catalog.getDirectory(); }, [] (const juce::File&) { return juce::String(); });
            if (const auto tab = juce::SystemStats::getEnvironmentVariable ("WT_DEMO_TAB", {}); tab.isNotEmpty())
                input->setCurrentTabIndex (tab.getIntValue());
            content = std::move (input);
        }
        content->setSize (picker ? 400 : 800, picker ? 560 : 580);

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
