#include "ResultsModel.h"
#include "../Core/Format.h"
#include <set>

void ResultsModel::setResult (AnalysisResult newResult, const juce::File& scannedRoot, bool isGrouped)
{
    result = std::move (newResult);
    root = scannedRoot;
    grouped = isGrouped;
    analysed = true;
    applySort();
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

juce::String ResultsModel::getCellText (const FileEntry& entry, Column column) const
{
    switch (column)
    {
        case Column::name:      return entry.name();
        case Column::folder:
        {
            const auto parent = entry.file.getParentDirectory();
            return parent == root ? juce::String() : parent.getRelativePathFrom (root);
        }
        case Column::size:      return Format::size (entry.size);
        case Column::modified:  return Format::date (entry.modified);
        case Column::created:   return Format::date (entry.created);
        case Column::type:      return entry.isDirectory ? juce::String ("Folder")
                                                         : entry.file.getFileExtension().fromFirstOccurrenceOf (".", false, false).toUpperCase();
        case Column::check:     break;
    }

    return {};
}

CheckState ResultsModel::getGroupState (int group) const
{
    size_t total = 0, checked = 0;
    forEachVisible (group, [&] (const FileEntry& e) { ++total; checked += e.selected ? 1 : 0; });

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
        entry.selected = ! entry.selected;
        return;
    }

    const bool check = getGroupState (r->group) != CheckState::all;

    for (auto& item : result.groups[(size_t) r->group].items)
        if (isVisible (item))
            item.selected = check;
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

    for (auto& group : result.groups)
        group.items.erase (std::remove_if (group.items.begin(), group.items.end(), isRemoved), group.items.end());

    const size_t minGroupSize = grouped ? 2 : 0;
    result.groups.erase (std::remove_if (result.groups.begin(), result.groups.end(),
                                         [&] (const ResultGroup& g) { return g.items.size() < minGroupSize; }),
                         result.groups.end());
    rebuildRows();
}

void ResultsModel::applySort()
{
    if (sortColumn == Column::check)
        return;

    const auto compare = [this] (const FileEntry& a, const FileEntry& b) -> int
    {
        switch (sortColumn)
        {
            case Column::size:      return a.size < b.size ? -1 : (a.size > b.size ? 1 : 0);
            case Column::modified:  return a.modified < b.modified ? -1 : (a.modified > b.modified ? 1 : 0);
            case Column::created:   return a.created < b.created ? -1 : (a.created > b.created ? 1 : 0);
            case Column::name:
            case Column::folder:
            case Column::type:
            case Column::check:     break;
        }

        return getCellText (a, sortColumn).compareNatural (getCellText (b, sortColumn));
    };

    for (auto& group : result.groups)
    {
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
