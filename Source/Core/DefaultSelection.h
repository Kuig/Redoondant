#pragma once

#include "AnalysisResult.h"

/** Which entries are checked by default (the first state of the check cycle). The user picks one;
    criteria only suggest the initial one (and the sort that goes with it).
*/
enum class DefaultSelection
{
    none,           ///< Nothing is checked.
    all,            ///< Everything is checked.
    followsSort,    ///< In each group the first entry (in the current sort order) is the only one not checked.
    longestName,    ///< In each group the entry with the longest name is the only one not checked.
    shortestName,   ///< In each group the entry with the shortest name is the only one not checked.
    folders,        ///< Only folders are checked.
    files           ///< Only files are checked.
};

namespace DefaultSelections
{
    inline const juce::StringArray& names()
    {
        static const juce::StringArray list { "Default: none checked", "Default: all checked", "Default: keep first (follows sort)",
                                              "Default: keep longest name", "Default: keep shortest name",
                                              "Default: folders only", "Default: files only" };
        return list;
    }

    inline juce::String nameOf (DefaultSelection s)     { return names()[(int) s]; }

    /** Strategies that keep one entry per group need groups; folders / files need a list that shows folders. */
    inline std::vector<DefaultSelection> available (bool grouped, bool showsFolders)
    {
        std::vector<DefaultSelection> list { DefaultSelection::none, DefaultSelection::all };

        if (grouped)
            list.insert (list.end(), { DefaultSelection::followsSort, DefaultSelection::longestName, DefaultSelection::shortestName });

        if (showsFolders)
            list.insert (list.end(), { DefaultSelection::folders, DefaultSelection::files });

        return list;
    }

    /** Whether the outcome depends on the order or on which entries are visible, so it must be redone when those change. */
    inline bool dependsOnView (DefaultSelection s)
    {
        return s == DefaultSelection::followsSort || s == DefaultSelection::longestName || s == DefaultSelection::shortestName;
    }

    /** The item a "keep one" strategy leaves unchecked in a group: the first candidate (in the group's current order)
        that wins. Null for the other strategies, or when there is no candidate.
    */
    template <typename Items, typename IsCandidate>
    const typename Items::value_type* keeperOf (DefaultSelection strategy, const Items& items, IsCandidate&& isCandidate)
    {
        const typename Items::value_type* keeper = nullptr;

        if (! dependsOnView (strategy))
            return keeper;

        for (const auto& item : items)
        {
            if (! isCandidate (item))
                continue;

            const auto length = item.name().length();
            const bool wins = keeper == nullptr
                               || (strategy == DefaultSelection::longestName && length > keeper->name().length())
                               || (strategy == DefaultSelection::shortestName && length < keeper->name().length());

            if (wins)
                keeper = &item;
        }

        return keeper;
    }

    /** Whether the strategy checks an item by default. Only the "keep one" strategies care whether it is visible. */
    inline bool checkedBy (DefaultSelection strategy, const FileEntry& item, bool exists, bool visible, bool isKeeper)
    {
        switch (strategy)
        {
            case DefaultSelection::none:    return false;
            case DefaultSelection::all:     return exists;
            case DefaultSelection::folders: return exists && item.isDirectory;
            case DefaultSelection::files:   return exists && ! item.isDirectory;
            default:                        return exists && visible && ! isKeeper;
        }
    }

    /** The entries a strategy checks in fresh analysis results (everything visible, nothing missing). */
    inline std::vector<FileEntry> checkedEntries (DefaultSelection strategy, const std::vector<ResultGroup>& groups)
    {
        std::vector<FileEntry> checked;

        for (const auto& group : groups)
        {
            const auto* keeper = keeperOf (strategy, group.items, [] (const FileEntry&) { return true; });

            for (const auto& item : group.items)
                if (checkedBy (strategy, item, true, true, &item == keeper))
                    checked.push_back (item);
        }

        return checked;
    }
}
