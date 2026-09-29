#pragma once

#include "../Core/Settings.h"
#include <thread>

/** A minimal audio file player (play/pause, stop, seek, volume) for the preview panel.
    It plays readers prepared elsewhere (opening a file can be slow, so it's done off the
    message thread). The audio device is opened lazily and in the background, see prepareDevice().
*/
class AudioPlayer final : public juce::Component,
                          private juce::Timer
{
public:
    explicit AudioPlayer (SettingsScope settings);
    ~AudioPlayer() override;

    /** Starts opening the audio device on a background thread, if not done yet
        (scanning audio devices can take several seconds).
    */
    void prepareDevice();

    /** Plays from an already opened reader (the player keeps it alive). */
    void load (std::shared_ptr<juce::AudioFormatReader> reader);
    void unload();

    /** Format details of a reader (format, sample rate, channels, bit depth, duration, embedded tags). */
    static juce::StringPairArray describe (const juce::AudioFormatReader& reader);

    void resized() override;

private:
    SettingsScope settings;

    std::unique_ptr<juce::AudioDeviceManager> deviceManager;      ///< Set once opened.
    std::unique_ptr<juce::AudioDeviceManager> openedManager;      ///< Handed over by the opening thread.
    juce::CriticalSection openedLock;
    std::thread deviceThread;
    juce::AudioSourcePlayer sourcePlayer;
    juce::AudioTransportSource transport;
    std::shared_ptr<juce::AudioFormatReader> reader;
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    juce::TimeSliceThread readAheadThread { "Audio read-ahead" };
    bool deviceRequested = false;

    juce::TextButton playButton { "Play" }, stopButton { "Stop" };
    juce::Slider position { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Slider volume { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Label time, volumeLabel { {}, "Volume" };

    void attachDevice();
    void togglePlayback();
    void stop();
    void timerCallback() override;
    void updateControls();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioPlayer)
};
