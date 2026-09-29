#include "AudioPlayer.h"

namespace
{
    juce::String formatTime (double seconds)
    {
        const auto total = (int) seconds;
        return juce::String (total / 60) + ":" + juce::String (total % 60).paddedLeft ('0', 2);
    }
}

AudioPlayer::AudioPlayer (juce::AudioFormatManager& f, SettingsScope s)
    : formats (f), settings (s)
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
    unload();

    if (deviceOpen)
    {
        deviceManager.removeAudioCallback (&sourcePlayer);
        sourcePlayer.setSource (nullptr);
    }

    readAheadThread.stopThread (1000);
}

void AudioPlayer::openDevice()
{
    if (deviceOpen)
        return;

    deviceManager.initialiseWithDefaultDevices (0, 2);
    sourcePlayer.setSource (&transport);
    deviceManager.addAudioCallback (&sourcePlayer);
    deviceOpen = true;
}

bool AudioPlayer::load (const juce::File& file)
{
    unload();

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr)
        return false;

    details.set ("Format", reader->getFormatName());
    details.set ("Sample rate", juce::String (reader->sampleRate, 0) + " Hz");
    details.set ("Channels", juce::String ((int) reader->numChannels));
    details.set ("Bit depth", juce::String ((int) reader->bitsPerSample) + (reader->usesFloatingPointData ? " (float)" : ""));

    if (reader->sampleRate > 0)
        details.set ("Duration", formatTime ((double) reader->lengthInSamples / reader->sampleRate));

    details.addArray (reader->metadataValues);

    openDevice();

    const auto sampleRate = reader->sampleRate;
    const auto channels = (int) reader->numChannels;
    readerSource = std::make_unique<juce::AudioFormatReaderSource> (reader.release(), true);
    transport.setSource (readerSource.get(), 32768, &readAheadThread, sampleRate, juce::jmax (1, channels));

    position.setRange (0.0, juce::jmax (0.01, transport.getLengthInSeconds()));
    position.setValue (0.0, juce::dontSendNotification);
    updateControls();
    return true;
}

void AudioPlayer::unload()
{
    stopTimer();
    transport.stop();
    transport.setSource (nullptr);
    readerSource.reset();
    details.clear();
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

    playButton.setButtonText (playing ? "Pause" : "Play");
    playButton.setEnabled (loaded);
    stopButton.setEnabled (loaded);
    position.setEnabled (loaded);

    if (! position.isMouseButtonDown())
        position.setValue (transport.getCurrentPosition(), juce::dontSendNotification);

    time.setText (loaded ? formatTime (transport.getCurrentPosition()) + " / " + formatTime (transport.getLengthInSeconds()) : juce::String(),
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
