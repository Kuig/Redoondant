#pragma once

#include "LatestOnlyWorker.h"
#include "../Core/FileEntry.h"
#include "../Core/ScanContext.h"

/** Fills in the background a value of the listed entries that takes a while to find (the content created date: every
    file is opened; the content kind: folders and archives are read), handing it to the list in batches as it arrives.
*/
template <typename Value>
class BackgroundLoader
{
public:
    using Batch = std::vector<std::pair<juce::File, Value>>;
    using Reader = std::function<Value (const FileEntry&)>;

    struct Source
    {
        std::function<bool()> wanted;                       ///< Whether to load now (e.g. only while a column is shown).
        std::function<std::vector<FileEntry>()> needing;    ///< The entries still without the value.

        /** Called on the background thread at the start of each load: the reader for its entries. The context tells
            when the load is abandoned.
        */
        std::function<Reader (const ScanContext&)> startReading;

        /** Called on the message thread with each batch; `last` for the final one. */
        std::function<void (const Batch&, bool last)> apply;
    };

    /** @param changed      called on the message thread whenever the number of entries still to read changed.
        @param needsCom     the reader uses the Windows Property System or the shell.
    */
    BackgroundLoader (Source s, std::function<void()> onChanged, bool needsCom)
        : source (std::move (s)), changed (std::move (onChanged)), worker (needsCom) {}

    /** Starts reading what the list still lacks, if wanted. With `restart` a running read is abandoned first (the
        list changed); otherwise it is left to finish.
    */
    void load (bool restart)
    {
        if (restart)        // The list changed: a running read is stale whether or not it is wanted.
        {
            const bool wasReading = pendingCount > 0;
            cancel();

            if (wasReading)
                changed();
        }

        if (! source.wanted() || pendingCount > 0)
            return;

        auto entries = source.needing();
        pendingCount = (int) entries.size();
        changed();

        if (entries.empty())
            return;

        worker.submit ([this, startReading = source.startReading, entries = std::move (entries)] (const LatestOnlyWorker::Job& job)
        {
            ScanContext context;
            context.shouldStop = [&job] { return ! job.isCurrent(); };
            const auto read = startReading (context);

            Batch batch;
            auto lastFlush = juce::Time::getMillisecondCounter();

            const auto flush = [&] (bool last)
            {
                auto sent = std::move (batch);
                batch.clear();
                lastFlush = juce::Time::getMillisecondCounter();

                // Only runs while this read is the latest, so the loader (and the page that owns it) is alive.
                job.deliver ([this, sent = std::move (sent), last]
                {
                    source.apply (sent, last);
                    pendingCount -= (int) sent.size();
                    changed();
                });
            };

            for (const auto& entry : entries)
            {
                if (! job.isCurrent())
                    return;

                batch.emplace_back (entry.file, read (entry));

                // Each batch costs the UI thread a pass over the whole list: send many at once, but still show progress.
                if (batch.size() >= maxBatch || juce::Time::getMillisecondCounter() - lastFlush >= batchMs)
                    flush (false);
            }

            flush (true);
        },
        [this] (const juce::String&)
        {
            // The read stopped: don't show it as running for ever, and let a later load() try again.
            pendingCount = 0;
            changed();
        });
    }

    /** Abandons a running read. */
    void cancel()
    {
        worker.cancel();
        pendingCount = 0;
    }

    /** Entries still to be read by the running read. */
    int pending() const noexcept        { return pendingCount; }

private:
    static constexpr size_t maxBatch = 1000;        // Values handed to the UI thread at a time...
    static constexpr juce::uint32 batchMs = 500;    // ...or after this long.

    Source source;
    std::function<void()> changed;
    LatestOnlyWorker worker;
    int pendingCount = 0;

    JUCE_DECLARE_NON_COPYABLE (BackgroundLoader)
};
