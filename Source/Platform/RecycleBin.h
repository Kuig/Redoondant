#pragma once

#include <JuceHeader.h>

namespace RecycleBin
{
    /** True if deleting the file with "send to the Recycle Bin" really recycles it: the file is on a
        fixed drive whose Recycle Bin is not turned off. Network shares, removable drives and drives set to
        "don't move files to the Recycle Bin" are not supported: Windows deletes there for good.
    */
    bool isSupported (const juce::File& file);

    /** Sends the file or folder to the Recycle Bin. Returns false (leaving it where it is) if that
        didn't happen, including when Windows would have deleted it for good (e.g. too big for the bin).
        Call only for files where isSupported() is true.
    */
    bool move (const juce::File& file);
}
