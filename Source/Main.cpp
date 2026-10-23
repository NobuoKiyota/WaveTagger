#include "Core/Catalog.h"
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

        if (args.size() >= 1 && args[0] == "--selftest-catalog")
        {
            setApplicationReturnValue (selfTestCatalog() ? 0 : 1);
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
            auto input = std::make_unique<InputSettingsComponent> (wheelMap, keyMap, [] {});
            if (juce::SystemStats::getEnvironmentVariable ("WT_DEMO_TAB", {}).isNotEmpty())
                input->setCurrentTabIndex (1);
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

    /** Exercises the CSV round trip (add, append, reload, duplicate handling) in the isolated data folder. */
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
        catalog.setDirectory (dir);

        bool ok = true;
        const auto check = [&ok] (bool cond, const char* what) {
            std::printf ("%s  %s\n", cond ? "PASS" : "FAIL", what);
            ok = ok && cond;
        };

        const auto baseCats = catalog.getCategories().size();
        check (baseCats > 50, "seed categories written and read back");
        check (catalog.getTypes ("TOOLS").size() > catalog.getTypes ("TOOLS", false).size(), "TOOLS has own + common types");

        check (catalog.addCategory ("ark", juce::String::fromUTF8 ("方舟")), "addCategory ok");
        check (catalog.getCategories().size() == baseCats + 1, "new category appears after reload");
        check (catalog.getCategories().back().name == "ARK" && catalog.getCategories().back().jp == juce::String::fromUTF8 ("方舟"),
               "name upper-cased, Japanese preserved");
        check (catalog.addCategory ("ARK", "dup") && catalog.getCategories().size() == baseCats + 1, "duplicate category not added twice");

        const auto before = catalog.getTypes ("TOOLS", false).size();
        check (catalog.addType ("TOOLS", "Pry", juce::String::fromUTF8 ("こじ開ける, 剥がす")), "addType with comma in Japanese");
        const auto types = catalog.getTypes ("TOOLS", false);
        check (types.size() == before + 1 && types.back().name == "Pry"
                   && types.back().jp == juce::String::fromUTF8 ("こじ開ける, 剥がす"),
               "comma-containing field survives quoting round trip");

        juce::FileInputStream in (catalog.getCategoriesFile());
        char head[3] {};
        in.read (head, 3);
        check ((juce::uint8) head[0] == 0xEF && (juce::uint8) head[1] == 0xBB && (juce::uint8) head[2] == 0xBF, "file keeps UTF-8 BOM for Excel");

        // an external edit (as Excel or another AI would do) is picked up
        catalog.getTypesFile().appendText ("TOOLS,ExternalEdit,x,\r\n", false, false);
        check (catalog.reloadIfChanged(), "external edit detected");
        check (catalog.getTypes ("TOOLS", false).back().name == "ExternalEdit", "externally added type is loaded");
        return ok;
    }

    Catalog catalog;
    WheelMap wheelMap;
    KeyMap keyMap;
    theme::LookAndFeel lookAndFeel;
    std::unique_ptr<MainWindow> mainWindow;
};
} // namespace wt

START_JUCE_APPLICATION (wt::WaveTaggerApplication)
