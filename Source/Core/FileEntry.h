#pragma once

#include <JuceHeader.h>
#include "ContentKind.h"

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
    ContentKind contentKind = ContentKind::unknown;     ///< What it holds (see ContentKinds::Classifier); unknown until classified.

    juce::String name() const   { return file.getFileName(); }
};

/** The sum of the sizes of the entries (a folder counts its whole content). Works on any vector of FileEntry-derived items. */
template <typename Entry>
juce::int64 totalSize (const std::vector<Entry>& entries)
{
    juce::int64 bytes = 0;

    for (const auto& entry : entries)
        bytes += entry.size;

    return bytes;
}

/** Latest / earliest modification time of the entries, in milliseconds (0 / max when there are none). */
template <typename Entry>
juce::int64 newestModified (const std::vector<Entry>& entries)
{
    juce::int64 latest = 0;

    for (const auto& entry : entries)
        latest = juce::jmax (latest, entry.modified.toMilliseconds());

    return latest;
}

template <typename Entry>
juce::int64 oldestModified (const std::vector<Entry>& entries)
{
    juce::int64 earliest = std::numeric_limits<juce::int64>::max();

    for (const auto& entry : entries)
        earliest = juce::jmin (earliest, entry.modified.toMilliseconds());

    return earliest;
}
