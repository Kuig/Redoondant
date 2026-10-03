#pragma once

#include "FileEntry.h"

/** Helpers for moving items to a folder chosen by the user (the action itself is Removal::toFolder). */
namespace MoveToFolder
{
    enum class Outcome
    {
        moved,                  ///< Now at the target, gone from the source.
        copiedOriginalKept,     ///< The target is complete but the source could not be removed completely.
        containsLinks,          ///< A folder with junctions/symbolic links can't be copied between drives: left untouched.
        failed                  ///< Nothing changed (any partial copy was removed).
    };

    /** The path an item called `name` gets inside `destination`: the plain name if it is free,
        otherwise "name (2).ext", "name (3).ext"... so nothing is ever overwritten.
    */
    juce::File uniqueTarget (const juce::File& destination, const juce::String& name);

    /** Moves a file or folder to an exact target that must not exist. Folders that can't be
        renamed (e.g. moves between drives) are copied first (links are never followed), the copy is
        checked against the source, and the source is deleted only after that.
        forceCopy makes it always take that slower path (used by tests).
    */
    Outcome moveItem (const juce::File& source, const juce::File& target, bool forceCopy = false);
}
