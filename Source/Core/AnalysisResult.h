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

/** A set of related entries (e.g. identical files). Ungrouped criteria use a single untitled group. */
struct ResultGroup
{
    juce::String title;
    std::vector<FileEntry> items;

    juce::int64 totalSize() const       { return ::totalSize (items); }

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

    /** flat() of the items sorted by `order` (a strict weak ordering of entries). */
    template <typename Order>
    static AnalysisResult flatSorted (std::vector<FileEntry> items, Order&& order)
    {
        std::sort (items.begin(), items.end(), std::forward<Order> (order));
        return flat (std::move (items));
    }
};
