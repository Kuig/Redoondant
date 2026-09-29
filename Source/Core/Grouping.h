#pragma once

#include "FileEntry.h"
#include <map>

/** Generic helpers used by criteria to split entries into groups and pick default selections. */
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

    /** Selects every item except the one that compares lowest ("best") according to isBetter. */
    template <typename Comparator>
    void selectAllButBest (std::vector<FileEntry>& items, Comparator&& isBetter)
    {
        const auto best = std::min_element (items.begin(), items.end(), isBetter);

        for (auto it = items.begin(); it != items.end(); ++it)
            it->selected = (it != best);
    }

    /** Selects or deselects all items. */
    inline void selectAll (std::vector<FileEntry>& items, bool shouldBeSelected)
    {
        for (auto& item : items)
            item.selected = shouldBeSelected;
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

    /** Sorts entries by time and splits them wherever two consecutive times are further apart than maxGap. */
    Groups clusterByTimeGap (std::vector<FileEntry> items,
                             std::function<juce::Time (const FileEntry&)> timeOf,
                             juce::RelativeTime maxGap,
                             size_t minSize = 2);
}
