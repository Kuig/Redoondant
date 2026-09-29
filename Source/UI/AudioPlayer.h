#pragma once

#include "../Core/Settings.h"

/** A minimal audio file player (play/pause, stop, seek, volume) for the preview panel.
    The audio device is opened lazily, on the first file loaded.
*/
class AudioPlayer final : public juce::Component,
                          private juce::Timer
{
public:
    AudioPlayer (juce::AudioFormatManager& formats, SettingsScope settings);
    ~AudioPlayer() override;

    /** Loads a file, returning false if it can't be decoded. */
    bool load (const juce::File& file);
    void unload();

    /** Format details of the loaded file (sample rate, channels, embedded metadata...). */
    const juce::StringPairArray& getDetails() const noexcept   { return details; }

    void resized() override;

private:
    juce::AudioFormatManager& formats;
    SettingsScope settings;

    juce::AudioDeviceManager deviceManager;
    juce::AudioSourcePlayer sourcePlayer;
    juce::AudioTransportSource transport;
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    juce::TimeSliceThread readAheadThread { "Audio read-ahead" };
    bool deviceOpen = false;
    juce::StringPairArray details;

    juce::TextButton playButton { "Play" }, stopButton { "Stop" };
    juce::Slider position { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Slider volume { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Label time, volumeLabel { {}, "Volume" };

    void openDevice();
    void togglePlayback();
    void stop();
    void timerCallback() override;
    void updateControls();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioPlayer)
};
