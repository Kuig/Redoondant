#pragma once

#include "../Core/AnalysisResult.h"
#include "../Core/DefaultSelection.h"

/** Bulk check states of a list: the criterion's proposal, everything, nothing. They cycle in this order.
    (Not to be confused with CheckState, which is how one check box is drawn.)
*/
enum class BulkCheck { defaults, all, none };

inline BulkCheck nextBulkCheck (BulkCheck c)     { return c == BulkCheck::defaults ? BulkCheck::all : (c == BulkCheck::all ? BulkCheck::none : BulkCheck::defaults); }

/** An entry as the list shows it: what the criterion found, plus its state in the list.
    A ResultItem is-a FileEntry, so it can be handed to anything that only needs the entry (a slice).
*/
struct ResultItem : FileEntry
{
    ResultItem() = default;
    explicit ResultItem (FileEntry found) : FileEntry (std::move (found)) {}

    bool selected = false;              ///< Checked for removal.
    bool selectedByDefault = false;     ///< The check state proposed by the default selection (or by whoever filled the list).
    bool missing = false;               ///< Listed but not on disk (can't be checked).
    bool visible = true;                ///< Passes the list's filter (kept up to date by ResultsModel, so the filter runs once per change).
};

/** A group of the list: its items with their state, and the group's own bookkeeping. */
struct ListedGroup
{
    juce::String title;
    std::vector<ResultItem> items;
    int order = 0;                      ///< Position in the analysis result.
    BulkCheck phase = BulkCheck::defaults;      ///< The last bulk state applied to this group.

    juce::int64 totalSize() const       { return ::totalSize (items); }
    juce::int64 newestModified() const  { return ::newestModified (items); }
    juce::int64 oldestModified() const  { return ::oldestModified (items); }
};

/** What a ResultsModel holds: the analysis result turned into groups of items with state. */
struct ListedResult
{
    std::vector<ListedGroup> groups;
};

namespace DefaultSelections
{
    /** Sets `selectedByDefault` of every item. The "keep one" strategies look at each group's visible items in their
        current order and keep the first one that wins (hidden and missing items are never kept nor checked).
    */
    inline void apply (DefaultSelection strategy, std::vector<ListedGroup>& groups)
    {
        for (auto& group : groups)
        {
            const auto* keeper = keeperOf (strategy, group.items, [] (const ResultItem& item) { return ! item.missing && item.visible; });

            for (auto& item : group.items)
                item.selectedByDefault = checkedBy (strategy, item, ! item.missing, item.visible, &item == keeper);
        }
    }
}
