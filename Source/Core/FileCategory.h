#pragma once

#include "FileEntry.h"

/** Coarse file types, used for filtering and for choosing a preview. */
enum class FileCategory
{
    folder,
    image,
    audio,
    video,
    document,
    archive,
    executable,
    text,
    other
};

namespace FileCategories
{
    FileCategory of (const juce::File& file, bool isDirectory);
    inline FileCategory of (const FileEntry& entry)     { return of (entry.file, entry.isDirectory); }

    juce::String nameOf (FileCategory category);

    /** All categories, in display order. */
    const std::vector<FileCategory>& all();
}
