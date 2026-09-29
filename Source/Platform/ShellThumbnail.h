#pragma once

#include <JuceHeader.h>

namespace ShellThumbnail
{
    /** The thumbnail the operating system shows for a file (video frame, Office page...),
        or an invalid image if there is none (generic icons are not returned).
        Call from a thread that has initialised COM (see ScopedComInit).
    */
    juce::Image get (const juce::File& file, int maxPixels);
}
