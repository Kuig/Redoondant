#include "Trash.h"
#include <set>

std::vector<FileEntry> Trash::withoutNested (std::vector<FileEntry> entries)
{
    std::set<juce::String> paths;

    for (const auto& entry : entries)
        paths.insert (entry.file.getFullPathName());

    const auto hasSelectedAncestor = [&] (const juce::File& file)
    {
        for (auto parent = file.getParentDirectory(); parent != parent.getParentDirectory(); parent = parent.getParentDirectory())
            if (paths.count (parent.getFullPathName()) > 0)
                return true;

        return false;
    };

    std::set<juce::String> kept;
    std::vector<FileEntry> result;

    for (auto& entry : entries)
        if (! hasSelectedAncestor (entry.file) && kept.insert (entry.file.getFullPathName()).second)
            result.push_back (std::move (entry));

    return result;
}

namespace
{
    RemovalReport removeEach (std::vector<FileEntry> entries, const std::function<bool (const juce::File&)>& remove)
    {
        RemovalReport report;

        for (const auto& entry : Trash::withoutNested (std::move (entries)))
        {
            if (remove (entry.file))
            {
                ++report.moved;
                report.bytesMoved += entry.size;
                report.movedFiles.add (entry.file);
            }
            else
            {
                report.failures.add (entry.file.getFullPathName());
            }
        }

        return report;
    }
}

RemovalReport Trash::moveToTrash (std::vector<FileEntry> entries)
{
    return removeEach (std::move (entries), [] (const juce::File& f) { return f.moveToTrash(); });
}

RemovalReport Trash::deletePermanently (std::vector<FileEntry> entries)
{
    return removeEach (std::move (entries), [] (const juce::File& f) { return f.isDirectory() ? f.deleteRecursively() : f.deleteFile(); });
}
