#include "Grouping.h"

int Grouping::sharedPrefixLength (const juce::String& a, const juce::String& b)
{
    auto pa = a.getCharPointer();
    auto pb = b.getCharPointer();
    int length = 0;

    while (! pa.isEmpty() && ! pb.isEmpty()
           && juce::CharacterFunctions::toLowerCase (*pa) == juce::CharacterFunctions::toLowerCase (*pb))
    {
        ++pa;
        ++pb;
        ++length;
    }

    return length;
}

std::vector<Grouping::RootCluster> Grouping::clusterBySharedRoot (std::vector<FileEntry> items,
                                                                   int minRootLength,
                                                                   double minRootRatio)
{
    const auto stemOf = [] (const FileEntry& e) { return e.file.getFileNameWithoutExtension(); };

    std::sort (items.begin(), items.end(), [&] (const FileEntry& a, const FileEntry& b)
    {
        return stemOf (a).compareIgnoreCase (stemOf (b)) < 0;
    });

    std::vector<RootCluster> clusters;
    int shortestInCluster = 0;

    const auto flush = [&] (RootCluster& cluster)
    {
        if (cluster.items.size() >= 2)
        {
            cluster.root = cluster.root.trimCharactersAtEnd (" -_.([{");
            clusters.push_back (std::move (cluster));
        }
    };

    RootCluster current;

    for (auto& item : items)
    {
        const auto stem = stemOf (item);

        if (! current.items.empty())
        {
            const int shared = sharedPrefixLength (current.root, stem);
            const int shortest = juce::jmin (shortestInCluster, stem.length());

            if (shared >= minRootLength && shared >= minRootRatio * shortest)
            {
                current.root = current.root.substring (0, shared);
                shortestInCluster = shortest;
                current.items.push_back (std::move (item));
                continue;
            }

            flush (current);
            current = {};
        }

        current.root = stem;
        shortestInCluster = stem.length();
        current.items.push_back (std::move (item));
    }

    flush (current);
    return clusters;
}

Grouping::Groups Grouping::clusterByTimeGap (std::vector<FileEntry> items,
                                             std::function<juce::Time (const FileEntry&)> timeOf,
                                             juce::RelativeTime maxGap,
                                             size_t minSize)
{
    std::sort (items.begin(), items.end(), [&] (const FileEntry& a, const FileEntry& b) { return timeOf (a) < timeOf (b); });

    Groups groups;
    std::vector<FileEntry> current;

    const auto flush = [&]
    {
        if (current.size() >= minSize)
            groups.push_back (std::move (current));

        current.clear();
    };

    for (auto& item : items)
    {
        if (! current.empty() && timeOf (item) - timeOf (current.back()) > maxGap)
            flush();

        current.push_back (std::move (item));
    }

    flush();
    return groups;
}
