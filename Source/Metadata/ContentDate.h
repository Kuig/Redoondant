#pragma once

#include "Metadata.h"
#include "../Core/FileEntry.h"
#include "../Core/ScanContext.h"

/** The "content created" date of a file: the oldest plausible date written in its metadata
    (EXIF date taken, media encoding date, document creation date, PDF creation date...),
    as opposed to the file-system dates, which change whenever the file is copied.
*/
namespace ContentDate
{
    /** The oldest valid date among the metadata's date items, or a null time (0) if there is none.
        Dates before 1980 and in the future are ignored: tools write placeholders there.
        If the file is given, a "document date" (created, saved...) equal to its own file-system dates is ignored too:
        Windows invents them for files that have no real document properties (e.g. plain text).
    */
    juce::Time oldestOf (const Metadata& metadata, const juce::File& file = {});

    /** Reads the file's metadata and returns oldestOf() it. Results are cached (by path and modification time).
        May be slow: call from a background thread that has initialised COM (see ComInit). Thread-safe.
    */
    juce::Time of (const juce::File& file);

    /** Fills `contentCreated` of the files (not folders) whose date is still unknown, reporting progress
        and stopping early when the scan is cancelled. Same threading requirements as of().
    */
    void fill (std::vector<FileEntry>& entries, const ScanContext& context);
}
