#pragma once

#include <JuceHeader.h>
#include <set>

/** How paths are compared everywhere: on Windows file names ignore case, so two spellings of a path are the same item. */
namespace Paths
{
    /** The key to compare or index paths with: lower case where file names are case-insensitive (Windows). */
    inline juce::String key (const juce::File& file)
    {
        const auto& path = file.getFullPathName();
        return juce::File::areFileNamesCaseSensitive() ? path : path.toLowerCase();
    }

    /** True for a drive root (or "/"): the end of every chain of parents. */
    inline bool isRoot (const juce::File& file)     { return file == file.getParentDirectory(); }

    /** True if one of the folders containing `file` (drive roots excepted) has its key() in `keys`. */
    inline bool hasAncestorIn (const juce::File& file, const std::set<juce::String>& keys)
    {
        for (auto parent = file.getParentDirectory(); ! isRoot (parent); parent = parent.getParentDirectory())
            if (keys.count (key (parent)) > 0)
                return true;

        return false;
    }
}
