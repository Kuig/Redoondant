#pragma once

#include "Trash.h"

/** Moving items to a folder chosen by the user. */
namespace MoveToFolder
{
    /** The path an item called `name` gets inside `destination`: the plain name if it is free,
        otherwise "name (2).ext", "name (3).ext"... so nothing is ever overwritten.
    */
    juce::File uniqueTarget (const juce::File& destination, const juce::String& name);

    /** Moves a file or folder to an exact target that must not exist. Folders that can't be
        renamed (e.g. moves between drives) are copied first and the source deleted only if the
        copy succeeded. forceCopy makes it always take that slower path (used by tests).
    */
    bool moveItem (const juce::File& source, const juce::File& target, bool forceCopy = false);

    /** Moves the entries into the destination folder (after Trash::withoutNested).
        Refuses to move a folder into itself, and skips items already in the destination.
    */
    RemovalReport run (std::vector<FileEntry> entries, const juce::File& destination, const Trash::Progress& progress = {});
}
