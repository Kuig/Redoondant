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

    /** Sets `selectedByDefault` of every entry. The "keep one" strategies look at each group's visible entries in their
        current order and keep the first one that wins (hidden and missing entries are never kept nor checked).
    */
    inline void apply (DefaultSelection strategy, std::vector<ResultGroup>& groups, const std::function<bool (const FileEntry&)>& isVisible)
    {
        for (auto& group : groups)
        {
            const FileEntry* keeper = nullptr;

            if (dependsOnView (strategy))
                for (const auto& item : group.items)
                {
                    if (item.missing || ! isVisible (item))
                        continue;

                    const auto length = item.name().length();
                    const auto* best = keeper;
                    const bool wins = best == nullptr
                                       || (strategy == DefaultSelection::longestName && length > best->name().length())
                                       || (strategy == DefaultSelection::shortestName && length < best->name().length());

                    if (wins)
                        keeper = &item;
                }

            for (auto& item : group.items)
            {
                switch (strategy)
                {
                    case DefaultSelection::none:    item.selectedByDefault = false; break;
                    case DefaultSelection::all:     item.selectedByDefault = ! item.missing; break;
                    case DefaultSelection::folders: item.selectedByDefault = ! item.missing && item.isDirectory; break;
                    case DefaultSelection::files:   item.selectedByDefault = ! item.missing && ! item.isDirectory; break;
                    default:                        item.selectedByDefault = ! item.missing && isVisible (item) && &item != keeper; break;
                }
            }
        }
    }

    /** apply() with everything visible, then checks exactly what is selected by default (for results used outside the table). */
    inline void applyAndCheck (DefaultSelection strategy, std::vector<ResultGroup>& groups)
    {
        apply (strategy, groups, [] (const FileEntry&) { return true; });

        for (auto& group : groups)
            for (auto& item : group.items)
                item.selected = item.selectedByDefault;
    }
}
