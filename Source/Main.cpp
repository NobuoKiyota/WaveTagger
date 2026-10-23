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
        setResizeLimits (800, 500, 10000, 10000);
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
        mainWindow = std::make_unique<MainWindow>();

        const auto args = getCommandLineParameterArray();
        auto toFile = [] (const juce::String& s) { return juce::File::getCurrentWorkingDirectory().getChildFile (s.unquoted()); };

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
    theme::LookAndFeel lookAndFeel;
    std::unique_ptr<MainWindow> mainWindow;
};
} // namespace wt

START_JUCE_APPLICATION (wt::WaveTaggerApplication)
