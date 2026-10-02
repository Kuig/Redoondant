#pragma once

#include "FileEntry.h"

/** Bulk check states: the criterion's proposal, everything, nothing. They cycle in this order. */
enum class Checks { defaults, all, none };

inline Checks nextChecks (Checks c)     { return c == Checks::defaults ? Checks::all : (c == Checks::all ? Checks::none : Checks::defaults); }

/** A set of related entries (e.g. identical files). Ungrouped criteria use a single untitled group. */
struct ResultGroup
{
    juce::String title;
    std::vector<FileEntry> items;
    Checks phase = Checks::defaults;    ///< The last bulk state applied to this group (see ResultsModel::cycleGroup).
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
