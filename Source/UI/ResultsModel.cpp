#include "ResultsModel.h"
#include "../Core/Format.h"
#include "../Core/Histogram.h"
#include "../Core/Paths.h"
#include <limits>
#include <map>
#include <set>

namespace
{
    /** The date a date column shows; the other columns have none. */
    std::optional<DateKind> dateKindOf (Column column)
    {
        switch (column)
        {
            case Column::modified:          return DateKind::modified;
            case Column::created:           return DateKind::created;
            case Column::contentCreated:    return DateKind::content;
            default:                        return std::nullopt;
        }
    }
}

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
    refreshVisibility();
    applySort();
    sortGroups();
    updateDefaults();
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

    DefaultSelections::apply (*defaultSelection, result.groups);

    // Hidden items follow too (they are never acted on), so they show the current defaults when a filter reveals them.
    for (auto& group : result.groups)
        if (group.phase == BulkCheck::defaults)
            for (auto& item : group.items)
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
    refreshVisibility();
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
        case Column::modified:
        case Column::created:
        case Column::contentCreated:    return Format::date (dateOf (entry, *dateKindOf (column)));
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

void ResultsModel::refreshVisibility()
{
    for (auto& group : result.groups)
        for (auto& item : group.items)
            item.visible = filter == nullptr || filter (item);
}

size_t ResultsModel::getVisibleCount() const
{
    size_t count = 0;

    for (const auto& group : result.groups)
        count += (size_t) std::count_if (group.items.begin(), group.items.end(), [] (const ResultItem& e) { return e.visible; });

    return count;
}

std::vector<FileEntry> ResultsModel::getCheckedEntries() const
{
    return collect ([] (const ResultItem& e) { return e.visible && e.selected; });
}

std::vector<FileEntry> ResultsModel::getVisibleEntries() const
{
    return collect ([] (const ResultItem& e) { return e.visible && ! e.missing; });
}

Histogram::Input ResultsModel::getHistogramInput() const
{
    Histogram::Input input;
    input.root = root;

    for (const auto& group : result.groups)
        for (const auto& item : group.items)
        {
            if (item.missing)
                continue;

            input.all.push_back (item);

            if (item.visible)
            {
                input.visible.push_back (item);

                if (item.selected)
                    input.checked.push_back (item);
            }
        }

    return input;
}

std::vector<FileEntry> ResultsModel::filesNeedingContentDate() const
{
    return collect ([] (const ResultItem& e) { return ! e.isDirectory && ! e.missing && e.contentCreated.toMilliseconds() == 0; });
}

void ResultsModel::setContentDates (const std::vector<std::pair<juce::File, juce::Time>>& dates, bool refreshView)
{
    std::map<juce::String, juce::Time> byPath;

    for (const auto& [file, time] : dates)
        byPath[Paths::key (file)] = time;

    for (auto& group : result.groups)
        for (auto& item : group.items)
            if (const auto found = byPath.find (Paths::key (item.file)); found != byPath.end())
                item.contentCreated = found->second;

    if (refreshView && (sortColumn == Column::contentCreated || filterUsesContentDate))     // The new dates can change the order and what the filter lets through.
    {
        if (filterUsesContentDate)
            refreshVisibility();

        applySort();
        updateViewDependentDefaults();
        rebuildRows();
    }
}

std::vector<FileEntry> ResultsModel::getAllEntries() const
{
    return collect ([] (const ResultItem& e) { return ! e.missing; });
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
        removed.insert (Paths::key (f));

    // Also drop entries living inside a removed folder.
    const auto isRemoved = [&] (const FileEntry& e) { return removed.count (Paths::key (e.file)) > 0 || Paths::hasAncestorIn (e.file, removed); };

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

void ResultsModel::update (const std::vector<FileEntry>& entries)
{
    std::map<juce::String, const FileEntry*> byPath;

    for (const auto& entry : entries)
        byPath[Paths::key (entry.file)] = &entry;

    for (auto& group : result.groups)
        for (auto& item : group.items)
            if (const auto found = byPath.find (Paths::key (item.file)); found != byPath.end())
            {
                const auto contentCreated = item.contentCreated;
                static_cast<FileEntry&> (item) = *found->second;
                item.contentCreated = contentCreated;
                item.visible = filter == nullptr || filter (item);
            }

    applySort();
    rebuildRows();
}

Removal::Keepers ResultsModel::keepersOf (const std::vector<FileEntry>& acted) const
{
    std::set<juce::String> actedPaths;

    for (const auto& entry : acted)
        actedPaths.insert (Paths::key (entry.file));

    Removal::Keepers keepers;

    for (const auto& group : result.groups)
    {
        std::vector<FileEntry> inAction, kept;

        for (const auto& item : group.items)
            if (actedPaths.count (Paths::key (item.file)) > 0)
                inAction.push_back (item);
            else if (! item.missing)
                kept.push_back (item);

        keepers.add (inAction, std::move (kept));
    }

    return keepers;
}

void ResultsModel::applySort()
{
    if (sortColumn == Column::check)
        return;

    const auto ordered = [this] (auto& items, auto&& compare)
    {
        std::stable_sort (items.begin(), items.end(), [&] (const auto& a, const auto& b)
        {
            const int c = compare (a, b);
            return sortForwards ? c < 0 : c > 0;
        });
    };

    const auto threeWay = [] (auto a, auto b) { return a < b ? -1 : (a > b ? 1 : 0); };

    for (auto& group : result.groups)
    {
        if (sortColumn == Column::size)
        {
            ordered (group.items, [&] (const ResultItem& a, const ResultItem& b) { return threeWay (a.size, b.size); });
        }
        else if (const auto kind = dateKindOf (sortColumn))
        {
            ordered (group.items, [&] (const ResultItem& a, const ResultItem& b) { return threeWay (dateOf (a, *kind), dateOf (b, *kind)); });
        }
        else
        {
            // Texts are built once per item, not twice per comparison.
            std::vector<std::pair<juce::String, size_t>> keys;
            keys.reserve (group.items.size());

            for (size_t i = 0; i < group.items.size(); ++i)
                keys.emplace_back (getCellText (group.items[i], sortColumn, &group), i);

            ordered (keys, [] (const auto& a, const auto& b) { return a.first.compareNatural (b.first); });

            std::vector<ResultItem> sorted;
            sorted.reserve (group.items.size());

            for (const auto& key : keys)
                sorted.push_back (std::move (group.items[key.second]));

            group.items = std::move (sorted);
        }
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
