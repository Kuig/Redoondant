#include "ResultsModel.h"
#include "../Core/Format.h"
#include "../Core/Histogram.h"
#include <limits>
#include <map>
#include <set>

void ResultsModel::setResult (AnalysisResult newResult, const juce::File& scannedRoot, bool isGrouped,
                              const std::function<void (ResultItem&)>& initialise)
{
    result = {};
    root = scannedRoot;

    for (auto& found : newResult.groups)
    {
        ListedGroup group;
        group.title = std::move (found.title);
        group.order = (int) result.groups.size();

        for (auto& entry : found.items)
        {
            ResultItem item (std::move (entry));

            if (initialise != nullptr)
                initialise (item);

            item.selectedByDefault = item.selected;
            group.items.push_back (std::move (item));
        }

        result.groups.push_back (std::move (group));
    }

    grouped = isGrouped;
    analysed = true;
    applySort();
    sortGroups();

    if (defaultSelection)
    {
        DefaultSelections::apply (*defaultSelection, result.groups, [this] (const FileEntry& e) { return isVisible (e); });

        for (auto& group : result.groups)
            for (auto& item : group.items)
                item.selected = item.selectedByDefault;
    }

    rebuildRows();
}

void ResultsModel::setDefaultSelection (DefaultSelection newSelection)
{
    defaultSelection = newSelection;
    updateDefaults();
}

void ResultsModel::updateDefaults()
{
    if (! defaultSelection)
        return;

    DefaultSelections::apply (*defaultSelection, result.groups, [this] (const FileEntry& e) { return isVisible (e); });

    for (auto& group : result.groups)
        if (group.phase == BulkCheck::defaults)
            for (auto& item : group.items)
                if (isVisible (item))
                    item.selected = item.selectedByDefault;
}

void ResultsModel::clear()
{
    result = {};
    root = {};
    analysed = false;
    rows.clear();
}

void ResultsModel::setFilter (Filter newFilter, bool usesContentDate)
{
    filter = std::move (newFilter);
    filterUsesContentDate = usesContentDate;
    updateViewDependentDefaults();
    rebuildRows();
}

void ResultsModel::sort (Column column, bool forwards)
{
    sortColumn = column;
    sortForwards = forwards;
    applySort();
    updateViewDependentDefaults();
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
    std::stable_sort (result.groups.begin(), result.groups.end(), [&] (const ListedGroup& a, const ListedGroup& b)
    {
        switch (groupOrder)
        {
            case GroupOrder::name:      { const int c = a.title.compareNatural (b.title); if (c != 0) return c < 0; break; }
            case GroupOrder::mostItems: if (a.items.size() != b.items.size()) return a.items.size() > b.items.size(); break;
            case GroupOrder::largest:   if (a.totalSize() != b.totalSize()) return a.totalSize() > b.totalSize(); break;
            case GroupOrder::newest:    if (a.newestModified() != b.newestModified()) return a.newestModified() > b.newestModified(); break;
            case GroupOrder::oldest:    if (a.oldestModified() != b.oldestModified()) return a.oldestModified() < b.oldestModified(); break;
            case GroupOrder::analysis:  break;
        }

        return a.order < b.order;
    });
}

const ResultsModel::Row* ResultsModel::getRow (int index) const
{
    return juce::isPositiveAndBelow (index, (int) rows.size()) ? &rows[(size_t) index] : nullptr;
}

const ListedGroup* ResultsModel::getGroup (int row) const
{
    const auto* r = getRow (row);
    return r != nullptr ? &result.groups[(size_t) r->group] : nullptr;
}

ResultItem* ResultsModel::getEntry (int row)
{
    const auto* r = getRow (row);
    return r != nullptr && ! r->isHeader() ? &result.groups[(size_t) r->group].items[(size_t) r->item] : nullptr;
}

