#pragma once

#include <JuceHeader.h>
#include <functional>

struct ScannedTree;

/** Everything an analysis needs to know about the scan it is running in.
    Analyses run on a background thread and must poll isCancelled() regularly.
*/
struct ScanContext
{
    juce::File root;
    bool recursive = false;

    /** Optional: the whole tree under a folder, already scanned. Scans of that folder or of folders inside it are then
        taken from it instead of the disk (several analyses of the same folder share one walk). See FileScanner.
    */
    std::shared_ptr<const ScannedTree> tree;

    std::function<bool()> shouldStop;
    std::function<void (double progress, const juce::String& message)> onProgress;

    bool isCancelled() const    { return shouldStop != nullptr && shouldStop(); }

    /** Reports progress (0..1, or negative for indeterminate) and a status message. */
    void progress (double value, const juce::String& message) const
    {
        if (onProgress != nullptr)
            onProgress (value, message);
    }
};
