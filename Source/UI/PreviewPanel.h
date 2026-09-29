#pragma once

#include "../Core/FileEntry.h"
#include "../Metadata/Metadata.h"
#include "AudioPlayer.h"

/** Shows a preview of the highlighted item (image, PDF page, OS thumbnail, text, folder or
    archive listing, audio player) and its metadata. Content is loaded on a background thread;
    only the latest request is displayed.
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

    enum class Content { none, image, text, audio };

    /** Everything needed to display an entry, prepared off the message thread. */
    struct Data
    {
        Content content = Content::none;
        juce::Image image;
        juce::String text;
        Metadata details;
        std::shared_ptr<juce::AudioFormatReader> audio;     ///< Opened off the message thread (can be slow).
    };

private:
    juce::AudioFormatManager formats;
    juce::Label title;
    juce::ImageComponent image;
    juce::TextEditor text, metadata;
    AudioPlayer audio;
    Content content = Content::none;
    int generation = 0;                     ///< Identifies the latest request; older results are dropped.
    juce::ThreadPool loader { 1 };

    void display (Data data);
    void showDetails (const Metadata& details);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PreviewPanel)
};