juce::String ResultsModel::getCellText (const ResultItem& entry, Column column, const ListedGroup* group) const
{
    switch (column)
    {
        case Column::name:
            if (root == juce::File() && group != nullptr && group->title.isNotEmpty())
                return entry.file.getRelativePathFrom (juce::File (group->title)).replace ("\\", "/");

            return entry.name();
        case Column::folder:
        {
            return Histogram::folderOf (entry, root);       // No analysed folder: where the item is.
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

namespace
{
    /** What the entries of a list show as one check box: the visible ones that exist can be checked, missing ones don't count. */
    struct CheckTally
    {
        size_t total = 0, checked = 0;

        void add (const ResultItem& e)
        {
            if (! e.missing)
            {
                ++total;
                checked += e.selected ? 1 : 0;
            }
        }

        CheckState state() const    { return checked == 0 ? CheckState::none : (checked == total ? CheckState::all : CheckState::some); }
    };

    /** Applies a bulk check state to an entry: all, none, or what the criterion proposed. */
    void applyBulkCheck (ResultItem& item, BulkCheck phase)
    {
        item.selected = ! item.missing && (phase == BulkCheck::all || (phase == BulkCheck::defaults && item.selectedByDefault));
    }
}

CheckState ResultsModel::getGroupState (int group) const
{
    CheckTally tally;
    forEachVisible (group, [&] (const ResultItem& e) { tally.add (e); });
    return tally.state();
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
    g.phase = nextBulkCheck (g.phase);

    for (auto& item : g.items)
        if (isVisible (item))
            applyBulkCheck (item, g.phase);
}

CheckState ResultsModel::getOverallState() const
{
    CheckTally tally;

    for (int g = 0; g < (int) result.groups.size(); ++g)
        forEachVisible (g, [&] (const ResultItem& e) { tally.add (e); });

    return tally.state();
}

int ResultsModel::countGroupsFullyChecked() const
{
    int count = 0;

    for (const auto& group : result.groups)
        if (group.items.size() >= 2 && std::all_of (group.items.begin(), group.items.end(), [] (const ResultItem& e) { return e.selected; }))
            ++count;

    return count;
}

size_t ResultsModel::getVisibleCount() const
{
    size_t count = 0;

    for (int g = 0; g < (int) result.groups.size(); ++g)
        forEachVisible (g, [&] (const ResultItem&) { ++count; });

    return count;
}

std::vector<FileEntry> ResultsModel::getCheckedEntries() const
{
    std::vector<FileEntry> checked;

    for (int g = 0; g < (int) result.groups.size(); ++g)
        forEachVisible (g, [&] (const ResultItem& e) { if (e.selected) checked.push_back (e); });

    return checked;
}

std::vector<FileEntry> ResultsModel::getVisibleEntries() const
{
    std::vector<FileEntry> visible;

    for (int g = 0; g < (int) result.groups.size(); ++g)
        forEachVisible (g, [&] (const ResultItem& e) { if (! e.missing) visible.push_back (e); });

    return visible;
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

    if (sortColumn == Column::contentCreated || filterUsesContentDate)     // The new dates can change the order and what the filter lets through.
    {
        applySort();
        updateViewDependentDefaults();
        rebuildRows();
    }
}

std::vector<FileEntry> ResultsModel::getAllEntries() const
{
    std::vector<FileEntry> all;

    for (const auto& group : result.groups)
        for (const auto& item : group.items)
            if (! item.missing)
                all.push_back (item);

    return all;
}

void ResultsModel::applyChecks (BulkCheck checks)
{
    for (auto& group : result.groups)
    {
        group.phase = checks;

        for (auto& item : group.items)
            if (isVisible (item))
                applyBulkCheck (item, checks);
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

    std::vector<ListedGroup> kept;

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

    const ListedGroup* currentGroup = nullptr;

    const auto compare = [this, &currentGroup] (const ResultItem& a, const ResultItem& b) -> int
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
        std::stable_sort (group.items.begin(), group.items.end(), [&] (const ResultItem& a, const ResultItem& b)
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
