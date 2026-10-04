#pragma once

#include <JuceHeader.h>

/** File operations that can be interrupted and say why they failed (juce::File's can't). */
namespace FileSystem
{
    using ShouldStop = std::function<bool()>;

    enum class Rename { done, otherVolume, failed };

    /** Renames (moves on the same volume) a file or folder to a target that must not exist. otherVolume: it can only
        be copied there. Never copies.
    */
    Rename rename (const juce::File& source, const juce::File& target);

    /** Copies a file to a target that must not exist; shouldStop is polled during the copy (a stopped copy leaves nothing behind). */
    bool copyFile (const juce::File& source, const juce::File& target, const ShouldStop& shouldStop = {});

    struct Deletion
    {
        int deleted = 0, failed = 0;
        bool stopped = false;

        bool complete() const noexcept      { return failed == 0 && ! stopped; }
    };

    /** The path the file system really uses for an existing file or folder: junctions, symbolic links and subst drives
        resolved. The file itself if it can't be opened.
    */
    juce::File finalPath (const juce::File& file);

    /** Deletes a file, or a folder with everything in it, for good. Links are deleted, never followed; read-only items
        are deleted too. Goes on after a failure (a file in use), and stops between items when shouldStop() is true.
    */
    Deletion deleteTree (const juce::File& file, const ShouldStop& shouldStop = {});
}
