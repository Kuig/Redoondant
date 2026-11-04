#pragma once

#include <JuceHeader.h>

/** The "Open in Redoondant" entry of the Explorer right-click menu of folders (and of the empty space inside a folder),
    registered for the current user only (no administrator rights needed). On Windows 11 it is under "Show more options".
*/
namespace ExplorerMenu
{
    /** True if the entry is in the menu (whichever copy of Redoondant it opens). */
    bool isInstalled();

    /** Adds the entry, opening this executable, or points an existing one at it. Returns false on failure. */
    bool install();

    /** Takes the entry out of the menu. Returns false on failure. */
    bool uninstall();

    /** If the entry opens an executable that no longer exists (Redoondant was moved), points it at this one. */
    void repairIfMoved();

    /** The folder Explorer passes on the command line ("C:\\Folder", quoted or not; options such as --test are skipped),
        or an invalid File if no argument is an existing folder.
    */
    juce::File folderFromCommandLine (const juce::String& commandLine);
}
