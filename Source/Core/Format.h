#pragma once

#include <JuceHeader.h>

/** Human-readable formatting shared by the whole UI. */
namespace Format
{
    inline juce::String size (juce::int64 bytes)       { return juce::File::descriptionOfSizeInBytes (bytes); }
    inline juce::String date (juce::Time time)         { return time.toMilliseconds() > 0 ? time.formatted ("%Y-%m-%d %H:%M") : juce::String(); }

    inline juce::String dateRange (juce::Time from, juce::Time to)
    {
        return from.formatted ("%Y-%m-%d") == to.formatted ("%Y-%m-%d")
                 ? date (from) + " - " + to.formatted ("%H:%M")
                 : date (from) + " - " + date (to);
    }

    inline juce::String count (size_t n, const juce::String& noun)
    {
        return juce::String ((juce::int64) n) + " " + noun + (n == 1 ? "" : "s");
    }
}
