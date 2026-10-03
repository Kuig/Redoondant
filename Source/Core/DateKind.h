#pragma once

#include "FileEntry.h"

/** Which date of an entry a criterion looks at. */
enum class DateKind
{
    created,        ///< File-system creation date.
    modified,       ///< File-system modification date.
    content         ///< Date written in the content's metadata (needs ContentDate::fill first).
};

namespace DateKinds
{
    /** Labels of the kinds, in DateKind order: "Created", "Modified", "Content created" (or "File created",
        "File modified" with fileNamed), each followed by `suffix`.
    */
    inline juce::StringArray names (bool fileNamed, const juce::String& suffix = {})
    {
        return { (fileNamed ? "File created" : "Created") + suffix,
                 (fileNamed ? "File modified" : "Modified") + suffix,
                 "Content created" + suffix };
    }
}

inline juce::Time dateOf (const FileEntry& entry, DateKind kind)
{
    return kind == DateKind::created ? entry.created : (kind == DateKind::modified ? entry.modified : entry.contentCreated);
}
