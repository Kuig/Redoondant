#pragma once

#include "FileEntry.h"

/** Outcome of an action that takes items out of the analysed folder
    (moving them to the Recycle Bin or to another folder, or deleting them).
*/
struct RemovalReport
{
    int processed = 0;                      ///< Items that were removed or moved by this action.
    juce::int64 bytes = 0;                  ///< Their total size.
    juce::Array<juce::File> files;          ///< The original locations of what is gone from the list (processed and already missing).
    juce::StringArray failures;             ///< Descriptions of what could not be removed or moved.
    juce::StringArray leftBehind;           ///< Folders/files copied to the destination whose original could not be removed completely.
    int alreadyGone = 0;                    ///< Items that no longer existed when their turn came (not counted in processed/bytes).
    bool cancelled = false;                 ///< The user stopped the operation: only part of the items were processed.
};

namespace Removal
{
    /** Called before each item with the fraction of bytes done (0..1) and the item's name;
        return false to stop (the report then lists what was done so far, with `cancelled` set).
    */
    using Progress = std::function<bool (double fraction, const juce::String& currentItem)>;

    /** Removes duplicates and entries contained in another entry's folder,
        so nothing is counted (or moved) twice.
    */
    std::vector<FileEntry> withoutNested (std::vector<FileEntry> entries);

    /** How many of the entries (after withoutNested) are on a volume without a usable Recycle Bin
        (network shares, removable drives, drives with the Recycle Bin turned off).
    */
    int countWithoutRecycleBin (std::vector<FileEntry> entries);

    /** Moves the entries to the Recycle Bin (after withoutNested) and reports the result.
        Items that Windows would delete for good instead of recycling are left untouched and listed as failures.
    */
    RemovalReport toTrash (std::vector<FileEntry> entries, const Progress& progress = {});

    /** Deletes the entries for good, bypassing the Recycle Bin (after withoutNested). */
    RemovalReport deletePermanently (std::vector<FileEntry> entries, const Progress& progress = {});

    /** Moves the entries into the destination folder (after withoutNested).
        Refuses to move a folder into itself, and skips items already in the destination.
        See MoveToFolder.h for the helpers it is built on.
    */
    RemovalReport toFolder (std::vector<FileEntry> entries, const juce::File& destination, const Progress& progress = {});
}
