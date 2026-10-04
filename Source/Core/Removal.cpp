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
    // Sorted with the separator before every other character, the content of a path comes right after it:
    // one pass then drops what lies inside the last item kept (and the duplicates).
    const auto separator = juce::File::getSeparatorChar();
    std::vector<std::pair<juce::String, size_t>> sorted;
    sorted.reserve (entries.size());

    for (size_t i = 0; i < entries.size(); ++i)
        sorted.emplace_back (Paths::key (entries[i].file).replaceCharacter (separator, (juce::juce_wchar) 1), i);

    std::stable_sort (sorted.begin(), sorted.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });

    std::vector<size_t> kept;
    juce::String insideKept;        // "<last kept path><separator>"

    for (const auto& [key, index] : sorted)
    {
        if (insideKept.isNotEmpty() && (key + juce::String::charToString (1)).startsWith (insideKept))
            continue;       // The same path again, or something inside it.

        kept.push_back (index);
        insideKept = key.endsWithChar (1) ? key : key + juce::String::charToString (1);     // A drive root already ends with one.
    }

    std::sort (kept.begin(), kept.end());       // Back to the order of the entries.
    std::vector<FileEntry> result;
    result.reserve (kept.size());

    for (const auto index : kept)
        result.push_back (std::move (entries[index]));

    return result;
}

int Removal::countWithoutRecycleBin (std::vector<FileEntry> entries)
{
    RecycleBin::Support support;
    int count = 0;

    for (const auto& entry : withoutNested (std::move (entries)))
        if (! support.isSupported (entry.file))
            ++count;

    return count;
}

namespace
{
    /** What an action did with one item: skipped items are left alone without being failures;
        leftBehind ones were copied but their original couldn't be removed completely; partial ones were deleted in part.
    */
    enum class Outcome { done, doneForGood, failed, skipped, leftBehind, partial };

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

    /** Acts on a batch of items that passed the checks, returning one result per item. */
    using BatchAction = std::function<std::vector<ItemResult> (const std::vector<FileEntry>&)>;

    /** Runs `act` on the items (after withoutNested) in batches of up to `batchSize`, with progress and cancellation,
        and fills the report. Items that don't exist any more are counted apart: they are gone from the list but not
        "removed" by us. After Cancel, the items checked but not acted on yet are left untouched.
    */
    RemovalReport forEachItem (std::vector<FileEntry> entries, const ScanContext& context, const Removal::Keepers& keepers,
                               size_t batchSize, const BatchAction& act)
    {
        RemovalReport report;
        const auto items = Removal::withoutNested (std::move (entries));

        juce::int64 totalBytes = 0, doneBytes = 0;

        for (const auto& entry : items)
            totalBytes += entry.size;

        std::vector<FileEntry> batch;

        const auto record = [&report] (const FileEntry& entry, const ItemResult& result)
        {
            const auto described = entry.file.getFullPathName() + (result.message.isEmpty() ? juce::String() : "  (" + result.message + ")");

            switch (result.outcome)
            {
                case Outcome::done:
                case Outcome::doneForGood:
                    ++report.processed;
                    report.bytes += entry.size;
                    report.files.add (entry.file);

                    if (result.outcome == Outcome::doneForGood)
                        report.deletedForGood.add (entry.file.getFullPathName());

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
        };

        const auto actOnBatch = [&]
        {
            if (batch.empty())
                return;

            std::vector<ItemResult> results;

            try
            {
                results = act (batch);
            }
            catch (const std::exception& e)     { results.assign (batch.size(), { Outcome::failed, e.what() }); }
            catch (...)                         { results.assign (batch.size(), { Outcome::failed, "unexpected error" }); }

            results.resize (batch.size());

            for (size_t k = 0; k < batch.size(); ++k)
                record (batch[k], results[k]);

            batch.clear();
        };

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

            batch.push_back (entry);

            if (batch.size() >= batchSize)
                actOnBatch();
        }

        if (! report.cancelled)
            actOnBatch();

        report.cancelled = report.cancelled || context.isCancelled();
        return report;
    }

    /** forEachItem for actions that take one item at a time. */
    RemovalReport forEachItem (std::vector<FileEntry> entries, const ScanContext& context, const Removal::Keepers& keepers,
                               const std::function<ItemResult (const FileEntry&)>& act)
    {
        return forEachItem (std::move (entries), context, keepers, 1, [&act] (const std::vector<FileEntry>& batch)
        {
            return std::vector<ItemResult> { act (batch.front()) };
        });
    }

    /** Items sent to the Recycle Bin in one shell operation: much faster than one at a time, still often enough to show progress. */
    constexpr size_t trashBatch = 200;

    FileSystem::ShouldStop stopperOf (const ScanContext& context)
    {
        return [&context] { return context.isCancelled(); };
    }
}

RemovalReport Removal::toTrash (std::vector<FileEntry> entries, const ScanContext& context, const Keepers& keepers)
{
    RecycleBin::Support support;

    return forEachItem (std::move (entries), context, keepers, trashBatch, [&] (const std::vector<FileEntry>& batch)
    {
        std::vector<ItemResult> results (batch.size());
        std::vector<juce::File> files;
        std::vector<size_t> sent;

        for (size_t i = 0; i < batch.size(); ++i)
        {
            if (support.isSupported (batch[i].file))
            {
                files.push_back (batch[i].file);
                sent.push_back (i);
            }
            else
            {
                results[i].message = "no Recycle Bin on this drive: left untouched";
            }
        }

        const auto outcomes = RecycleBin::move (files, stopperOf (context));

        for (size_t k = 0; k < sent.size(); ++k)
        {
            auto& result = results[sent[k]];

            switch (outcomes[k])
            {
                case RecycleBin::Outcome::recycled:         result.outcome = Outcome::done; break;
                case RecycleBin::Outcome::deletedForGood:   result.outcome = Outcome::doneForGood; break;
                case RecycleBin::Outcome::failed:           result.message = context.isCancelled() ? "stopped: left untouched"
                                                                                                   : "Windows could not recycle it (in use?)"; break;
            }
        }

        return results;
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
