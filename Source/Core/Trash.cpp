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

TrashReport Trash::moveToTrash (std::vector<FileEntry> entries)
{
    TrashReport report;

    for (const auto& entry : withoutNested (std::move (entries)))
    {
        if (entry.file.moveToTrash())
        {
            ++report.moved;
            report.bytesFreed += entry.size;
            report.trashed.add (entry.file);
        }
        else
        {
            report.failures.add (entry.file.getFullPathName());
        }
    }

    return report;
}
