#pragma once

#include "LatestOnlyWorker.h"
#include "ResultsTable.h"

/** Reads the "content created" dates of the listed files in the background (they can take a while: every file
    is opened) and fills them into the model as they arrive. Only done while the column is shown.
*/
class ContentDateLoader
{
public:
    /** @param changed   called on the message thread whenever the number of dates still to read changed. */
    ContentDateLoader (ResultsModel& model, ResultsTable& table, std::function<void()> changed);

    /** Starts reading the dates the list still lacks, if the column is visible. With `restart` a running read is
        abandoned first (the list changed); otherwise it is left to finish.
    */
    void load (bool restart);

    /** Abandons a running read. */
    void cancel();

    /** Dates still to be read by the running read. */
    int pending() const noexcept        { return pendingCount; }

private:
    ResultsModel& model;
    ResultsTable& table;
    std::function<void()> changed;
    LatestOnlyWorker worker { true };       // Reading dates uses the Windows Property System.
    int pendingCount = 0;

    JUCE_DECLARE_NON_COPYABLE (ContentDateLoader)
};
