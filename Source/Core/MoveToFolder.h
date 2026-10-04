#pragma once

#include "FileEntry.h"

/** Helpers for moving items to a folder chosen by the user (the action itself is Removal::toFolder). */
namespace MoveToFolder
{
    enum class Outcome
    {
        moved,                  ///< Now at the target, gone from the source.
        copiedOriginalKept,     ///< The target is complete but the source could not be removed completely.
        containsLinks,          ///< A link, or a folder with junctions/symbolic links, can't be copied between drives: left untouched.
        stopped,                ///< Cancelled during a copy between drives: nothing changed (the partial copy was removed).
        failed                  ///< Nothing changed (any partial copy was removed).
    };

    /** The path an item called `name` gets inside `destination`: the plain name if it is free,
        otherwise "name (2).ext", "name (3).ext"... so nothing is ever overwritten.
    */
    juce::File uniqueTarget (const juce::File& destination, const juce::String& name);

    /** Moves a file or folder to an exact target that must not exist. A rename is tried first; only when the target
        is on another volume is the item copied (links are never followed, shouldStop is polled during the copy), the
        copy checked against the source, and the source deleted after that. A rename that fails for another reason
        (a file in use, no permission) fails: copying would only leave the same file in two places.
        forceCopy makes it always take the copying path (used by tests).
    */
    Outcome moveItem (const juce::File& source, const juce::File& target, bool forceCopy = false,
                      const std::function<bool()>& shouldStop = {});
}
