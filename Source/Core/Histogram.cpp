#include "Histogram.h"
#include "FileCategory.h"
#include <map>

namespace
{
    constexpr int maxFolders = 10;
    constexpr double bytesPerMB = 1024.0 * 1024.0;
}

const juce::StringArray& Histogram::propertyNames()
{
    static const juce::StringArray names { "Folder", "Size", "Modified", "Created", "Content created", "Type" };
    return names;
}

std::optional<DateKind> Histogram::dateKindOf (Property property)
{
    switch (property)
    {
        case Property::modified:        return DateKind::modified;
        case Property::created:         return DateKind::created;
        case Property::contentCreated:  return DateKind::content;
        default:                        return std::nullopt;
    }
}

const juce::StringArray& Histogram::Buckets::dateNames()
{
    static const juce::StringArray names { "Today", "1-7 days ago", "7-30 days ago", "30 days - 1 year ago", "1-2 years ago",
                                           "More than 2 years ago", "Unknown date" };
    return names;
}

int Histogram::Buckets::dateBucket (juce::Time time, juce::Time now)
{
    if (time.toMilliseconds() <= 0)
        return unknownDate;

    const double days = (now - time).inDays();

    if (days < 1)       return 0;
    if (days < 7)       return 1;
    if (days < 30)      return 2;
    if (days < 365)     return 3;
    if (days < 730)     return 4;
    return 5;
}

const juce::StringArray& Histogram::Buckets::sizeNames()
{
    static const juce::StringArray names { "Under 1 MB", "1-10 MB", "10-100 MB", "100 MB - 1 GB", "1 GB or more" };
    return names;
}

int Histogram::Buckets::sizeBucket (juce::int64 bytes)
{
    const double mb = (double) bytes / bytesPerMB;
    return mb < 1 ? 0 : (mb < 10 ? 1 : (mb < 100 ? 2 : (mb < 1024 ? 3 : 4)));
}

Histogram::Buckets::SizeRange Histogram::Buckets::sizeRange (int bucket)
{
    // The upper limits are one byte short of the next bucket, since the filter includes its limit.
    constexpr double oneByte = 1.0 / bytesPerMB;

    switch (bucket)
    {
        case 0:     return { 0, 1 - oneByte };
        case 1:     return { 1, 10 - oneByte };
        case 2:     return { 10, 100 - oneByte };
        case 3:     return { 100, 1024 - oneByte };
        default:    return { 1024, 0 };
    }
}

juce::String Histogram::folderOf (const FileEntry& entry, const juce::File& root)
{
    const auto parent = entry.file.getParentDirectory();

    if (root == juce::File())
        return parent.getFullPathName();

    return parent == root ? juce::String() : parent.getRelativePathFrom (root);
}

void Histogram::Stats::add (const FileEntry& entry)
{
    ++count;
    bytes += entry.size;

    if (entry.size > largestSize)
    {
        largestSize = entry.size;
        largestName = entry.name();
    }

    if (entry.modified.toMilliseconds() > 0)
    {
        if (oldest.toMilliseconds() == 0 || entry.modified < oldest)    oldest = entry.modified;
        if (newest.toMilliseconds() == 0 || entry.modified > newest)    newest = entry.modified;
    }
}

std::vector<Histogram::Bin> Histogram::compute (const Input& input, Property property, Metric metric, const juce::File& root, juce::Time now)
{
    std::vector<Bin> bins;
    std::function<int (const FileEntry&)> binOf;       // Index of the bar of an entry; out of range: not shown.

    const auto addBin = [&bins] (const juce::String& label, int key, bool filterable = true, const juce::String& folder = {})
    {
        Bin bin;
        bin.label = label;
        bin.key = key;
        bin.folder = folder;
        bin.filterable = filterable;
        bins.push_back (bin);
    };

    switch (property)
    {
        case Property::size:
            for (int i = 0; i < Buckets::sizeNames().size(); ++i)
                addBin (Buckets::sizeNames()[i], i);

            binOf = [] (const FileEntry& e) { return Buckets::sizeBucket (e.size); };
            break;

        case Property::modified:
        case Property::created:
        case Property::contentCreated:
        {
            const auto kind = *dateKindOf (property);

            for (int i = 0; i < Buckets::dateNames().size(); ++i)
                addBin (Buckets::dateNames()[i], i);

            binOf = [kind, now] (const FileEntry& e) { return Buckets::dateBucket (dateOf (e, kind), now); };
            break;
        }

        case Property::type:
            for (size_t i = 0; i < FileCategories::all().size(); ++i)
                addBin (FileCategories::nameOf (FileCategories::all()[i]), (int) i);

            binOf = [] (const FileEntry& e)
            {
                const auto& all = FileCategories::all();
                return (int) (std::find (all.begin(), all.end(), FileCategories::of (e)) - all.begin());
            };
            break;

        case Property::folder:
        {
            std::map<juce::String, Stats> byFolder;

            for (const auto& e : input.all)
                byFolder[folderOf (e, root)].add (e);

            std::vector<std::pair<juce::String, Stats>> ranked (byFolder.begin(), byFolder.end());
            std::stable_sort (ranked.begin(), ranked.end(), [metric] (const auto& a, const auto& b)
            {
                return valueOf (a.second, metric) > valueOf (b.second, metric);
            });

            std::map<juce::String, int> index;

            for (size_t i = 0; i < ranked.size() && i < (size_t) maxFolders; ++i)
            {
                const auto& folder = ranked[i].first;
                addBin (folder.isEmpty() ? juce::String ("(root)") : folder, (int) i, folder.isNotEmpty(), folder);   // The root has no text for the Folder filter.
                index[folder] = (int) i;
            }

            const bool others = ranked.size() > (size_t) maxFolders;

            if (others)
                addBin ("Others", maxFolders, false);

            binOf = [root, index = std::move (index), others] (const FileEntry& e)
            {
                const auto found = index.find (folderOf (e, root));
                return found != index.end() ? found->second : (others ? maxFolders : -1);
            };
            break;
        }
    }

    const auto fill = [&] (const std::vector<FileEntry>& entries, Stats Bin::* layer)
    {
        for (const auto& e : entries)
        {
            const int bin = binOf (e);

            if (juce::isPositiveAndBelow (bin, (int) bins.size()))
                (bins[(size_t) bin].*layer).add (e);
        }
    };

    fill (input.all, &Bin::total);
    fill (input.visible, &Bin::filtered);
    fill (input.checked, &Bin::checked);

    // Types and the unknown date are only listed when something is in them; the folders are already limited.
    bins.erase (std::remove_if (bins.begin(), bins.end(), [property] (const Bin& b)
    {
        const bool optional = property == Property::type || (dateKindOf (property) && b.key == Buckets::unknownDate);
        return optional && b.total.count == 0;
    }), bins.end());

    return bins;
}
