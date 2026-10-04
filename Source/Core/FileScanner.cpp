#include "FileScanner.h"
#include <filesystem>

namespace
{
    /** System files that should never be proposed for deletion. */
    bool isIgnored (const juce::File& file)
    {
        return file.getFileName().equalsIgnoreCase ("desktop.ini");
    }

    struct Totals
    {
        juce::int64 size = 0;
        int fileCount = 0;
        bool contentUnknown = false;    // Something in there couldn't be listed (or is a link that isn't followed).
    };

    class Walker
    {
    public:
        Walker (const ScanContext& c, const FileScanner::Options& o, bool rec)
            : context (c), options (o), recursive (rec) {}

        std::vector<FileEntry> run (const juce::File& root)
        {
            walk (root, true);
            return std::move (results);
        }

        /** The size, file count and completeness of a folder's whole content, without listing it. */
        Totals measure (const juce::File& folder)
        {
            return walk (folder, false);
        }

    private:
        const ScanContext& context;
        const FileScanner::Options& options;
        const bool recursive;
        std::vector<FileEntry> results;
        int visited = 0;

        Totals walk (const juce::File& folder, bool emitChildren)
        {
            Totals totals;
            bool anyItem = false;

            for (const auto& item : juce::RangedDirectoryIterator (folder, false, "*",
                                                                    juce::File::findFilesAndDirectories,
                                                                    juce::File::FollowSymlinks::no))
            {
                if (context.isCancelled())
                    break;

                if (++visited % 500 == 0)
                    context.progress (-1.0, "Scanning... " + juce::String (visited) + " items");

                const auto file = item.getFile();
                anyItem = true;

                if (isIgnored (file))
                    continue;

                FileEntry entry;
                entry.file = file;
                entry.isDirectory = item.isDirectory();
                entry.size = entry.isDirectory ? 0 : item.getFileSize();
                entry.fileCount = entry.isDirectory ? 0 : 1;
                entry.modified = item.getModificationTime();
                entry.created = item.getCreationTime();

                const bool isLink = entry.isDirectory && file.isSymbolicLink();     // One file-system query per folder.

                if (isLink)
                    entry.contentUnknown = true;        // Not followed: what it leads to isn't counted.

                if (entry.isDirectory && ! isLink)
                {
                    const bool emitGrandChildren = emitChildren && recursive
                                                   && (options.shouldDescend == nullptr || options.shouldDescend (entry));

                    if (emitGrandChildren || options.directorySizes)
                    {
                        const auto sub = walk (file, emitGrandChildren);
                        entry.size = sub.size;
                        entry.fileCount = sub.fileCount;
                        entry.contentUnknown = sub.contentUnknown;
                    }
                }

                totals.size += entry.size;
                totals.fileCount += entry.fileCount;
                totals.contentUnknown = totals.contentUnknown || entry.contentUnknown;

                if (emitChildren)
                    results.push_back (std::move (entry));
            }

            // The iterator reports nothing for a folder it may not read: tell "empty" from "unreadable".
            if (! anyItem && ! context.isCancelled())
            {
                std::error_code error;
                std::filesystem::directory_iterator probe (std::filesystem::path (folder.getFullPathName().toWideCharPointer()), error);
                totals.contentUnknown = totals.contentUnknown || (bool) error;
            }

            return totals;
        }
    };

}

std::vector<FileEntry> FileScanner::scan (const ScanContext& context, const Options& options)
{
    return Walker (context, options, context.recursive).run (context.root);
}

std::vector<FileEntry> FileScanner::scanTree (const juce::File& folder, const ScanContext& context, const Options& options)
{
    return Walker (context, options, true).run (folder);
}

std::vector<FileEntry> FileScanner::filesOnly (std::vector<FileEntry> entries)
{
    return keepIf (std::move (entries), [] (const FileEntry& e) { return ! e.isDirectory; });
}

std::vector<FileEntry> FileScanner::scanFiles (const ScanContext& context)
{
    return filesOnly (scan (context, Options { false }));
}

std::vector<FileEntry> FileScanner::scanFilesTree (const juce::File& folder, const ScanContext& context)
{
    return filesOnly (scanTree (folder, context, Options { false }));
}

FileEntry FileScanner::makeEntry (const juce::File& file)
{
    FileEntry entry;
    entry.file = file;
    entry.isDirectory = file.isDirectory();
    entry.modified = file.getLastModificationTime();
    entry.created = file.getCreationTime();

    if (entry.isDirectory)
    {
        const ScanContext context;
        const auto totals = Walker (context, {}, true).measure (file);
        entry.size = totals.size;
        entry.fileCount = totals.fileCount;
        entry.contentUnknown = totals.contentUnknown || file.isSymbolicLink();
    }
    else
    {
        entry.size = file.getSize();
        entry.fileCount = 1;
    }

    return entry;
}
