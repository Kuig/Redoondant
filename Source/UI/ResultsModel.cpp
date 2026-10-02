#include "ResultsModel.h"
#include "../Core/Format.h"
#include <limits>
#include <map>
#include <set>

void ResultsModel::setResult (AnalysisResult newResult, const juce::File& scannedRoot, bool isGrouped)
{
    result = std::move (newResult);
    root = scannedRoot;

    for (size_t i = 0; i < result.groups.size(); ++i)
        result.groups[i].order = (int) i;

    for (auto& group : result.groups)
        for (auto& item : group.items)
            item.selectedByDefault = item.selected;

    grouped = isGrouped;
    analysed = true;
    applySort();
    sortGroups();
    rebuildRows();
}

void ResultsModel::setFilter (Filter newFilter)
{
    filter = std::move (newFilter);
    rebuildRows();
}

void ResultsModel::sort (Column column, bool forwards)
{
    sortColumn = column;
    sortForwards = forwards;
    applySort();
    rebuildRows();
}

void ResultsModel::setGroupOrder (GroupOrder newOrder)
{
    groupOrder = newOrder;
    sortGroups();
    rebuildRows();
}

void ResultsModel::sortGroups()
{
    const auto total = [] (const ResultGroup& g)
    {
        juce::int64 bytes = 0;

        for (const auto& item : g.items)
            bytes += item.size;

        return bytes;
    };

    const auto newest = [] (const ResultGroup& g)
    {
        juce::int64 latest = 0;

        for (const auto& item : g.items)
            latest = juce::jmax (latest, item.modified.toMilliseconds());

        return latest;
    };

    const auto oldest = [] (const ResultGroup& g)
    {
        juce::int64 earliest = std::numeric_limits<juce::int64>::max();

        for (const auto& item : g.items)
            earliest = juce::jmin (earliest, item.modified.toMilliseconds());

        return earliest;
    };

    std::stable_sort (result.groups.begin(), result.groups.end(), [&] (const ResultGroup& a, const ResultGroup& b)
    {
        switch (groupOrder)
        {
            case GroupOrder::name:      { const int c = a.title.compareNatural (b.title); if (c != 0) return c < 0; break; }
            case GroupOrder::mostItems: if (a.items.size() != b.items.size()) return a.items.size() > b.items.size(); break;
            case GroupOrder::largest:   if (total (a) != total (b)) return total (a) > total (b); break;
            case GroupOrder::newest:    if (newest (a) != newest (b)) return newest (a) > newest (b); break;
            case GroupOrder::oldest:    if (oldest (a) != oldest (b)) return oldest (a) < oldest (b); break;
            case GroupOrder::analysis:  break;
        }

        return a.order < b.order;
    });
}

const ResultsModel::Row* ResultsModel::getRow (int index) const
{
    return juce::isPositiveAndBelow (index, (int) rows.size()) ? &rows[(size_t) index] : nullptr;
}

const ResultGroup* ResultsModel::getGroup (int row) const
{
    const auto* r = getRow (row);
    return r != nullptr ? &result.groups[(size_t) r->group] : nullptr;
}

FileEntry* ResultsModel::getEntry (int row)
{
    const auto* r = getRow (row);
    return r != nullptr && ! r->isHeader() ? &result.groups[(size_t) r->group].items[(size_t) r->item] : nullptr;
}

juce::String ResultsModel::getCellText (const FileEntry& entry, Column column, const ResultGroup* group) const
{
    switch (column)
    {
        case Column::name:
            if (root == juce::File() && group != nullptr && group->title.isNotEmpty())
                return entry.file.getRelativePathFrom (juce::File (group->title)).replace ("\\", "/");

            return entry.name();
        case Column::folder:
        {
            const auto parent = entry.file.getParentDirectory();

            if (root == juce::File())       // No analysed folder: show where the item is.
                return parent.getFullPathName();

            return parent == root ? juce::String() : parent.getRelativePathFrom (root);
        }
        case Column::size:      return entry.missing ? juce::String ("-") : Format::size (entry.size);
        case Column::modified:  return Format::date (entry.modified);
        case Column::created:   return Format::date (entry.created);
        case Column::contentCreated:    return Format::date (entry.contentCreated);
        case Column::type:      return entry.isDirectory ? juce::String ("Folder")
                                                         : entry.file.getFileExtension().fromFirstOccurrenceOf (".", false, false).toUpperCase();
        case Column::check:     break;
    }

    return {};
}

CheckState ResultsModel::getGroupState (int group) const
{
    size_t total = 0, checked = 0;
    forEachVisible (group, [&] (const FileEntry& e)
    {
        if (! e.missing)    // Missing entries can't be checked: they don't count.
        {
            ++total;
            checked += e.selected ? 1 : 0;
        }
    });

    return checked == 0 ? CheckState::none : (checked == total ? CheckState::all : CheckState::some);
}

void ResultsModel::toggle (int row)
{
    const auto* r = getRow (row);

    if (r == nullptr)
        return;

    if (! r->isHeader())
    {
        auto& entry = result.groups[(size_t) r->group].items[(size_t) r->item];
        entry.selected = ! entry.selected && ! entry.missing;
        return;
    }

    cycleGroup (r->group);
}

void ResultsModel::cycleGroup (int group)
{
    auto& g = result.groups[(size_t) group];
    g.phase = nextChecks (g.phase);

    for (auto& item : g.items)
        if (isVisible (item))
            item.selected = ! item.missing && (g.phase == Checks::all || (g.phase == Checks::defaults && item.selectedByDefault));
}

