#pragma once

#include <JuceHeader.h>

/** Initialises COM (multi-threaded apartment) for the lifetime of the object, on the calling thread.
    Needed by worker threads that read Windows metadata or thumbnails. No-op on other platforms.
*/
class ScopedComInit
{
public:
    ScopedComInit();
    ~ScopedComInit();

private:
    bool initialised = false;

    JUCE_DECLARE_NON_COPYABLE (ScopedComInit)
};
