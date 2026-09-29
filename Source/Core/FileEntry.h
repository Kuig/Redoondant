#pragma once

#include <JuceHeader.h>

/** A file or folder found during a scan, as listed in the results table. */
struct FileEntry
{
    juce::File file;
    bool isDirectory = false;
    juce::int64 size = 0;       ///< For folders: total size of their content (when computed).
    int fileCount = 0;          ///< For folders: number of files they contain, recursively.
    juce::Time modified;
    juce::Time created;
    bool selected = false;      ///< Checked for deletion.

    juce::String name() const   { return file.getFileName(); }

    /** Builds an entry by querying the file system (folder sizes are computed recursively). */
    static FileEntry fromFile (const juce::File& file);
};