CheckState ResultsModel::getOverallState() const
{
    size_t total = 0, checked = 0;

    for (int g = 0; g < (int) result.groups.size(); ++g)
        forEachVisible (g, [&] (const FileEntry& e)
        {
            if (! e.missing)
            {
                ++total;
                checked += e.selected ? 1 : 0;
            }
        });

    return checked == 0 ? CheckState::none : (checked == total ? CheckState::all : CheckState::some);
}

size_t ResultsModel::getVisibleCount() const
{
    size_t count = 0;

    for (int g = 0; g < (int) result.groups.size(); ++g)
        forEachVisible (g, [&] (const FileEntry&) { ++count; });

    return count;
}

std::vector<FileEntry> ResultsModel::getCheckedEntries() const
{
    std::vector<FileEntry> checked;

    for (int g = 0; g < (int) result.groups.size(); ++g)
        forEachVisible (g, [&] (const FileEntry& e) { if (e.selected) checked.push_back (e); });

    return checked;
}

std::vector<juce::File> ResultsModel::filesNeedingContentDate() const
{
    std::vector<juce::File> files;

    for (const auto& group : result.groups)
        for (const auto& item : group.items)
            if (! item.isDirectory && ! item.missing && item.contentCreated.toMilliseconds() == 0)
                files.push_back (item.file);

    return files;
}

void ResultsModel::setContentDates (const std::vector<std::pair<juce::File, juce::Time>>& dates)
{
    std::map<juce::String, juce::Time> byPath;

    for (const auto& [file, time] : dates)
        byPath[file.getFullPathName()] = time;

    for (auto& group : result.groups)
        for (auto& item : group.items)
            if (const auto found = byPath.find (item.file.getFullPathName()); found != byPath.end())
                item.contentCreated = found->second;

    if (sortColumn == Column::contentCreated)
    {
        applySort();
        rebuildRows();
    }
}

std::vector<FileEntry> ResultsModel::getAllEntries() const
{
    std::vector<FileEntry> all;

    for (const auto& group : result.groups)
        all.insert (all.end(), group.items.begin(), group.items.end());

    return all;
}

void ResultsModel::applyChecks (Checks checks)
{
    for (auto& group : result.groups)
    {
        group.phase = checks;

        for (auto& item : group.items)
            if (isVisible (item))
                item.selected = ! item.missing && (checks == Checks::all || (checks == Checks::defaults && item.selectedByDefault));
    }
}

void ResultsModel::checkWhere (const std::function<bool (const FileEntry&)>& shouldCheck)
{
    for (auto& group : result.groups)
        for (auto& item : group.items)
            item.selected = ! item.missing && shouldCheck (item);
}

void ResultsModel::remove (const juce::Array<juce::File>& files)
{
    std::set<juce::String> removed;

    for (const auto& f : files)
        removed.insert (f.getFullPathName());

    const auto isRemoved = [&] (const FileEntry& e)
    {
        // Also drop entries living inside a removed folder.
        for (auto f = e.file; f != f.getParentDirectory(); f = f.getParentDirectory())
            if (removed.count (f.getFullPathName()) > 0)
                return true;

        return false;
    };

    std::vector<ResultGroup> kept;

    for (auto& group : result.groups)
    {
        const auto before = group.items.size();
        group.items.erase (std::remove_if (group.items.begin(), group.items.end(), isRemoved), group.items.end());

        const bool broken = grouped && group.items.size() < 2 && group.items.size() != before;

        if (! group.items.empty() && ! broken)
            kept.push_back (std::move (group));
    }

    result.groups = std::move (kept);
    rebuildRows();
}

void ResultsModel::applySort()
{
    if (sortColumn == Column::check)
        return;

    const ResultGroup* currentGroup = nullptr;

    const auto compare = [this, &currentGroup] (const FileEntry& a, const FileEntry& b) -> int
    {
        switch (sortColumn)
        {
            case Column::size:      return a.size < b.size ? -1 : (a.size > b.size ? 1 : 0);
            case Column::modified:  return a.modified < b.modified ? -1 : (a.modified > b.modified ? 1 : 0);
            case Column::created:   return a.created < b.created ? -1 : (a.created > b.created ? 1 : 0);
            case Column::contentCreated:    return a.contentCreated < b.contentCreated ? -1 : (a.contentCreated > b.contentCreated ? 1 : 0);
            case Column::name:
            case Column::folder:
            case Column::type:
            case Column::check:     break;
        }

        return getCellText (a, sortColumn, currentGroup).compareNatural (getCellText (b, sortColumn, currentGroup));
    };

    for (auto& group : result.groups)
    {
        currentGroup = &group;
        std::stable_sort (group.items.begin(), group.items.end(), [&] (const FileEntry& a, const FileEntry& b)
        {
            const int c = compare (a, b);
            return sortForwards ? c < 0 : c > 0;
        });
    }
}

void ResultsModel::rebuildRows()
{
    rows.clear();

    for (int g = 0; g < (int) result.groups.size(); ++g)
    {
        const auto& items = result.groups[(size_t) g].items;
        const auto headerIndex = rows.size();

        if (grouped)
            rows.push_back ({ g, -1 });

        for (int i = 0; i < (int) items.size(); ++i)
            if (isVisible (items[(size_t) i]))
                rows.push_back ({ g, i });

        if (grouped && rows.size() == headerIndex + 1)
            rows.pop_back();    // No visible items: hide the header too.
    }
}
