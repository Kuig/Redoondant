#pragma once

#include "FileEntry.h"

/** A set of related entries (e.g. identical files). Ungrouped criteria use a single untitled group. */
struct ResultGroup
{
    juce::String title;
    std::vector<FileEntry> items;
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
