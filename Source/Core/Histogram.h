#pragma once

#include "DateKind.h"
#include "FileEntry.h"
#include <optional>

/** A property of the listed entries that can be summed up in a histogram (the columns of the table, except the name). */
enum class Property { folder, size, modified, created, contentCreated, type, contentKind };

/** What the height of a bar means. */
enum class Metric { count, size };

namespace Histogram
{
    const juce::StringArray& propertyNames();

    /** The property at a position of propertyNames() (clamped to the existing ones). */
    inline Property propertyAt (int index)      { return (Property) juce::jlimit (0, propertyNames().size() - 1, index); }

    /** The date a date property looks at; the other properties have none. */
    std::optional<DateKind> dateKindOf (Property property);

    /** Buckets shared by the histogram and the filters, so a bar and the filter it sets always agree. */
    namespace Buckets
    {
        /** Today, 1-7d, 7-30d, 30d-1y, 1-2y, >2y (days and years ago), then "unknown date". */
        const juce::StringArray& dateNames();
        constexpr int unknownDate = 6;

        /** Index in dateNames() of the age of `time` at `now` (a null time is unknown). */
        int dateBucket (juce::Time time, juce::Time now);

        const juce::StringArray& sizeNames();
        int sizeBucket (juce::int64 bytes);

        /** The MB range of a size bucket, as the size filter takes it: `toMB` 0 = no upper limit. */
        struct SizeRange { double fromMB, toMB; };
        SizeRange sizeRange (int bucket);
    }

    /** The folder of an entry as the Folder column shows it: relative to `root`, empty for the root itself,
        and the full path when there is no root.
    */
    juce::String folderOf (const FileEntry& entry, const juce::File& root);

    /** Summary of the entries of one bar. */
    struct Stats
    {
        int count = 0;
        juce::int64 bytes = 0;
        juce::int64 largestSize = -1;
        juce::String largestName;
        juce::Time oldest, newest;      ///< Modification dates (null: none).

        void add (const FileEntry& entry);
        juce::int64 average() const     { return count > 0 ? bytes / count : 0; }
    };

    /** One bar, in three layers: every entry, the visible ones (passing the filters) and the checked ones. */
    struct Bin
    {
        juce::String label;
        int key = -1;                   ///< The bucket, or the index in FileCategories::all() for types, in ContentKinds::all() for kinds.
        juce::String folder;            ///< For folders: what the Folder filter takes.
        bool filterable = true;         ///< Whether clicking it can set a filter.
        Stats total, filtered, checked;
    };

    struct Input
    {
        std::vector<FileEntry> all, visible, checked;   ///< checked is a subset of visible, visible of all.
        juce::File root;                                ///< The analysed folder (for the Folder property); none if there is no list.
    };

    /** The bars for `property`. Date and size bars are always the same; types and kinds are those present (with the
        items not classified yet apart, in a bar that sets no filter); folders are the
        ten with most entries (or bytes) plus "Others", ranked on all the entries.
    */
    std::vector<Bin> compute (const Input& input, Property property, Metric metric, const juce::File& root, juce::Time now);

    inline juce::int64 valueOf (const Stats& stats, Metric metric)      { return metric == Metric::count ? stats.count : stats.bytes; }
}
