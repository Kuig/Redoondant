#include "FileScanner.h"

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

    private:
        const ScanContext& context;
        const FileScanner::Options& options;
        const bool recursive;
        std::vector<FileEntry> results;
        int visited = 0;

        Totals walk (const juce::File& folder, bool emitChildren)
        {
            Totals totals;

            for (const auto& item : juce::RangedDirectoryIterator (folder, false, "*",
                                                                    juce::File::findFilesAndDirectories,
                                                                    juce::File::FollowSymlinks::no))
            {
                if (context.isCancelled())
                    break;

                if (++visited % 500 == 0)
                    context.progress (-1.0, "Scanning... " + juce::String (visited) + " items");

                const auto file = item.getFile();

                if (isIgnored (file))
                    continue;

                FileEntry entry;
                entry.file = file;
                entry.isDirectory = item.isDirectory();
                entry.size = entry.isDirectory ? 0 : item.getFileSize();
                entry.fileCount = entry.isDirectory ? 0 : 1;
                entry.modified = item.getModificationTime();
                entry.created = item.getCreationTime();

                if (entry.isDirectory && ! file.isSymbolicLink())
                {
                    const bool emitGrandChildren = emitChildren && recursive
                                                   && (options.shouldDescend == nullptr || options.shouldDescend (entry));

                    if (emitGrandChildren || options.directorySizes)
                    {
                        const auto sub = walk (file, emitGrandChildren);
                        entry.size = sub.size;
                        entry.fileCount = sub.fileCount;
                    }
                }

                totals.size += entry.size;
                totals.fileCount += entry.fileCount;

                if (emitChildren)
                    results.push_back (std::move (entry));
            }

            return totals;
        }
    };

    std::vector<FileEntry> keepIf (std::vector<FileEntry> entries, bool wantDirectories)
    {
        entries.erase (std::remove_if (entries.begin(), entries.end(),
                                       [=] (const FileEntry& e) { return e.isDirectory != wantDirectories; }),
                       entries.end());
        return entries;
    }
}

std::vector<FileEntry> FileScanner::scan (const ScanContext& context, const Options& options)
{
    return Walker (context, options, context.recursive).run (context.root);
}

std::vector<FileEntry> FileScanner::scanTree (const juce::File& folder, const ScanContext& context, const Options& options)
{
    return Walker (context, options, true).run (folder);
}

std::vector<FileEntry> FileScanner::filesOnly (std::vector<FileEntry> entries)    { return keepIf (std::move (entries), false); }
std::vector<FileEntry> FileScanner::foldersOnly (std::vector<FileEntry> entries)  { return keepIf (std::move (entries), true); }

FileEntry FileEntry::fromFile (const juce::File& file)
{
    FileEntry entry;
    entry.file = file;
    entry.isDirectory = file.isDirectory();
    entry.modified = file.getLastModificationTime();
    entry.created = file.getCreationTime();

    if (entry.isDirectory)
    {
        for (const auto& child : FileScanner::filesOnly (FileScanner::scanTree (file, {})))
        {
            entry.size += child.size;
            ++entry.fileCount;
        }
    }
    else
    {
        entry.size = file.getSize();
        entry.fileCount = 1;
    }

    return entry;
}
