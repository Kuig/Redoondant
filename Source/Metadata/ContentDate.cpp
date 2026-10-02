#include "ContentDate.h"
#include "MetadataReader.h"
#include <map>
#include <mutex>

juce::Time ContentDate::oldestOf (const Metadata& metadata, const juce::File& file)
{
    const auto isFileSystemDate = [&] (const MetadataItem& item, juce::Time time)
    {
        const auto near = [&] (juce::Time other) { return std::abs (time.toMilliseconds() - other.toMilliseconds()) < 2000; };
        return file != juce::File() && item.key.startsWith ("System.Document.Date")
                 && (near (file.getCreationTime()) || near (file.getLastModificationTime()));
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

juce::Time ContentDate::of (const juce::File& file)
{
    struct Cached { juce::Time modified, result; };
    static std::mutex lock;
    static std::map<juce::String, Cached> cache;

    const auto key = file.getFullPathName();
    const auto modified = file.getLastModificationTime();

    {
        const std::lock_guard<std::mutex> guard (lock);

        if (const auto found = cache.find (key); found != cache.end() && found->second.modified == modified)
            return found->second.result;
    }

    const auto result = oldestOf (MetadataReader::read (file), file);

    const std::lock_guard<std::mutex> guard (lock);
    cache[key] = { modified, result };
    return result;
}

void ContentDate::fill (std::vector<FileEntry>& entries, const ScanContext& context)
{
    for (size_t i = 0; i < entries.size() && ! context.isCancelled(); ++i)
    {
        auto& entry = entries[i];

        if (entry.isDirectory || entry.contentCreated.toMilliseconds() != 0)
            continue;

        context.progress ((double) i / (double) entries.size(), "Reading dates of " + entry.name() + "...");
        entry.contentCreated = of (entry.file);
    }
}
