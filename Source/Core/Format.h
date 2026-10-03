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

    /** Text with paths that message boxes can wrap: invisible break opportunities (zero-width spaces) after each separator. */
    inline juce::String wrappable (const juce::String& text)
    {
        const juce::String zeroWidthSpace (juce::CharPointer_UTF8 ("\xe2\x80\x8b"));
        return text.replace ("\\", "\\" + zeroWidthSpace).replace ("/", "/" + zeroWidthSpace);
    }

    template <typename Integer>
    juce::String count (Integer n, const juce::String& noun)
    {
        static_assert (std::is_integral_v<Integer>, "count() takes a whole number");
        return juce::String ((juce::int64) n) + " " + noun + (n == 1 ? "" : "s");
    }
}
