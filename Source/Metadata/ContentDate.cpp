#include "ContentDate.h"
#include "MetadataReader.h"
#include "../Core/Paths.h"
#include <map>
#include <mutex>
#include <optional>

namespace
{
    /** oldestOf(), with the file-system dates of the file (created, modified) given by `fileDates`, asked only if needed. */
    juce::Time oldestDate (const Metadata& metadata, const std::function<std::pair<juce::Time, juce::Time>()>& fileDates)
    {
        std::optional<std::pair<juce::Time, juce::Time>> fileTimes;

        const auto isFileSystemDate = [&] (const MetadataItem& item, juce::Time time)
        {
            if (fileDates == nullptr || ! item.key.startsWith ("System.Document.Date"))
                return false;

            if (! fileTimes)
                fileTimes = fileDates();

            const auto near = [&] (juce::Time other) { return std::abs (time.toMilliseconds() - other.toMilliseconds()) < 2000; };
            return near (fileTimes->first) || near (fileTimes->second);
        };

        const auto earliest = juce::Time (1980, 0, 1, 0, 0);
        const auto latest = juce::Time::getCurrentTime() + juce::RelativeTime::days (1);
        juce::Time oldest;

        for (const auto& item : metadata.items())
        {
            if (! item.isDate)
                continue;

            const juce::Time time ((juce::int64) item.raw);

            if (time >= earliest && time <= latest && ! isFileSystemDate (item, time) && (oldest.toMilliseconds() == 0 || time < oldest))
                oldest = time;
        }

        return oldest;
    }
}

juce::Time ContentDate::oldestOf (const Metadata& metadata, const juce::File& file)
{
    if (file == juce::File())
        return oldestDate (metadata, {});

    return oldestDate (metadata, [&] { return std::make_pair (file.getCreationTime(), file.getLastModificationTime()); });
}

juce::Time ContentDate::oldestOf (const Metadata& metadata, const FileEntry& entry)
{
    return oldestDate (metadata, [&] { return std::make_pair (entry.created, entry.modified); });
}

juce::Time ContentDate::of (const FileEntry& entry, const MetadataSource& source)
{
    if (source != nullptr)
        return oldestOf (source (entry.file), entry);

    struct Cached { juce::Time modified, result; };
    static std::mutex lock;
    static std::map<juce::String, Cached> cache;

    const auto key = Paths::key (entry.file);
    const auto modified = entry.modified;      // As scanned: the file isn't asked again.

    {
        const std::lock_guard<std::mutex> guard (lock);

        if (const auto found = cache.find (key); found != cache.end() && found->second.modified == modified)
            return found->second.result;
    }

    const auto result = oldestOf (MetadataReader::readDates (entry.file), entry);

    const std::lock_guard<std::mutex> guard (lock);
    cache[key] = { modified, result };
    return result;
}

void ContentDate::fill (std::vector<FileEntry>& entries, const ScanContext& context, const MetadataSource& source)
{
    for (size_t i = 0; i < entries.size() && ! context.isCancelled(); ++i)
    {
        auto& entry = entries[i];

        if (entry.isDirectory || entry.contentCreated.toMilliseconds() != 0)
            continue;

        context.progress ((double) i / (double) entries.size(), "Reading dates of " + entry.name() + "...");
        entry.contentCreated = of (entry, source);
    }
}
