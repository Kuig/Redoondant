#pragma once

#include <JuceHeader.h>

/** One metadata property of a file. */
struct MetadataItem
{
    juce::String key;       ///< Canonical name shared by all sources, e.g. "System.Music.Artist".
    juce::String label;     ///< Human-readable name, e.g. "Contributing artists".
    juce::String value;     ///< Display value.
    juce::var raw;          ///< Numeric value when meaningful (e.g. duration in seconds), otherwise void.
};

/** The metadata of a file, in display order. */
class Metadata
{
public:
    std::vector<MetadataItem>& items() noexcept                 { return list; }
    const std::vector<MetadataItem>& items() const noexcept     { return list; }

    const MetadataItem* find (const juce::String& key) const
    {
        for (const auto& item : list)
            if (item.key == key)
                return &item;

        return nullptr;
    }

    juce::String text (const juce::String& key) const
    {
        const auto* item = find (key);
        return item != nullptr ? item->value : juce::String();
    }

    /** Adds an item unless its key is already present or its value is empty. */
    void add (MetadataItem item)
    {
        if (item.value.trim().isNotEmpty() && (item.key.isEmpty() || find (item.key) == nullptr))
            list.push_back (std::move (item));
    }

    void add (const juce::String& key, const juce::String& label, const juce::String& value, const juce::var& raw = {})
    {
        add (MetadataItem { key, label, value, raw });
    }

    void append (const Metadata& other)
    {
        for (const auto& item : other.list)
            add (item);
    }

private:
    std::vector<MetadataItem> list;
};

/** Canonical keys used across sources (Windows property names, also used for PDF metadata). */
namespace MetadataKeys
{
    inline const juce::String title        = "System.Title";
    inline const juce::String author       = "System.Author";
    inline const juce::String artist       = "System.Music.Artist";
    inline const juce::String albumArtist  = "System.Music.AlbumArtist";
    inline const juce::String album        = "System.Music.AlbumTitle";
    inline const juce::String track        = "System.Music.TrackNumber";
    inline const juce::String duration     = "System.Media.Duration";
    inline const juce::String dateTaken    = "System.Photo.DateTaken";
    inline const juce::String cameraMaker  = "System.Photo.CameraManufacturer";
    inline const juce::String cameraModel  = "System.Photo.CameraModel";
    inline const juce::String pageCount    = "System.Document.PageCount";
}
