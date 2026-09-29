#pragma once

#include <JuceHeader.h>

/** Runs a function on the message thread and waits for its result, so that background
    threads can safely read UI-owned state. Runs it directly when already on the message thread.
    The message thread must not be blocked waiting for the caller (it isn't during an analysis,
    whose progress window is asynchronous).
*/
template <typename Function>
auto callOnMessageThread (Function&& function) -> decltype (function())
{
    using Result = decltype (function());

    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
        return function();

    std::optional<Result> result;
    juce::WaitableEvent done;

    juce::MessageManager::callAsync ([&]
    {
        result = function();
        done.signal();
    });

    done.wait();
    return std::move (*result);
}
