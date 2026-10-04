#include "MoveToFolder.h"
#include "../Platform/FileSystem.h"

juce::File MoveToFolder::uniqueTarget (const juce::File& destination, const juce::String& name)
{
    const auto plain = destination.getChildFile (name);

    if (! plain.exists())
        return plain;

    // "report.pdf" -> "report (2).pdf", "report (3).pdf"... (Explorer-style numbering).
    auto extension = juce::File (name).getFileExtension();

    if (extension.length() == name.length())    // ".gitignore": a name that is only an "extension" has no stem to number.
        extension = {};

    const auto stem = name.dropLastCharacters (extension.length());

    for (int number = 2;; ++number)
    {
        const auto candidate = destination.getChildFile (stem + " (" + juce::String (number) + ")" + extension);

        if (! candidate.exists())
            return candidate;
    }
}

namespace
{
    struct TreeSize
    {
        int items = 0;
        juce::int64 bytes = 0;
        bool hasLinks = false;
    };

    TreeSize measure (const juce::File& folder)
    {
        TreeSize total;

        for (const auto& entry : juce::RangedDirectoryIterator (folder, false, "*", juce::File::findFilesAndDirectories,
                                                                juce::File::FollowSymlinks::no))
        {
            const auto file = entry.getFile();

            if (file.isSymbolicLink())
            {
                total.hasLinks = true;
                continue;
            }

            ++total.items;

            if (entry.isDirectory())
            {
                const auto inner = measure (file);
                total.items += inner.items;
                total.bytes += inner.bytes;
                total.hasLinks = total.hasLinks || inner.hasLinks;
            }
            else
            {
                total.bytes += entry.getFileSize();
            }
        }

        return total;
    }

    /** Copies a folder tree without following links; false if anything could not be copied (or shouldStop said so). */
    bool copyTree (const juce::File& source, const juce::File& target, const FileSystem::ShouldStop& shouldStop)
    {
        if (! target.createDirectory())
            return false;

        for (const auto& entry : juce::RangedDirectoryIterator (source, false, "*", juce::File::findFilesAndDirectories,
                                                                juce::File::FollowSymlinks::no))
        {
            const auto file = entry.getFile();
            const auto copy = target.getChildFile (file.getFileName());

            if (entry.isDirectory() ? ! copyTree (file, copy, shouldStop) : ! FileSystem::copyFile (file, copy, shouldStop))
                return false;
        }

        return true;
    }
}

MoveToFolder::Outcome MoveToFolder::moveItem (const juce::File& source, const juce::File& target, bool forceCopy,
                                              const std::function<bool()>& shouldStop)
{
    if (target.exists())
        return Outcome::failed;     // Never overwrite.

    if (! forceCopy)
    {
        const auto renamed = FileSystem::rename (source, target);

        if (renamed != FileSystem::Rename::otherVolume)
            return renamed == FileSystem::Rename::done ? Outcome::moved : Outcome::failed;
    }

    if (source.isSymbolicLink())
        return Outcome::containsLinks;      // Copying would copy what it leads to, then delete only the link.

    const auto stopped = [&] { return shouldStop != nullptr && shouldStop(); };

    if (! source.isDirectory())
    {
        if (! FileSystem::copyFile (source, target, shouldStop))
            return stopped() ? Outcome::stopped : Outcome::failed;

        return FileSystem::deleteTree (source).complete() ? Outcome::moved : Outcome::copiedOriginalKept;
    }

    // Folders can't be renamed across drives: copy, check the copy, and only then remove the original.
    const auto before = measure (source);

    if (before.hasLinks)
        return Outcome::containsLinks;

    const auto copiedWell = copyTree (source, target, shouldStop);
    const auto after = copiedWell ? measure (target) : TreeSize();

    if (! copiedWell || after.items != before.items || after.bytes != before.bytes)
    {
        FileSystem::deleteTree (target);    // Don't leave a partial copy behind.
        return stopped() ? Outcome::stopped : Outcome::failed;
    }

    return FileSystem::deleteTree (source).complete() ? Outcome::moved : Outcome::copiedOriginalKept;
}
