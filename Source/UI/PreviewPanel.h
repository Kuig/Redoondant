#pragma once

#include "../Core/FileEntry.h"
#include "../Metadata/Metadata.h"
#include "AudioPlayer.h"
#include "TextPreview.h"

/** Shows a preview of the highlighted item (image, PDF page, OS thumbnail, text, folder or
    archive listing, audio player) and its metadata. Content is loaded on a background thread;
    only the latest request is displayed.
*/
class PreviewPanel final : public juce::Component
{
public:
    explicit PreviewPanel (SettingsScope settings);
    ~PreviewPanel() override;

    /** Shows the entries: one gets the full preview (with the audio player); several are shown in a grid
        with only the metadata that differs between them. No entries clears the panel.
    */
    void show (std::vector<FileEntry> entries);

    void paint (juce::Graphics&) override;
    void resized() override;

    enum class Content { none, image, text, audio };

    /** Everything needed to display an entry, prepared off the message thread. */
    struct Data
    {
        juce::String name;
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
    TextPreview text;
    juce::TextEditor metadata;
    AudioPlayer audio;
    Content content = Content::none;
    int generation = 0;                     ///< Identifies the latest request; older results are dropped.
    juce::ThreadPool loader { 1 };

    class Tile;
    std::vector<std::unique_ptr<Tile>> tiles;       ///< The previews of a multiple selection.
    bool multiple = false;

    void display (Data data);
    void displayMany (std::vector<Data> all, int notShown);
    void showDetails (const Metadata& details);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PreviewPanel)
};
