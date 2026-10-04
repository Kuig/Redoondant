#include "Removal.h"
#include "MoveToFolder.h"
#include "Paths.h"
#include "../Platform/RecycleBin.h"

std::vector<FileEntry> Removal::withoutNested (std::vector<FileEntry> entries)
{
    std::set<juce::String> paths;

    for (const auto& entry : entries)
        paths.insert (Paths::key (entry.file));

    std::set<juce::String> kept;
    std::vector<FileEntry> result;

    for (auto& entry : entries)
        if (! Paths::hasAncestorIn (entry.file, paths) && kept.insert (Paths::key (entry.file)).second)
            result.push_back (std::move (entry));

    return result;
}

int Removal::countWithoutRecycleBin (std::vector<FileEntry> entries)
{
    int count = 0;

    for (const auto& entry : withoutNested (std::move (entries)))
        if (! RecycleBin::isSupported (entry.file))
            ++count;

    return count;
}

namespace
{
    /** What an action did with one item: skipped items are left alone without being failures;
        leftBehind ones were copied but their original couldn't be removed completely.
    */
    enum class Outcome { done, failed, skipped, leftBehind };

    struct ItemResult
    {
        Outcome outcome = Outcome::failed;
        juce::String message;       // What to say after the path, for failed and leftBehind.
    };

    /** True if the file was modified since the analysis listed it: it may no longer be what the user reviewed
        (a duplicate that was edited, a folder that got new files). Entries without a recorded date can't be compared.
    */
    bool changedSinceAnalysis (const FileEntry& entry)
    {
        if (entry.modified.toMilliseconds() <= 0)
            return false;

        return entry.file.getLastModificationTime() != entry.modified
                || (! entry.isDirectory && entry.file.getSize() != entry.size);
    }

    /** Runs `act` on each item (after withoutNested) with progress and cancellation, and fills the report.
        Items that don't exist any more are counted apart: they are gone from the list but not "removed" by us.
    */
    RemovalReport forEachItem (std::vector<FileEntry> entries, const Removal::Progress& progress,
                               const std::function<ItemResult (const FileEntry&)>& act)
    {
        RemovalReport report;
        const auto items = Removal::withoutNested (std::move (entries));

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

            if (! entry.file.exists())
            {
                ++report.alreadyGone;
                report.files.add (entry.file);
                continue;
            }

            if (changedSinceAnalysis (entry))
            {
                report.failures.add (entry.file.getFullPathName() + "  (changed since the analysis: left untouched)");
                continue;
            }

            ItemResult result;

            try
            {
                result = act (entry);
            }
            catch (const std::exception& e)     { result = {}; result.message = e.what(); }
            catch (...)                         { result = {}; result.message = "unexpected error"; }

            const auto described = entry.file.getFullPathName() + (result.message.isEmpty() ? juce::String() : "  (" + result.message + ")");

            switch (result.outcome)
            {
                case Outcome::done:
                    ++report.processed;
                    report.bytes += entry.size;
                    report.files.add (entry.file);
                    break;

                case Outcome::failed:       report.failures.add (described); break;
                case Outcome::leftBehind:   report.leftBehind.add (described); break;
                case Outcome::skipped:      break;
            }
        }

        return report;
    }
}

RemovalReport Removal::toTrash (std::vector<FileEntry> entries, const Progress& progress)
{
    return forEachItem (std::move (entries), progress, [] (const FileEntry& entry)
    {
        ItemResult result;

        if (! RecycleBin::isSupported (entry.file))
        {
            result.message = "no Recycle Bin on this drive: left untouched";
            return result;
        }

        if (RecycleBin::move (entry.file))
            result.outcome = Outcome::done;

        return result;
    });
}

RemovalReport Removal::deletePermanently (std::vector<FileEntry> entries, const Progress& progress)
{
    return forEachItem (std::move (entries), progress, [] (const FileEntry& entry)
    {
        ItemResult result;
        const auto& f = entry.file;

        if (f.isDirectory() ? f.deleteRecursively() : f.deleteFile())
            result.outcome = Outcome::done;

        return result;
    });
}

RemovalReport Removal::toFolder (std::vector<FileEntry> entries, const juce::File& destination, const Progress& progress)
{
    if (! destination.isDirectory())
    {
        RemovalReport report;
        report.failures.add ("The destination folder doesn't exist: " + destination.getFullPathName());
        return report;
    }

    return forEachItem (std::move (entries), progress, [&] (const FileEntry& entry)
    {
        ItemResult result;
        const auto& file = entry.file;

        if (file.getParentDirectory() == destination)
        {
            result.outcome = Outcome::skipped;      // Already there.
            return result;
        }

        if (file == destination || destination.isAChildOf (file))
        {
            result.message = "can't be moved into itself";
            return result;
        }

        switch (MoveToFolder::moveItem (file, MoveToFolder::uniqueTarget (destination, file.getFileName())))
        {
            case MoveToFolder::Outcome::moved:
                result.outcome = Outcome::done;
                break;

            case MoveToFolder::Outcome::copiedOriginalKept:
                result.outcome = Outcome::leftBehind;
                result.message = "copied, but the original could not be removed completely";
                break;

            case MoveToFolder::Outcome::containsLinks:
                result.message = "contains links (junctions or symbolic links), which can't be copied between drives";
                break;

            case MoveToFolder::Outcome::failed:
                break;
        }

        return result;
    });
}
