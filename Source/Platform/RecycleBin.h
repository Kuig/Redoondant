#pragma once

#include <JuceHeader.h>
#include <map>

namespace RecycleBin
{
    /** True if deleting the file with "send to the Recycle Bin" really recycles it: the file is on a fixed volume
        (the volume it is really on, also for folders where a drive is mounted) whose Recycle Bin is not turned off.
        Network shares, removable drives and drives set to "don't move files to the Recycle Bin" are not supported:
        Windows deletes there for good.
    */
    bool isSupported (const juce::File& file);

    /** isSupported() for many files: each folder and each volume is asked once. */
    class Support
    {
    public:
        bool isSupported (const juce::File& file);

    private:
        std::map<juce::String, bool> byFolder, byVolume;
    };

    enum class Outcome
    {
        recycled,
        deletedForGood,     ///< Windows couldn't recycle it (too big for the Recycle Bin...) and the user agreed to delete it.
        failed              ///< Still there.
    };

    /** Sends the files and folders to the Recycle Bin in one shell operation (much faster than one at a time), and says
        what happened to each. Where Windows can't recycle an item it asks the user first. `shouldStop` is polled before
        each item; when it returns true the remaining items are left in place (failed). Call only for supported files,
        from a thread that has initialised COM.
    */
    std::vector<Outcome> move (const std::vector<juce::File>& files, const std::function<bool()>& shouldStop = {});
}
