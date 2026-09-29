#include "AudioPlayer.h"
#include "../Platform/ComInit.h"

namespace
{
    /** Only the device types a preview needs: on Windows the shared WASAPI output, which avoids
        scanning DirectSound and exclusive-mode devices (slow on some systems).
    */
    class PreviewDeviceManager final : public juce::AudioDeviceManager
    {
    public:
        void createAudioDeviceTypes (juce::OwnedArray<juce::AudioIODeviceType>& types) override
        {
           #if JUCE_WINDOWS
            types.add (juce::AudioIODeviceType::createAudioIODeviceType_WASAPI (juce::WASAPIDeviceMode::shared));
           #else
            AudioDeviceManager::createAudioDeviceTypes (types);
           #endif
        }
    };

    juce::String formatTime (double seconds)
    {
        const auto total = (int) seconds;
        return juce::String (total / 60) + ":" + juce::String (total % 60).paddedLeft ('0', 2);
    }
}

AudioPlayer::AudioPlayer (SettingsScope s)
    : settings (s)
{
    readAheadThread.startThread();

    playButton.onClick = [this] { togglePlayback(); };
    stopButton.onClick = [this] { stop(); };

    position.onDragEnd = [this] { transport.setPosition (position.getValue()); };

    volume.setRange (0.0, 1.0);
    volume.setValue (settings.getDouble ("volume", 0.8), juce::dontSendNotification);
    volume.onValueChange = [this]
    {
        transport.setGain ((float) volume.getValue());
        settings.set ("volume", volume.getValue());
    };
    transport.setGain ((float) volume.getValue());

    time.setJustificationType (juce::Justification::centredRight);

    for (auto* c : std::initializer_list<juce::Component*> { &playButton, &stopButton, &position, &time, &volumeLabel, &volume })
        addAndMakeVisible (c);

    updateControls();
}

AudioPlayer::~AudioPlayer()
{
    if (deviceThread.joinable())
        deviceThread.join();

    unload();

    if (deviceManager != nullptr)
    {
        deviceManager->removeAudioCallback (&sourcePlayer);
        sourcePlayer.setSource (nullptr);
    }

    readAheadThread.stopThread (1000);
}

void AudioPlayer::prepareDevice()
{
    if (deviceRequested)
        return;

    deviceRequested = true;
    deviceThread = std::thread ([this, safeThis = juce::Component::SafePointer<AudioPlayer> (this)]
    {
        const ScopedComInit com;
        auto manager = std::make_unique<PreviewDeviceManager>();
        manager->initialiseWithDefaultDevices (0, 2);

        {
            const juce::ScopedLock lock (openedLock);
            openedManager = std::move (manager);
        }

        juce::MessageManager::callAsync ([safeThis]
        {
            if (safeThis != nullptr)
                safeThis->attachDevice();
        });
    });
}

void AudioPlayer::attachDevice()
{
    {
        const juce::ScopedLock lock (openedLock);
        deviceManager = std::move (openedManager);
    }

    if (deviceManager == nullptr)
        return;

    sourcePlayer.setSource (&transport);
    deviceManager->addAudioCallback (&sourcePlayer);
    updateControls();
}

juce::StringPairArray AudioPlayer::describe (const juce::AudioFormatReader& reader)
{
    juce::StringPairArray details;
    details.set ("Format", reader.getFormatName());
    details.set ("Sample rate", juce::String (reader.sampleRate, 0) + " Hz");
    details.set ("Channels", juce::String ((int) reader.numChannels));
    details.set ("Bit depth", juce::String ((int) reader.bitsPerSample) + (reader.usesFloatingPointData ? " (float)" : ""));

    if (reader.sampleRate > 0)
        details.set ("Duration", formatTime ((double) reader.lengthInSamples / reader.sampleRate));

    details.addArray (reader.metadataValues);
    return details;
}

void AudioPlayer::load (std::shared_ptr<juce::AudioFormatReader> newReader)
{
    unload();

    if (newReader == nullptr)
        return;

    prepareDevice();    // Normally already requested when the preview was shown.

    reader = std::move (newReader);
    readerSource = std::make_unique<juce::AudioFormatReaderSource> (reader.get(), false);
    transport.setSource (readerSource.get(), 32768, &readAheadThread, reader->sampleRate, juce::jmax (1, (int) reader->numChannels));

    position.setRange (0.0, juce::jmax (0.01, transport.getLengthInSeconds()));
    position.setValue (0.0, juce::dontSendNotification);
    updateControls();
}

void AudioPlayer::unload()
{
    stopTimer();
    transport.stop();
    transport.setSource (nullptr);
    readerSource.reset();
    reader.reset();
    updateControls();
}

void AudioPlayer::togglePlayback()
{
    if (transport.isPlaying())
    {
        transport.stop();
    }
    else
    {
        if (transport.hasStreamFinished())
            transport.setPosition (0.0);

        transport.start();
        startTimerHz (20);
    }

    updateControls();
}

void AudioPlayer::stop()
{
    transport.stop();
    transport.setPosition (0.0);
    stopTimer();
    updateControls();
}

void AudioPlayer::timerCallback()
{
    if (! transport.isPlaying())
        stopTimer();

    updateControls();
}

void AudioPlayer::updateControls()
{
    const bool loaded = readerSource != nullptr;
    const bool playing = transport.isPlaying();
    const bool deviceReady = deviceManager != nullptr;

    playButton.setButtonText (playing ? "Pause" : "Play");
    playButton.setEnabled (loaded && deviceReady);
    stopButton.setEnabled (loaded);
    position.setEnabled (loaded);

    if (! position.isMouseButtonDown())
        position.setValue (transport.getCurrentPosition(), juce::dontSendNotification);

    time.setText (! loaded ? juce::String()
                           : (deviceReady ? formatTime (transport.getCurrentPosition()) + " / " + formatTime (transport.getLengthInSeconds())
                                          : juce::String ("Opening audio output...")),
                  juce::dontSendNotification);
}

void AudioPlayer::resized()
{
    auto area = getLocalBounds();

    auto controls = area.removeFromTop (26);
    playButton.setBounds (controls.removeFromLeft (70));
    controls.removeFromLeft (6);
    stopButton.setBounds (controls.removeFromLeft (60));
    time.setBounds (controls);

    area.removeFromTop (4);
    position.setBounds (area.removeFromTop (24));

    auto volumeRow = area.removeFromTop (24);
    volumeLabel.setBounds (volumeRow.removeFromLeft (60));
    volume.setBounds (volumeRow);
}
