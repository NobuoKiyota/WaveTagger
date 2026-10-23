#include "MainComponent.h"
#include "Theme.h"
#include "Util/Text.h"

namespace wt
{
namespace
{
constexpr int toolbarHeight = 38;
constexpr int overviewHeight = 40;
constexpr int transportHeight = 62;
} // namespace

MainComponent::MainComponent()
{
    formats.registerBasicFormats();
    setWantsKeyboardFocus (true);

    for (auto* b : { &openButton, &audioButton })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }
    openButton.onClick = [this] { showOpenDialog(); };
    audioButton.onClick = [this] { showAudioSettings(); };

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

    addAndMakeVisible (overview);
    addAndMakeVisible (waveView);
    addAndMakeVisible (transport);

    spectrogram.onNewImage = [this] { waveView.repaint(); };

    waveView.onCursorClicked = [this] (juce::int64 s) {
        if (playback.isPlaying())
            playback.play (s, -1, transport.isLooping());
    };

    transport.onToStart = [this] { view.clearSelection(); view.setCursor (0); view.ensureVisible (0); };
    transport.onPlay = [this] { if (! playback.isPlaying()) togglePlay(); };
    transport.onStop = [this] { stopPlayback(); };
    transport.onLoopChanged = [this] (bool loop) { playback.setLooping (loop); };

    view.addChangeListener (this);
    vblank = std::make_unique<juce::VBlankAttachment> (this, [this] { onVBlank(); });

    setSize (1400, 860);
}

MainComponent::~MainComponent()
{
    vblank = nullptr;
    view.removeChangeListener (this);
    loader.removeAllJobs (true, 10000);
    spectrogram.onNewImage = nullptr;
}

//==============================================================================
void MainComponent::openFile (const juce::File& file)
{
    stopPlayback();
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

    fileLabel.setText (audio->file.getFullPathName(), juce::dontSendNotification);
    if (onTitleChanged)
        onTitleChanged ("WaveTagger - " + audio->file.getFileName());

    refreshTransport();
    grabKeyboardFocus();

    if (snapshotOutput != juce::File())
    {
        const auto n = audio->getNumSamples();
        view.setSelection (n * 2 / 5, n * 3 / 5);
        view.setCursor (n * 2 / 5);
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
        playback.play (view.getSelectionStart(), view.getSelectionEnd(), transport.isLooping());
    else
        playback.play (view.getCursor(), -1, transport.isLooping());
}

void MainComponent::stopPlayback()
{
    playback.stop();
}

void MainComponent::onVBlank()
{
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

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshTransport();
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
    openButton.setBounds (bar.removeFromLeft (64));
    bar.removeFromLeft (6);
    audioButton.setBounds (bar.removeFromLeft (76));
    bar.removeFromLeft (12);
    floorSlider.setBounds (bar.removeFromRight (130));
    floorLabel.setBounds (bar.removeFromRight (70));
    bar.removeFromRight (10);
    blendSlider.setBounds (bar.removeFromRight (130));
    blendLabel.setBounds (bar.removeFromRight (84));
    fileLabel.setBounds (bar);

    transport.setBounds (r.removeFromBottom (transportHeight));
    overview.setBounds (r.removeFromTop (overviewHeight)
                            .withTrimmedLeft (WaveView::leftStrip)
                            .withTrimmedRight (WaveView::rightAxis));
    r.removeFromTop (4);
    waveView.setBounds (r);
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    const auto code = key.getKeyCode();
    const auto mods = key.getModifiers();
    const auto plain = ! mods.isCommandDown() && ! mods.isAltDown();

    if (mods.isCommandDown() && code == 'O')
    {
        showOpenDialog();
        return true;
    }

    if (audio == nullptr)
        return false;

    if (mods.isCommandDown() && code == 'A')
    {
        view.setSelection (0, audio->getNumSamples());
        return true;
    }

    if (! plain)
        return false;

    const auto centrePx = 0.5 * view.getWidth();

    if (code == juce::KeyPress::spaceKey)          togglePlay();
    else if (code == juce::KeyPress::escapeKey)    view.clearSelection();
    else if (code == juce::KeyPress::homeKey)      { view.setCursor (0); view.ensureVisible (0); }
    else if (code == juce::KeyPress::endKey)       { view.setCursor (audio->getNumSamples()); view.ensureVisible ((double) audio->getNumSamples()); }
    else if (code == 'F')                          view.zoomToFit();
    else if (code == 'Z')                          { if (view.hasSelection()) view.zoomToRange (view.getSelectionStart(), view.getSelectionEnd()); }
    else if (code == 'L')                          { transport.setLooping (! transport.isLooping()); playback.setLooping (transport.isLooping()); }
    else if (key.getTextCharacter() == '+' || key.getTextCharacter() == '=' || code == juce::KeyPress::numberPadAdd)
        view.zoomAround (centrePx, 0.5);
    else if (key.getTextCharacter() == '-' || code == juce::KeyPress::numberPadSubtract)
        view.zoomAround (centrePx, 2.0);
    else if (code == juce::KeyPress::leftKey)      view.scrollPixels (-view.getWidth() * 0.1);
    else if (code == juce::KeyPress::rightKey)     view.scrollPixels (view.getWidth() * 0.1);
    else
        return false;

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
