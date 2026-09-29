#pragma once

#include "FileEntry.h"

/** Outcome of moving items to the Recycle Bin. */
struct TrashReport
{
    int moved = 0;
    juce::int64 bytesFreed = 0;
    juce::Array<juce::File> trashed;
    juce::StringArray failures;
};

namespace Trash
{
    /** Removes duplicates and entries contained in another entry's folder,
        so nothing is counted (or trashed) twice.
    */
    std::vector<FileEntry> withoutNested (std::vector<FileEntry> entries);

    /** Moves the entries to the Recycle Bin (after withoutNested) and reports the result. */
    TrashReport moveToTrash (std::vector<FileEntry> entries);
}
