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
    RemovalReport removeEach (std::vector<FileEntry> entries, const std::function<bool (const juce::File&)>& remove,
                              const Trash::Progress& progress)
    {
        RemovalReport report;
        const auto items = Trash::withoutNested (std::move (entries));

        juce::int64 totalBytes = 0, doneBytes = 0;

        for (const auto& entry : items)
            totalBytes += entry.size;

        for (size_t i = 0; i < items.size(); ++i)
        {
            const auto& entry = items[i];
            const double fraction = totalBytes > 0 ? (double) doneBytes / (double) totalBytes : (double) i / (double) items.size();

            if (progress != nullptr && ! progress (fraction, entry.name()))
            {
                report.cancelled = true;
                break;
            }

            doneBytes += entry.size;

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

RemovalReport Trash::moveToTrash (std::vector<FileEntry> entries, const Progress& progress)
{
    return removeEach (std::move (entries), [] (const juce::File& f) { return f.moveToTrash(); }, progress);
}

RemovalReport Trash::deletePermanently (std::vector<FileEntry> entries, const Progress& progress)
{
    return removeEach (std::move (entries), [] (const juce::File& f) { return f.isDirectory() ? f.deleteRecursively() : f.deleteFile(); }, progress);
}
