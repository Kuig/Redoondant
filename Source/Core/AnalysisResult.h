#pragma once

#include "FileEntry.h"

/** Columns of the results table (values are TableHeaderComponent column ids). */
enum class Column
{
    check = 1,
    name,
    folder,
    size,
    modified,
    created,
    type,
    contentCreated
};

/** Bulk check states of a list: the criterion's proposal, everything, nothing. They cycle in this order.
    (Not to be confused with CheckState, which is how one check box is drawn.)
*/
enum class BulkCheck { defaults, all, none };

inline BulkCheck nextBulkCheck (BulkCheck c)     { return c == BulkCheck::defaults ? BulkCheck::all : (c == BulkCheck::all ? BulkCheck::none : BulkCheck::defaults); }

/** A set of related entries (e.g. identical files). Ungrouped criteria use a single untitled group. */
struct ResultGroup
{
    juce::String title;
    std::vector<FileEntry> items;
    int order = 0;                      ///< Position in the analysis result (set by ResultsModel).
    BulkCheck phase = BulkCheck::defaults;    ///< The last bulk state applied to this group (see ResultsModel::cycleGroup).

    juce::int64 totalSize() const       { return ::totalSize (items); }

    /** Latest / earliest modification time of the items, in milliseconds (0 / max when empty). */
    juce::int64 newestModified() const
    {
        juce::int64 latest = 0;

        for (const auto& item : items)
            latest = juce::jmax (latest, item.modified.toMilliseconds());

        return latest;
    }

    juce::int64 oldestModified() const
    {
        juce::int64 earliest = std::numeric_limits<juce::int64>::max();

        for (const auto& item : items)
            earliest = juce::jmin (earliest, item.modified.toMilliseconds());

        return earliest;
    }

    /** For groups of identical files: the space taken by every copy but one. */
    juce::int64 wastedSize() const      { return items.empty() ? 0 : items.front().size * (juce::int64) (items.size() - 1); }
};

/** The output of a criterion's analysis. */
struct AnalysisResult
{
    std::vector<ResultGroup> groups;

    /** Wraps a flat list into a single untitled group. */
    static AnalysisResult flat (std::vector<FileEntry> items)
    {
        AnalysisResult result;
        result.groups.push_back ({ {}, std::move (items) });
        return result;
    }
};
