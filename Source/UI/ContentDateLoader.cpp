#include "ContentDateLoader.h"
#include "../Metadata/ContentDate.h"

namespace
{
    constexpr size_t maxDateBatch = 1000;       // Content dates handed to the UI thread at a time...
    constexpr juce::uint32 dateBatchMs = 500;   // ...or after this long.
}

ContentDateLoader::ContentDateLoader (ResultsModel& m, ResultsTable& t, std::function<void()> onChanged)
    : model (m), table (t), changed (std::move (onChanged))
{
}

void ContentDateLoader::cancel()
{
    worker.cancel();
    pendingCount = 0;
}

void ContentDateLoader::load (bool restart)
{
    if (restart)        // The list changed: a running read is stale whether or not the column is shown.
    {
        const bool wasReading = pendingCount > 0;
        cancel();

        if (wasReading)
            changed();
    }

    if (! table.isColumnVisible (Column::contentCreated) || pendingCount > 0)
        return;

    auto files = model.filesNeedingContentDate();
    pendingCount = (int) files.size();
    changed();

    if (files.empty())
        return;

    worker.submit ([this, files = std::move (files)] (const LatestOnlyWorker::Job& job)
    {
        std::vector<std::pair<juce::File, juce::Time>> batch;
        auto lastFlush = juce::Time::getMillisecondCounter();

        const auto flush = [&] (bool last)
        {
            auto sent = std::move (batch);
            batch.clear();
            lastFlush = juce::Time::getMillisecondCounter();

            // Only runs while this read is the latest, so the loader (and the page that owns it) is alive.
            job.deliver ([this, sent = std::move (sent), last]
            {
                model.setContentDates (sent, last);
                pendingCount -= (int) sent.size();
                table.refresh();
                changed();
            });
        };

        for (const auto& entry : files)
        {
            if (! job.isCurrent())
                return;

            batch.emplace_back (entry.file, ContentDate::of (entry));

            // Each batch costs the UI thread a pass over the whole list: send many at once, but still show progress.
            if (batch.size() >= maxDateBatch || juce::Time::getMillisecondCounter() - lastFlush >= dateBatchMs)
                flush (false);
        }

        flush (true);
    },
    [this] (const juce::String&)
    {
        // The read stopped: don't show "reading dates" for ever, and let a later load() try again.
        pendingCount = 0;
        changed();
    });
}
