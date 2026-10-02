#pragma once

#include "../Core/FileEntry.h"

/** Small system icons for the file list. Icons are fetched from the shell on a background thread:
    get() returns what is cached (an invalid image until it arrives) and `onIconLoaded` fires on the
    message thread when a new one is ready, so the UI thread never waits for the shell.
*/
class IconCache
{
public:
    IconCache();
    ~IconCache();

    /** The icon for an entry, or an invalid image if it isn't loaded yet (it is requested). */
    juce::Image get (const FileEntry& entry);

    /** Called (on the message thread) when icons were added: repaint. */
    std::function<void()> onIconLoaded;

    static constexpr int iconSize = 16;

private:
    /** Icons that live inside the file itself, so every file has its own; the others are per extension. */
    static bool hasOwnIcon (const juce::File& file);
    static juce::String keyFor (const FileEntry& entry);

    struct Shared;
    std::shared_ptr<Shared> shared;     // Shared with the loader jobs, which may outlive this object briefly.
    juce::ThreadPool loader { 1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IconCache)
};
