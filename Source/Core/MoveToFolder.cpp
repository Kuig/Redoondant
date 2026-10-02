#include "MoveToFolder.h"

juce::File MoveToFolder::uniqueTarget (const juce::File& destination, const juce::String& name)
{
    const auto plain = destination.getChildFile (name);

    if (! plain.exists())
        return plain;

    // "report.pdf" -> "report (2).pdf", "report (3).pdf"... (Explorer-style numbering).
    const auto extension = juce::File (name).getFileExtension();
    const auto stem = name.dropLastCharacters (extension.length());

    for (int number = 2;; ++number)
    {
        const auto candidate = destination.getChildFile (stem + " (" + juce::String (number) + ")" + extension);

        if (! candidate.exists())
            return candidate;
    }
}

bool MoveToFolder::moveItem (const juce::File& source, const juce::File& target, bool forceCopy)
{
    if (target.exists())
        return false;   // moveFileTo would delete it.

    if (! forceCopy && source.moveFileTo (target))
        return true;

    if (! source.isDirectory())
        return forceCopy && source.copyFileTo (target) && source.deleteFile();

    // Folders can't be renamed across drives: copy, and only then remove the original.
    if (! source.copyDirectoryTo (target))
    {
        target.deleteRecursively();     // Don't leave a partial copy behind.
        return false;
    }

    return source.deleteRecursively();
}

RemovalReport MoveToFolder::run (std::vector<FileEntry> entries, const juce::File& destination, const Trash::Progress& progress)
{
    RemovalReport report;

    if (! destination.isDirectory())
    {
        report.failures.add ("The destination folder doesn't exist: " + destination.getFullPathName());
        return report;
    }

    const auto items = Trash::withoutNested (std::move (entries));
    juce::int64 totalBytes = 0, doneBytes = 0;

    for (const auto& entry : items)
        totalBytes += entry.size;

    for (size_t i = 0; i < items.size(); ++i)
    {
        const auto& entry = items[i];
        const auto& file = entry.file;
        const double fraction = totalBytes > 0 ? (double) doneBytes / (double) totalBytes : (double) i / (double) items.size();

        if (progress != nullptr && ! progress (fraction, entry.name()))
        {
            report.cancelled = true;
            break;
        }

        doneBytes += entry.size;

        if (file.getParentDirectory() == destination)
            continue;   // Already there.

        if (file == destination || destination.isAChildOf (file))
        {
            report.failures.add (file.getFullPathName() + "  (can't be moved into itself)");
            continue;
        }

        if (moveItem (file, uniqueTarget (destination, file.getFileName())))
        {
            ++report.moved;
            report.bytesMoved += entry.size;
            report.movedFiles.add (file);
        }
        else
        {
            report.failures.add (file.getFullPathName());
        }
    }

    return report;
}
