#pragma once

#include "FileEntry.h"

/** Folders whose content can be emptied without harm (temporary files, caches): the default
    list, path helpers (environment variables) and the grouping used to present them.
*/
namespace EmptyFolders
{
    struct Default
    {
        juce::String path;          ///< May contain %VARIABLE% tokens, so it doesn't depend on the user.
        bool checkedByDefault;      ///< False for big caches that are convenient to keep (browsers...).
    };

    const std::vector<Default>& defaults();

    /** Replaces %VARIABLE% tokens by their value; unknown variables are left untouched. */
    juce::String expand (const juce::String& path);

    /** The reverse for the well-known folders (a path under %LOCALAPPDATA% becomes "%LOCALAPPDATA%\..."). */
    juce::String contract (const juce::String& path);

    /** True for folders that must never be emptied: drive roots, and the user profile, AppData, Windows,
        Program Files... or any folder that contains one of them.
    */
    bool isTooBroad (const juce::File& folder);

    struct Group
    {
        juce::File parent;
        std::vector<size_t> members;    ///< Indices into the list given to groupByCommonParent, sorted by path.
    };

    /** Groups folders under the deepest directory (not a drive root) that contains at least one
        other listed folder, or under their own parent if there is none. Groups are sorted by path.
    */
    std::vector<Group> groupByCommonParent (const std::vector<juce::File>& folders);

    /** The direct children of a folder (the things that "emptying" it moves), with sizes. */
    std::vector<FileEntry> contentsOf (const juce::File& folder);
}
