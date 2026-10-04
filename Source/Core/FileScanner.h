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

    /** Builds an entry by querying the file system (folder sizes are computed recursively). */
    FileEntry makeEntry (const juce::File& file);

    /** Keeps the entries that satisfy `accept`. */
    template <typename Predicate>
    std::vector<FileEntry> keepIf (std::vector<FileEntry> entries, Predicate&& accept)
    {
        entries.erase (std::remove_if (entries.begin(), entries.end(), [&] (const FileEntry& e) { return ! accept (e); }), entries.end());
        return entries;
    }

    /** Keeps only the files of a list of entries. */
    std::vector<FileEntry> filesOnly (std::vector<FileEntry> entries);

    /** The files of context.root (and of its sub-folders if context.recursive), without computing folder sizes: the cheap scan. */
    std::vector<FileEntry> scanFiles (const ScanContext& context);

    /** The files of a folder and all its sub-folders, without computing folder sizes. */
    std::vector<FileEntry> scanFilesTree (const juce::File& folder, const ScanContext& context);

    /** scan() followed by keepIf(). */
    template <typename Predicate>
    std::vector<FileEntry> scanAndFilter (const ScanContext& context, const Options& options, Predicate&& accept)
    {
        return keepIf (scan (context, options), std::forward<Predicate> (accept));
    }
}

/** Orderings for std::sort of entries. */
namespace Orderings
{
    inline bool largestFirst (const FileEntry& a, const FileEntry& b)   { return a.size > b.size; }
    inline bool byName (const FileEntry& a, const FileEntry& b)         { return a.name().compareNatural (b.name()) < 0; }
}
