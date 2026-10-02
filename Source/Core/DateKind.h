#pragma once

#include "FileEntry.h"

/** Which date of an entry a criterion looks at. */
enum class DateKind
{
    created,        ///< File-system creation date.
    modified,       ///< File-system modification date.
    content         ///< Date written in the content's metadata (needs ContentDate::fill first).
};

inline juce::Time dateOf (const FileEntry& entry, DateKind kind)
{
    return kind == DateKind::created ? entry.created : (kind == DateKind::modified ? entry.modified : entry.contentCreated);
}
