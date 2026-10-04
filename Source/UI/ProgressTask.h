#pragma once

#include "../Core/ScanContext.h"

/** Long work (an analysis, a removal) on a background thread behind a modal progress window with a Cancel button. */
namespace ProgressTask
{
    /** Runs `work` on a background thread with COM initialised. It gets a context whose isCancelled() turns true when
        the user presses Cancel (it must poll it: the thread is never killed) and whose progress() updates the window.
        Then `done` runs on the message thread, only if `owner` still exists, with whether the user cancelled and the
        message of an exception that stopped `work` (empty if none). Results travel through what the lambdas share.
    */
    void run (juce::Component& owner, const juce::String& title, std::function<void (const ScanContext&)> work,
              std::function<void (bool cancelled, const juce::String& error)> done);

    /** True while a task runs: quitting then would destroy what it is working on. */
    bool isRunning();
}
