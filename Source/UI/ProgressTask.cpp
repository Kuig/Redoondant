#include "ProgressTask.h"
#include "../Platform/ComInit.h"

namespace
{
    std::atomic<int> running { 0 };

    /** Deletes itself when done. Cancel waits for the work to notice (no time-out: killing the thread could leave
        a file half-copied, a lock held or a report lost).
    */
    class Task final : public juce::ThreadWithProgressWindow
    {
    public:
        Task (juce::Component& ownerComponent, const juce::String& title, std::function<void (const ScanContext&)> w,
              std::function<void (bool, const juce::String&)> d)
            : ThreadWithProgressWindow (title, true, true, -1, {}, &ownerComponent),
              owner (&ownerComponent), work (std::move (w)), done (std::move (d))
        {
            context.shouldStop = [this] { return threadShouldExit(); };
            context.onProgress = [this] (double value, const juce::String& text)
            {
                setProgress (value);
                setStatusMessage (text);
            };
            setProgress (-1.0);
            ++running;
        }

        void run() override
        {
            // An exception (out of memory on a huge tree, a failing codec...) must not take the whole program down.
            try
            {
                const ScopedComInit com;    // Metadata, thumbnails and the Recycle Bin use COM.
                work (context);
            }
            catch (const std::exception& e)     { error = e.what(); }
            catch (...)                         { error = "unknown error"; }
        }

        void threadComplete (bool cancelled) override
        {
            --running;

            if (owner != nullptr)
                done (cancelled, error);

            delete this;
        }

    private:
        juce::Component::SafePointer<juce::Component> owner;
        std::function<void (const ScanContext&)> work;
        std::function<void (bool, const juce::String&)> done;
        ScanContext context;
        juce::String error;
    };
}

void ProgressTask::run (juce::Component& owner, const juce::String& title, std::function<void (const ScanContext&)> work,
                        std::function<void (bool, const juce::String&)> done)
{
    (new Task (owner, title, std::move (work), std::move (done)))->launchThread();
}

bool ProgressTask::isRunning()
{
    return running.load() > 0;
}
