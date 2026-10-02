#pragma once

#include <JuceHeader.h>

namespace ShellIcon
{
    /** The icon the operating system shows for a file or folder: the type's icon, or the file's own
        icon for executables, shortcuts, icons... (never a content thumbnail). Returns an invalid image on failure.
        Call from a thread that has initialised COM (see ScopedComInit).
    */
    juce::Image get (const juce::File& file, int pixels);
}
