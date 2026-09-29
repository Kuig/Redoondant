#include "EmptyFolders.h"
#include "FileScanner.h"
#include <map>

namespace
{
    // Only folders documented as pure, regenerable temporary data: no profiles, cookies or settings.
    // Teams classic: the cache folders of the usual clearing procedure (not "backgrounds", which holds the user's own images).
    const char* const teamsCacheFolders[] = { "Application Cache\\Cache", "blob_storage", "Cache", "databases", "GPUCache", "IndexedDB",
                                              "Local Storage", "Service Worker\\CacheStorage", "tmp" };
    const char* const chromiumCacheFolders[] = { "Cache", "Code Cache", "GPUCache" };
    const char* const codeCacheFolders[] = { "Cache", "CachedData", "Code Cache", "GPUCache" };

    const char* const variables[] = { "LOCALAPPDATA", "APPDATA", "PROGRAMDATA", "USERPROFILE", "WINDIR" };

    juce::String variable (const juce::String& name)    { return juce::SystemStats::getEnvironmentVariable (name, {}); }

    bool isRoot (const juce::File& f)                   { return f == f.getParentDirectory(); }

    std::vector<juce::File> protectedFolders()
    {
        std::vector<juce::File> folders;

        for (const char* name : { "USERPROFILE", "APPDATA", "LOCALAPPDATA", "WINDIR", "PROGRAMDATA", "ProgramFiles", "ProgramFiles(x86)" })
            if (const auto value = variable (name); value.isNotEmpty())
                folders.emplace_back (value);

        folders.push_back (juce::File::getSpecialLocation (juce::File::userHomeDirectory).getParentDirectory());    // C:\Users
        return folders;
    }
}

const std::vector<EmptyFolders::Default>& EmptyFolders::defaults()
{
    static const auto list = []
    {
        std::vector<Default> d;
        const auto add = [&d] (const juce::String& path, bool checked = true) { d.push_back ({ path, checked }); };

        // Adobe (Premiere media cache)
        add ("%APPDATA%\\Adobe\\Common\\Media Cache Files");
        add ("%APPDATA%\\Adobe\\Common\\Media Cache");
        add ("%APPDATA%\\Adobe\\Common\\Peak Files");
        add ("%LOCALAPPDATA%\\Adobe\\CameraRaw\\Cache");

        // Microsoft Teams (classic; and new, whose documented cache folder also resets theme and language: not checked at first)
        for (const auto* folder : teamsCacheFolders)
            add ("%APPDATA%\\Microsoft\\Teams\\" + juce::String (folder));

        add ("%LOCALAPPDATA%\\Packages\\MSTeams_8wekyb3d8bbwe\\LocalCache\\Microsoft\\MSTeams", false);

        // Windows temporary files and reports
        add ("%TEMP%");
        add ("%WINDIR%\\Temp");
        add ("%LOCALAPPDATA%\\CrashDumps");
        add ("%LOCALAPPDATA%\\Microsoft\\Windows\\INetCache");
        add ("%PROGRAMDATA%\\Microsoft\\Windows\\WER\\ReportArchive");
        add ("%PROGRAMDATA%\\Microsoft\\Windows\\WER\\ReportQueue");

        // Graphics shader caches (rebuilt on demand)
        add ("%LOCALAPPDATA%\\D3DSCache");
        add ("%LOCALAPPDATA%\\NVIDIA\\DXCache");
        add ("%LOCALAPPDATA%\\NVIDIA\\GLCache");
        add ("%LOCALAPPDATA%\\AMD\\DxCache");

        // Big caches that make the next start slower: listed, but not checked at first
        for (const auto* browser : { "Google\\Chrome", "Microsoft\\Edge" })
            for (const auto* folder : chromiumCacheFolders)
                add ("%LOCALAPPDATA%\\" + juce::String (browser) + "\\User Data\\Default\\" + folder, false);

        for (const auto* folder : codeCacheFolders)
            add ("%APPDATA%\\Code\\" + juce::String (folder), false);

        add ("%LOCALAPPDATA%\\pip\\Cache", false);
        add ("%LOCALAPPDATA%\\npm-cache", false);
        add ("%LOCALAPPDATA%\\NuGet\\v3-cache", false);
        return d;
    }();

    return list;
}

juce::String EmptyFolders::expand (const juce::String& path)
{
    juce::String result;

    for (int i = 0; i < path.length();)
    {
        const auto end = path[i] == '%' ? path.indexOfChar (i + 1, '%') : -1;

        if (end > i + 1)
        {
            const auto value = variable (path.substring (i + 1, end));

            if (value.isNotEmpty())
            {
                result << value;
                i = end + 1;
                continue;
            }
        }

        result << path[i++];
    }

    return result;
}

juce::String EmptyFolders::contract (const juce::String& path)
{
    const juce::File file (path);
    juce::String best;
    int bestLength = 0;

    for (const auto* name : variables)
    {
        const auto value = variable (name);

        if (value.isNotEmpty() && (file == juce::File (value) || file.isAChildOf (juce::File (value))) && value.length() > bestLength)
        {
            best = "%" + juce::String (name) + "%" + file.getFullPathName().substring (juce::File (value).getFullPathName().length());
            bestLength = value.length();
        }
    }

    return best.isNotEmpty() ? best : path;
}

bool EmptyFolders::isTooBroad (const juce::File& folder)
{
    if (isRoot (folder))
        return true;

    for (const auto& p : protectedFolders())
        if (p == folder || p.isAChildOf (folder))
            return true;

    return false;
}

std::vector<EmptyFolders::Group> EmptyFolders::groupByCommonParent (const std::vector<juce::File>& folders)
{
    std::map<juce::String, Group> groups;    // Keyed by the lower-case path: Windows paths are case-insensitive.

    for (size_t i = 0; i < folders.size(); ++i)
    {
        auto parent = folders[i].getParentDirectory();

        for (auto candidate = parent; ! isRoot (candidate); candidate = candidate.getParentDirectory())
        {
            const bool sharedWithAnother = [&]
            {
                for (size_t j = 0; j < folders.size(); ++j)
                    if (j != i && folders[j].isAChildOf (candidate))
                        return true;

                return false;
            }();

            if (sharedWithAnother)
            {
                parent = candidate;
                break;
            }
        }

        auto& group = groups[parent.getFullPathName().toLowerCase()];
        group.parent = parent;
        group.members.push_back (i);
    }

    std::vector<Group> result;

    for (auto& [key, group] : groups)
    {
        std::sort (group.members.begin(), group.members.end(), [&] (size_t a, size_t b)
        {
            return folders[a].getFullPathName().compareNatural (folders[b].getFullPathName()) < 0;
        });

        result.push_back (std::move (group));
    }

    std::sort (result.begin(), result.end(), [] (const Group& a, const Group& b)
    {
        return a.parent.getFullPathName().compareNatural (b.parent.getFullPathName()) < 0;
    });

    return result;
}

std::vector<FileEntry> EmptyFolders::contentsOf (const juce::File& folder)
{
    ScanContext context;
    context.root = folder;
    context.recursive = false;
    return FileScanner::scan (context);
}
