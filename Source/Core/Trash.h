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
    bool cancelled = false;                 ///< The user stopped the operation: only part of the items were processed.
};

namespace Trash
{
    /** Called before each item with the fraction of bytes done (0..1) and the item's name;
        return false to stop (the report then lists what was done so far, with `cancelled` set).
    */
    using Progress = std::function<bool (double fraction, const juce::String& currentItem)>;

    /** Removes duplicates and entries contained in another entry's folder,
        so nothing is counted (or moved) twice.
    */
    std::vector<FileEntry> withoutNested (std::vector<FileEntry> entries);

    /** Moves the entries to the Recycle Bin (after withoutNested) and reports the result. */
    RemovalReport moveToTrash (std::vector<FileEntry> entries, const Progress& progress = {});

    /** Deletes the entries for good, bypassing the Recycle Bin (after withoutNested). */
    RemovalReport deletePermanently (std::vector<FileEntry> entries, const Progress& progress = {});
}
