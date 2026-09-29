#pragma once

#include "../Core/FileEntry.h"
#include "AudioPlayer.h"

/** Shows a preview of the highlighted item (image, text, audio player, or folder listing)
    and its metadata.
*/
class PreviewPanel final : public juce::Component
{
public:
    explicit PreviewPanel (SettingsScope settings);
    ~PreviewPanel() override;

    /** Shows an entry, or clears the panel for nullptr. */
    void show (const FileEntry* entry);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    enum class Content { none, image, text, audio };

    juce::AudioFormatManager formats;
    juce::Label title;
    juce::ImageComponent image;
    juce::TextEditor text, metadata;
    AudioPlayer audio;
    Content content = Content::none;

    Content loadContent (const FileEntry& entry, juce::StringPairArray& details);
    void showDetails (const juce::StringPairArray& details);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PreviewPanel)
};
