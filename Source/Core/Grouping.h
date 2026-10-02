#pragma once

#include "FileEntry.h"
#include <map>

/** Generic helpers used by criteria to split entries into groups. */
namespace Grouping
{
    using Groups = std::vector<std::vector<FileEntry>>;

    /** Splits items by key, dropping groups smaller than minSize. */
    template <typename KeyFunction>
    Groups groupBy (std::vector<FileEntry> items, KeyFunction&& key, size_t minSize = 2)
    {
        using Key = std::decay_t<decltype (key (std::declval<const FileEntry&>()))>;
        std::map<Key, std::vector<FileEntry>> buckets;

        for (auto& item : items)
            buckets[key (item)].push_back (std::move (item));

        Groups result;

        for (auto& bucket : buckets)
            if (bucket.second.size() >= minSize)
                result.push_back (std::move (bucket.second));

        return result;
    }

    /** The different file extensions (lowercase, e.g. ".mp3") among the items. */
    inline juce::StringArray extensionsOf (const std::vector<FileEntry>& items)
    {
        juce::StringArray extensions;

        for (const auto& item : items)
            extensions.addIfNotAlreadyThere (item.file.getFileExtension().toLowerCase());

        return extensions;
    }

    /** Number of leading characters shared by two strings, ignoring case. */
    int sharedPrefixLength (const juce::String& a, const juce::String& b);

    struct RootCluster
    {
        juce::String root;              ///< The name start shared by all items.
        std::vector<FileEntry> items;
    };

    /** Clusters entries whose names (without extension) start with the same text.
        A name joins a cluster when the shared start is at least minRootLength characters
        and at least minRootRatio (0..1) of the shortest name involved.
        Makes no assumption on what the differing ends look like.
    */
    std::vector<RootCluster> clusterBySharedRoot (std::vector<FileEntry> items, int minRootLength, double minRootRatio);

    /** Sorts entries by a numeric value and splits them wherever two consecutive values differ by more than maxGap. */
    Groups clusterByGap (std::vector<FileEntry> items,
                         std::function<double (const FileEntry&)> valueOf,
                         double maxGap,
                         size_t minSize = 2);

    /** Sorts entries by time and splits them wherever two consecutive times are further apart than maxGap. */
    Groups clusterByTimeGap (std::vector<FileEntry> items,
                             std::function<juce::Time (const FileEntry&)> timeOf,
                             juce::RelativeTime maxGap,
                             size_t minSize = 2);
}
