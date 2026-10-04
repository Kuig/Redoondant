#include "IconCache.h"
#include "../Platform/ComInit.h"
#include "../Platform/ShellIcon.h"
#include <map>
#include <mutex>
#include <set>

namespace
{
    constexpr size_t maxCachedIcons = 2000;
}

struct IconCache::Shared
{
    std::mutex lock;
    std::map<juce::String, juce::Image> icons;
    std::set<juce::String> requested;
    std::set<IconCache*> caches;        // The live ones: told when an icon arrives (the table that asked first may not be the only one waiting).
};

std::shared_ptr<IconCache::Shared> IconCache::sharedIcons()
{
    static const auto instance = std::make_shared<Shared>();
    return instance;
}

IconCache::IconCache() : shared (sharedIcons())
{
    const std::lock_guard<std::mutex> guard (shared->lock);
    shared->caches.insert (this);
}

IconCache::~IconCache()
{
    {
        const std::lock_guard<std::mutex> guard (shared->lock);
        shared->caches.erase (this);    // Late jobs no longer call back here.
    }

    loader.removeAllJobs (true, 5000);
}

bool IconCache::hasOwnIcon (const juce::File& file)
{
    static const juce::StringArray extensions { ".exe", ".ico", ".lnk", ".scr", ".cur", ".msi", ".appx", ".msix", ".url" };
    return extensions.contains (file.getFileExtension(), true);
}

juce::String IconCache::keyFor (const FileEntry& entry)
{
    if (entry.isDirectory)
        return "<folder>";

    return hasOwnIcon (entry.file) ? entry.file.getFullPathName().toLowerCase()
                                   : "." + entry.file.getFileExtension().fromFirstOccurrenceOf (".", false, false).toLowerCase();
}

juce::Image IconCache::get (const FileEntry& entry)
{
    const auto key = keyFor (entry);

    {
        const std::lock_guard<std::mutex> guard (shared->lock);

        if (const auto found = shared->icons.find (key); found != shared->icons.end())
            return found->second;

        if (shared->requested.size() >= maxCachedIcons || ! shared->requested.insert (key).second)
            return {};      // Already on its way, or the cache is full (then nothing is recorded, so nothing waits forever).
    }

    loader.addJob ([state = shared, file = entry.file, key]
    {
        const ScopedComInit com;
        auto icon = ShellIcon::get (file, iconSize * 2);    // Oversized: scaled down by the painter, sharp on high-DPI screens.

        {
            const std::lock_guard<std::mutex> guard (state->lock);
            state->icons[key] = icon.isValid() ? icon : juce::Image();
        }

        juce::MessageManager::callAsync ([state]
        {
            // Caches are created and destroyed on the message thread, so the live ones stay alive during these calls.
            std::set<IconCache*> caches;

            {
                const std::lock_guard<std::mutex> guard (state->lock);
                caches = state->caches;
            }

            for (auto* cache : caches)
                if (cache->onIconLoaded != nullptr)
                    cache->onIconLoaded();
        });
    });

    return {};
}
