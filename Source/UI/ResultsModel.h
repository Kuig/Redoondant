#pragma once

#include "ListedResult.h"
#include "../Core/Histogram.h"
#include "../Core/Removal.h"
#include <optional>

enum class CheckState { none, some, all };

/** How the groups are ordered in a grouped list (items inside a group follow the column sort). */
enum class GroupOrder { analysis, name, mostItems, largest, newest, oldest };

inline const juce::StringArray& groupOrderNames()
{
    static const juce::StringArray names { "Groups: analysis order", "Groups: by name", "Groups: most items first",
                                           "Groups: largest first", "Groups: newest first", "Groups: oldest first" };
    return names;
}

/** The results of an analysis as displayed: groups flattened into rows (a header row per group
    when grouped), with sorting, filtering and check-state bookkeeping.
    Only visible (unfiltered) entries count as checked and can be trashed.
*/
class ResultsModel
{
public:
    struct Row
    {
        int group = 0;
        int item = -1;      ///< -1 for group header rows.

        bool isHeader() const noexcept      { return item < 0; }
    };

    using Filter = std::function<bool (const FileEntry&)>;

    /** Shows an analysis result. The items start unchecked and present; `initialise` can set the state of each one
        (`selected`, `missing`): what it checks also becomes the "default checks" state, unless a default selection
        (setDefaultSelection) takes over.
    */
    void setResult (AnalysisResult newResult, const juce::File& scannedRoot, bool isGrouped,
                    const std::function<void (ResultItem&)>& initialise = {});
    /** `usesContentDate`: the filter looks at the content created date, so the rows must be filtered again when dates arrive. */
    void setFilter (Filter newFilter, bool usesContentDate = false);

    /** Forgets the result (e.g. the analysed folder changed): back to the state before the first analysis. */
    void clear();
    void sort (Column column, bool forwards);
    void setGroupOrder (GroupOrder newOrder);

    /** Chooses which entries are checked by default. Groups still in their "default" check state are updated
        (and again whenever the sort or the filter changes, for the strategies that depend on them).
        Without a call, the entries keep the checks `initialise` gave them (Cache Cleaner).
    */
    void setDefaultSelection (DefaultSelection newSelection);
    DefaultSelection getDefaultSelection() const    { return defaultSelection.value_or (DefaultSelection::none); }

    bool hasResult() const noexcept                 { return analysed; }

    int getNumRows() const noexcept                 { return (int) rows.size(); }
    const Row* getRow (int index) const;
    const ListedGroup* getGroup (int row) const;
    ResultItem* getEntry (int row);

    /** Text shown in a cell. Entries flagged `missing` have no size or dates. When no folder was
        analysed (empty root) and the group's title is a folder, names are shown relative to it.
    */
    juce::String getCellText (const ResultItem& entry, Column column, const ListedGroup* group = nullptr) const;

    CheckState getGroupState (int group) const;

    /** Flips an entry; on a group header row, cycles the group (see cycleGroup). */
    void toggle (int row);

    /** Applies the group's next bulk state (default checks -> all -> none -> default) to its visible entries. */
    void cycleGroup (int group);

    /** Whether none, some or all of the visible entries are checked. */
    CheckState getOverallState() const;

    size_t getVisibleCount() const;
    std::vector<FileEntry> getCheckedEntries() const;
    std::vector<FileEntry> getVisibleEntries() const;       // Entries flagged `missing` are left out of these lists.

    /** Files (not folders) whose content date has not been read yet. */
    std::vector<FileEntry> filesNeedingContentDate() const;

    /** Stores content dates read in the background (null times are kept as "unknown"). The sort and the filter
        follow the new dates when `refreshView` (the last batch: re-sorting at every batch would shuffle the list).
    */
    void setContentDates (const std::vector<std::pair<juce::File, juce::Time>>& dates, bool refreshView = true);

    /** Every entry of the result that exists, visible or not. */
    std::vector<FileEntry> getAllEntries() const;

    /** What the histogram sums up (every entry, the visible ones, the checked ones), in one pass. */
    Histogram::Input getHistogramInput() const;
    const juce::File& getRoot() const noexcept      { return root; }
    const ListedResult& getResult() const noexcept      { return result; }

    /** Bulk check states for the visible entries. */
    void applyChecks (BulkCheck checks);

    /** Groups of two or more items in which every item is checked (so nothing of them would be kept). */
    int countGroupsFullyChecked() const;

    /** Checks exactly the entries (missing ones excepted) for which `shouldCheck` is true. */
    void checkWhere (const std::function<bool (const FileEntry&)>& shouldCheck);

    /** Removes entries (e.g. after trashing them). In grouped mode, groups that lose items and are left
        with fewer than 2 are dropped (a single remaining file is no longer a duplicate, a version...).
    */
    void remove (const juce::Array<juce::File>& files);

    /** Replaces the scan data (size, dates...) of the listed items that are in `entries`, keeping their state and content date. */
    void update (const std::vector<FileEntry>& entries);

    /** For each group with some of `acted`: the group's other items, which stay (see Removal::Keepers). */
    Removal::Keepers keepersOf (const std::vector<FileEntry>& acted) const;

private:
    ListedResult result;
    juce::File root;
    bool grouped = false;
    bool keepsSmallGroups = false;      ///< A group left with one item stays (nested groups).
    bool analysed = false;
    Filter filter;
    bool filterUsesContentDate = false;
    std::optional<DefaultSelection> defaultSelection;
    GroupOrder groupOrder = GroupOrder::analysis;
    Column sortColumn = Column::check;      // Column::check = keep the analysis order.
    bool sortForwards = true;
    std::vector<Row> rows;

    bool isVisible (const ResultItem& item) const   { return item.visible; }

    /** Runs the filter on every item (whenever the filter or what it looks at changes). */
    void refreshVisibility();

    /** The entries for which `keep` is true, in list order. */
    template <typename Predicate>
    std::vector<FileEntry> collect (Predicate&& keep) const
    {
        std::vector<FileEntry> entries;

        for (const auto& group : result.groups)
            for (const auto& item : group.items)
                if (keep (item))
                    entries.push_back (item);

        return entries;
    }

    void applySort();
    void sortGroups();
    void rebuildRows();

    /** Recomputes the defaults and checks the groups that are in their default state. */
    void updateDefaults();
    void updateViewDependentDefaults()      { if (defaultSelection && DefaultSelections::dependsOnView (*defaultSelection)) updateDefaults(); }

    template <typename Function>
    void forEachVisible (int group, Function&& f) const
    {
        for (auto& item : result.groups[(size_t) group].items)
            if (isVisible (item))
                f (item);
    }
};
