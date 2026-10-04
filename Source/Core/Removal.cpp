#include "Removal.h"
#include "Format.h"
#include "FileScanner.h"
#include "MoveToFolder.h"
#include "Paths.h"
#include "../Platform/FileSystem.h"
#include "../Platform/RecycleBin.h"

void Removal::Keepers::add (const std::vector<FileEntry>& acted, std::vector<FileEntry> kept)
{
    if (acted.empty() || kept.empty())
        return;

    for (const auto& entry : acted)
        groupOf[Paths::key (entry.file)] = groups.size();

    groups.push_back (std::move (kept));
}

const std::vector<FileEntry>* Removal::Keepers::keptWith (const juce::File& file) const
{
    const auto found = groupOf.find (Paths::key (file));
    return found != groupOf.end() ? &groups[found->second] : nullptr;
}

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
        leftBehind ones were copied but their original couldn't be removed completely; partial ones were deleted in part.
    */
    enum class Outcome { done, failed, skipped, leftBehind, partial };

    struct ItemResult
    {
        Outcome outcome = Outcome::failed;
        juce::String message;       // What to say after the path, for failed, leftBehind and partial.
    };

    /** True if the item was modified since the analysis listed it: it may no longer be what the user reviewed
        (a duplicate that was edited, a folder that got new files, even deep inside). Entries without a recorded date can't be compared.
    */
    bool changedSinceAnalysis (const FileEntry& entry)
    {
        if (entry.modified.toMilliseconds() <= 0)
            return false;

        if (entry.file.getLastModificationTime() != entry.modified)
            return true;

        if (! entry.isDirectory)
            return entry.file.getSize() != entry.size;

        // A folder's date only changes with its direct children: measure its whole content again.
        const auto now = FileScanner::makeEntry (entry.file);
        return now.size != entry.size || now.fileCount != entry.fileCount;
    }

    /** True if one of the items kept in the entry's group is still there as analysed (or the group has no guard). */
    bool keptCopyRemains (const FileEntry& entry, const Removal::Keepers& keepers)
    {
        const auto* kept = keepers.keptWith (entry.file);

        return kept == nullptr
            || std::any_of (kept->begin(), kept->end(), [] (const FileEntry& k) { return k.file.exists() && ! changedSinceAnalysis (k); });
    }

    /** Runs `act` on each item (after withoutNested) with progress and cancellation, and fills the report.
        Items that don't exist any more are counted apart: they are gone from the list but not "removed" by us.
    */
    RemovalReport forEachItem (std::vector<FileEntry> entries, const ScanContext& context, const Removal::Keepers& keepers,
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
            context.progress (totalBytes > 0 ? (double) doneBytes / (double) totalBytes : (double) i / (double) items.size(), entry.name());

            if (context.isCancelled())
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

            if (! keptCopyRemains (entry, keepers))
            {
                report.failures.add (entry.file.getFullPathName() + "  (the item kept in its group is gone or changed since the analysis: "
                                                                    "left untouched, so a copy remains)");
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

                case Outcome::partial:
                {
                    auto now = FileScanner::makeEntry (entry.file);
                    report.bytes += juce::jmax ((juce::int64) 0, entry.size - now.size);
                    report.partlyDeleted.add (described);
                    report.shrunk.push_back (std::move (now));
                    break;
                }

                case Outcome::failed:       report.failures.add (described); break;
                case Outcome::leftBehind:   report.leftBehind.add (described); break;
                case Outcome::skipped:      break;
            }
        }

        report.cancelled = report.cancelled || context.isCancelled();
        return report;
    }

    FileSystem::ShouldStop stopperOf (const ScanContext& context)
    {
        return [&context] { return context.isCancelled(); };
    }
}

RemovalReport Removal::toTrash (std::vector<FileEntry> entries, const ScanContext& context, const Keepers& keepers)
{
    return forEachItem (std::move (entries), context, keepers, [] (const FileEntry& entry)
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

RemovalReport Removal::deletePermanently (std::vector<FileEntry> entries, const ScanContext& context, const Keepers& keepers)
{
    return forEachItem (std::move (entries), context, keepers, [&context] (const FileEntry& entry)
    {
        ItemResult result;
        const auto deletion = FileSystem::deleteTree (entry.file, stopperOf (context));

        if (deletion.complete())
        {
            result.outcome = Outcome::done;
        }
        else if (deletion.deleted > 0 && entry.file.exists())
        {
            result.outcome = Outcome::partial;
            result.message = deletion.stopped ? "stopped half-way" : Format::count (deletion.failed, "item") + " could not be deleted (in use?)";
        }
        else
        {
            result.message = "in use, or no permission?";
        }

        return result;
    });
}

RemovalReport Removal::toFolder (std::vector<FileEntry> entries, const juce::File& destination, const ScanContext& context,
                                 const Keepers& keepers)
{
    if (! destination.isDirectory())
    {
        RemovalReport report;
        report.failures.add ("The destination folder doesn't exist: " + destination.getFullPathName());
        return report;
    }

    return forEachItem (std::move (entries), context, keepers, [&] (const FileEntry& entry)
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

        switch (MoveToFolder::moveItem (file, MoveToFolder::uniqueTarget (destination, file.getFileName()), false, stopperOf (context)))
        {
            case MoveToFolder::Outcome::moved:
                result.outcome = Outcome::done;
                break;

            case MoveToFolder::Outcome::copiedOriginalKept:
                result.outcome = Outcome::leftBehind;
                result.message = "copied, but the original could not be removed completely";
                break;

            case MoveToFolder::Outcome::containsLinks:
                result.message = "is or contains links (junctions or symbolic links), which can't be copied between drives";
                break;

            case MoveToFolder::Outcome::stopped:
                result.message = "stopped during the copy: left untouched";
                break;

            case MoveToFolder::Outcome::failed:
                result.message = "could not be moved (in use, or no permission?)";
                break;
        }

        return result;
    });
}
