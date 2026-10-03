#pragma once

#include <JuceHeader.h>
#include "../Platform/ComInit.h"

/** Runs background jobs one at a time on its own thread, for results that only matter while they are the
    latest request (previews, measurements, content dates): submitting a new job drops the queued ones and
    makes the running one stale, and a stale job's results never reach the UI.

    The job gets a Job handle: it polls isCurrent() to stop early and hands results to the message thread
    with deliver(). deliver() actions only run while the job is still the latest and the worker exists, so
    they may use their owner (e.g. capture `this` of the component that owns the worker).
    Jobs must not touch their owner directly, only through deliver().
*/
class LatestOnlyWorker
{
public:
    struct State
    {
        std::atomic<int> generation { 0 };
    };

    class Job
    {
    public:
        /** False once a newer job was submitted or the worker was cancelled: stop working. */
        bool isCurrent() const noexcept     { return state->generation.load() == request; }

        /** Runs `action` on the message thread, unless the job has become stale by then. */
        void deliver (std::function<void()> action) const
        {
            juce::MessageManager::callAsync ([state = state, request = request, action = std::move (action)]
            {
                if (state->generation.load() == request)
                    action();
            });
        }

    private:
        friend class LatestOnlyWorker;
        Job (std::shared_ptr<State> s, int r) : state (std::move (s)), request (r) {}

        std::shared_ptr<State> state;
        int request;
    };

    /** @param initialiseCom   the jobs read Windows metadata or thumbnails, which needs COM on their thread. */
    explicit LatestOnlyWorker (bool initialiseCom = false)  : needsCom (initialiseCom) {}

    ~LatestOnlyWorker()                                     { shutdown(); }

    /** Starts `work` after abandoning what was queued and ignoring what is running. If it throws, `onError`
        (if any) is called on the message thread with the message, unless the job is stale by then.
    */
    void submit (std::function<void (const Job&)> work, std::function<void (const juce::String&)> onError = {})
    {
        const int request = ++state->generation;
        pool.removeAllJobs (false, 0);

        pool.addJob ([job = Job (state, request), work = std::move (work), onError = std::move (onError), com = needsCom]
        {
            const auto fail = [&] (const juce::String& message)
            {
                if (onError != nullptr)
                    job.deliver ([onError, message] { onError (message); });
            };

            try
            {
                std::unique_ptr<ScopedComInit> initialised;

                if (com)
                    initialised = std::make_unique<ScopedComInit>();

                work (job);
            }
            catch (const std::exception& e)     { fail (e.what()); }
            catch (...)                         { fail ("unknown error"); }
        });
    }

    /** Makes the queued and running jobs stale; doesn't wait. */
    void cancel()
    {
        ++state->generation;
        pool.removeAllJobs (false, 0);
    }

    /** cancel(), then waits (up to `timeoutMs`) for a running job to notice and finish. */
    void shutdown (int timeoutMs = 10000)
    {
        ++state->generation;
        pool.removeAllJobs (true, timeoutMs);
    }

private:
    std::shared_ptr<State> state = std::make_shared<State>();
    juce::ThreadPool pool { 1 };
    bool needsCom;

    JUCE_DECLARE_NON_COPYABLE (LatestOnlyWorker)
};
