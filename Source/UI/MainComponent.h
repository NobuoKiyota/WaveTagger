#pragma once

#include "Core/PlaybackEngine.h"
#include "Core/SpectrogramRenderer.h"
#include "OverviewBar.h"
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

    /** Loads a file, renders, writes a PNG of the whole window and quits (for automated visual checks). */
    void runSnapshot (const juce::File& input, const juce::File& output);

    std::function<void (const juce::String&)> onTitleChanged;

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

    juce::AudioFormatManager formats;
    ViewState view;
    SpectrogramRenderer spectrogram;
    PlaybackEngine playback;
    AudioDataPtr audio;

    juce::TextButton openButton { "Open" }, audioButton { "Audio..." };
    juce::Label fileLabel, blendLabel, floorLabel;
    juce::Slider blendSlider, floorSlider;
    OverviewBar overview { view };
    WaveView waveView { view, spectrogram };
    TransportBar transport;

    juce::ThreadPool loader { juce::ThreadPoolOptions {}.withNumberOfThreads (1) };
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::VBlankAttachment> vblank;
    juce::String statusText;
    bool wasPlaying = false;
    juce::File snapshotOutput;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
} // namespace wt
