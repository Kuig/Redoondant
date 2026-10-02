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
    bool alive = true;      // Cleared by the destructor so late jobs don't call back.
};

IconCache::IconCache() : shared (std::make_shared<Shared>()) {}

IconCache::~IconCache()
{
    {
        const std::lock_guard<std::mutex> guard (shared->lock);
        shared->alive = false;
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

        if (! shared->requested.insert (key).second || shared->requested.size() > maxCachedIcons)
            return {};
    }

    loader.addJob ([state = shared, file = entry.file, key, owner = this]
    {
        const ScopedComInit com;
        auto icon = ShellIcon::get (file, iconSize * 2);    // Oversized: scaled down by the painter, sharp on high-DPI screens.

        {
            const std::lock_guard<std::mutex> guard (state->lock);

            if (! state->alive)
                return;

            state->icons[key] = icon.isValid() ? icon : juce::Image();
        }

        juce::MessageManager::callAsync ([state, owner]
        {
            bool alive;

            {
                const std::lock_guard<std::mutex> guard (state->lock);
                alive = state->alive;
            }

            if (alive && owner->onIconLoaded != nullptr)
                owner->onIconLoaded();
        });
    });

    return {};
}
