#pragma once

#include "FileEntry.h"
#include "ScanContext.h"

/** Walks a folder tree, producing FileEntry objects with aggregated folder sizes. */
namespace FileScanner
{
    struct Options
    {
        /** Whether folder sizes/file counts are needed. If false and the scan isn't recursive,
            sub-folders aren't visited at all (much faster).
        */
        bool directorySizes = true;

        /** Optional: return false to keep a folder's content out of the results
            (its size is still computed). Only relevant for recursive scans.
        */
        std::function<bool (const FileEntry&)> shouldDescend;
    };

    /** Lists the content of context.root (and, if context.recursive, of its sub-folders).
        Files and folders are both returned, in no particular order.
    */
    std::vector<FileEntry> scan (const ScanContext& context, const Options& options = {});

    /** Lists the content of a folder recursively, regardless of the context's recursive flag. */
    std::vector<FileEntry> scanTree (const juce::File& folder, const ScanContext& context, const Options& options = {});

    /** Keeps only files (or only folders) from a list of entries. */
    std::vector<FileEntry> filesOnly (std::vector<FileEntry> entries);
    std::vector<FileEntry> foldersOnly (std::vector<FileEntry> entries);
}
