#pragma once

#include "FileEntry.h"
#include "ScanContext.h"
#include <map>

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
    juce::StringArray partlyDeleted;        ///< Folders deleted only in part (a file in use, or Cancel half-way).
    std::vector<FileEntry> shrunk;          ///< Those folders as they are now, measured again.
    int alreadyGone = 0;                    ///< Items that no longer existed when their turn came (not counted in processed/bytes).
    bool cancelled = false;                 ///< The user stopped the operation: only part of the items were processed.
};

namespace Removal
{
    /** For lists whose groups hold copies of the same thing (identical files, an archive and its extracted folder...):
        the items of each group that the action leaves in place. An item of such a group is only acted on while one of
        them still exists unchanged, so the last copy of something is never removed because another page (or Explorer)
        took the one the user meant to keep.
    */
    class Keepers
    {
    public:
        /** `acted` are the checked items of a group, `kept` the other items of that group. */
        void add (const std::vector<FileEntry>& acted, std::vector<FileEntry> kept);

        /** The items kept in the group of `file`, or null if it has none (no guard). */
        const std::vector<FileEntry>* keptWith (const juce::File& file) const;

        bool isEmpty() const noexcept       { return groups.empty(); }

    private:
        std::vector<std::vector<FileEntry>> groups;
        std::map<juce::String, size_t> groupOf;     // Paths::key -> index in groups.
    };

    /** Removes duplicates and entries contained in another entry's folder,
        so nothing is counted (or moved) twice.
    */
    std::vector<FileEntry> withoutNested (std::vector<FileEntry> entries);

    /** How many of the entries (after withoutNested) are on a volume without a usable Recycle Bin
        (network shares, removable drives, drives with the Recycle Bin turned off).
    */
    int countWithoutRecycleBin (std::vector<FileEntry> entries);

    /** The actions below work on the entries after withoutNested, report each item to context.progress() (with the
        fraction of bytes done) and stop when context.isCancelled() (between items, and during long copies and deletions).
        Items modified since the analysis, and items whose group has lost its kept copies, are left untouched.
    */

    /** Moves the entries to the Recycle Bin and reports the result.
        Items that Windows would delete for good instead of recycling are left untouched and listed as failures.
    */
    RemovalReport toTrash (std::vector<FileEntry> entries, const ScanContext& context = {}, const Keepers& keepers = {});

    /** Deletes the entries for good, bypassing the Recycle Bin. */
    RemovalReport deletePermanently (std::vector<FileEntry> entries, const ScanContext& context = {}, const Keepers& keepers = {});

    /** Moves the entries into the destination folder. Refuses to move a folder into itself, and skips items already in
        the destination. See MoveToFolder.h for the helpers it is built on.
    */
    RemovalReport toFolder (std::vector<FileEntry> entries, const juce::File& destination, const ScanContext& context = {},
                            const Keepers& keepers = {});
}
