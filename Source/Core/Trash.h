#pragma once

#include "FileEntry.h"

/** Outcome of an action that takes items out of the analysed folder
    (moving them to the Recycle Bin or to another folder).
*/
struct RemovalReport
{
    int moved = 0;
    juce::int64 bytesMoved = 0;
    juce::Array<juce::File> movedFiles;     ///< The original locations of what was moved.
    juce::StringArray failures;             ///< Descriptions of what could not be moved.
};

namespace Trash
{
    /** Removes duplicates and entries contained in another entry's folder,
        so nothing is counted (or moved) twice.
    */
    std::vector<FileEntry> withoutNested (std::vector<FileEntry> entries);

    /** Moves the entries to the Recycle Bin (after withoutNested) and reports the result. */
    RemovalReport moveToTrash (std::vector<FileEntry> entries);

    /** Deletes the entries for good, bypassing the Recycle Bin (after withoutNested). */
    RemovalReport deletePermanently (std::vector<FileEntry> entries);
}
