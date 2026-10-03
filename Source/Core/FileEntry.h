#pragma once

#include <JuceHeader.h>

/** A file or folder found during a scan, as listed in the results table. */
struct FileEntry
{
    juce::File file;
    bool isDirectory = false;
    juce::int64 size = 0;       ///< For folders: total size of their content (when computed).
    int fileCount = 0;          ///< For folders: number of files they contain, recursively.
    bool contentUnknown = false;    ///< For folders: a link (junction, symbolic link) or something inside could not be listed, so size and fileCount may be too low.
    juce::Time modified;
    juce::Time created;
    juce::Time contentCreated;  ///< Oldest date written in the file's metadata (see ContentDate); unknown (0) until read.
    bool missing = false;       ///< Listed but not on disk (can't be checked).
    bool selected = false;      ///< Checked for deletion.
    bool selectedByDefault = false;     ///< Check state proposed by the criterion.

    juce::String name() const   { return file.getFileName(); }
};

/** The sum of the sizes of the entries (a folder counts its whole content). */
inline juce::int64 totalSize (const std::vector<FileEntry>& entries)
{
    juce::int64 bytes = 0;

    for (const auto& entry : entries)
        bytes += entry.size;

    return bytes;
}
