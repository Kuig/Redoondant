#pragma once

#include "../Core/AnalysisResult.h"

/** Columns of the results table (values are TableHeaderComponent column ids). */
enum class Column
{
    check = 1,
    name,
    folder,
    size,
    modified,
    created,
    type,
    contentCreated
};

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

    void setResult (AnalysisResult newResult, const juce::File& scannedRoot, bool isGrouped);
    void setFilter (Filter newFilter);
    void sort (Column column, bool forwards);
    void setGroupOrder (GroupOrder newOrder);

    bool hasResult() const noexcept                 { return analysed; }
    bool isGrouped() const noexcept                 { return grouped; }

    int getNumRows() const noexcept                 { return (int) rows.size(); }
    const Row* getRow (int index) const;
    const ResultGroup* getGroup (int row) const;
    FileEntry* getEntry (int row);

    /** Text shown in a cell. Entries flagged `missing` have no size or dates. When no folder was
        analysed (empty root) and the group's title is a folder, names are shown relative to it.
    */
    juce::String getCellText (const FileEntry& entry, Column column, const ResultGroup* group = nullptr) const;

    CheckState getGroupState (int group) const;

    /** Flips an entry; on a group header row, cycles the group (see cycleGroup). */
    void toggle (int row);

    /** Applies the group's next bulk state (default checks -> all -> none -> default) to its visible entries. */
    void cycleGroup (int group);

    /** Whether none, some or all of the visible entries are checked. */
    CheckState getOverallState() const;

    size_t getVisibleCount() const;
    std::vector<FileEntry> getCheckedEntries() const;

    /** Files (not folders) whose content date has not been read yet. */
    std::vector<juce::File> filesNeedingContentDate() const;

    /** Stores content dates read in the background (null times are kept as "unknown"). */
    void setContentDates (const std::vector<std::pair<juce::File, juce::Time>>& dates);

    /** Every entry of the result, visible or not. */
    std::vector<FileEntry> getAllEntries() const;
    const juce::File& getRoot() const noexcept      { return root; }

    /** Bulk check states for the visible entries. */
    using Checks = ::Checks;
    void applyChecks (Checks checks);

    /** Checks exactly the entries (missing ones excepted) for which `shouldCheck` is true. */
    void checkWhere (const std::function<bool (const FileEntry&)>& shouldCheck);

    /** Removes entries (e.g. after trashing them). In grouped mode, groups that lose items and are left
        with fewer than 2 are dropped (a single remaining file is no longer a duplicate, a version...).
    */
    void remove (const juce::Array<juce::File>& files);

private:
    AnalysisResult result;
    juce::File root;
    bool grouped = false;
    bool analysed = false;
    Filter filter;
    GroupOrder groupOrder = GroupOrder::analysis;
    Column sortColumn = Column::check;      // Column::check = keep the analysis order.
    bool sortForwards = true;
    std::vector<Row> rows;

    bool isVisible (const FileEntry& entry) const   { return filter == nullptr || filter (entry); }
    void applySort();
    void sortGroups();
    void rebuildRows();

    template <typename Function>
    void forEachVisible (int group, Function&& f) const
    {
        for (auto& item : result.groups[(size_t) group].items)
            if (isVisible (item))
                f (item);
    }
};
