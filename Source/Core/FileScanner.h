#pragma once

#include "FileEntry.h"
#include "ScanContext.h"

/** Every file and folder under a folder (with folder sizes), as scanned once to be shared (ScanContext::tree). */
struct ScannedTree
{
    juce::File root;
    std::vector<FileEntry> entries;
};

/** Walks a folder tree, producing FileEntry objects with aggregated folder sizes.
    When the context carries a ScannedTree that covers the folder, the scans are taken from it (same entries, same
    order) instead of the disk.
*/
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

    /** Scans the whole tree under context.root (whatever context.recursive says), to hand to several analyses. */
    std::shared_ptr<const ScannedTree> scanWholeTree (const ScanContext& context);

    /** Builds an entry by querying the file system (folder sizes are computed recursively). */
    FileEntry makeEntry (const juce::File& file);

    /** Keeps the entries that satisfy `accept`. */
    template <typename Predicate>
    std::vector<FileEntry> keepIf (std::vector<FileEntry> entries, Predicate&& accept)
    {
        entries.erase (std::remove_if (entries.begin(), entries.end(), [&] (const FileEntry& e) { return ! accept (e); }), entries.end());
        return entries;
    }

    /** The names of the folders where version control keeps its data (Git, Subversion, Mercurial, Bazaar, Darcs, CVS,
        Jujutsu, Pijul). */
    inline const juce::StringArray& versionControlFolderNames()
    {
        static const juce::StringArray names { ".git", ".svn", ".hg", ".bzr", "_darcs", "CVS", ".jj", ".pijul" };
        return names;
    }

    /** A folder where version control keeps its data: its content belongs to the repository, so criteria can leave it
        out (removing it would break the repository).
    */
    inline bool isVersionControlFolder (const FileEntry& e)
    {
        return e.isDirectory && versionControlFolderNames().contains (e.name(), true);
    }

    /** The names of the folders where package managers install the dependencies of a project (npm, Bower, pip), and of
        Python virtual environments (only with their pyvenv.cfg). */
    inline const juce::StringArray& dependencyFolderNames()
    {
        static const juce::StringArray names { "node_modules", "bower_components", "site-packages", ".venv", "venv" };
        return names;
    }

    /** A folder of installed dependencies: they hold many identical files (licences, shared libraries) that are not
        redundant, since each package needs its own; the whole folder can be removed instead (Junk folders).
    */
    inline bool isDependencyFolder (const FileEntry& e)
    {
        if (! e.isDirectory || ! dependencyFolderNames().contains (e.name(), true))
            return false;

        return ! e.name().endsWithIgnoreCase ("venv") || e.file.getChildFile ("pyvenv.cfg").existsAsFile();
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
