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
    type
};

enum class CheckState { none, some, all };

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

    bool hasResult() const noexcept                 { return analysed; }
    bool isGrouped() const noexcept                 { return grouped; }

    int getNumRows() const noexcept                 { return (int) rows.size(); }
    const Row* getRow (int index) const;
    const ResultGroup* getGroup (int row) const;
    FileEntry* getEntry (int row);

    /** Text shown in a cell. */
    juce::String getCellText (const FileEntry& entry, Column column) const;

    CheckState getGroupState (int group) const;

    /** Flips an entry, or all visible entries of a group for header rows. */
    void toggle (int row);

    size_t getVisibleCount() const;
    std::vector<FileEntry> getCheckedEntries() const;

    /** Every entry of the result, visible or not. */
    std::vector<FileEntry> getAllEntries() const;
    const juce::File& getRoot() const noexcept      { return root; }

    /** Bulk check states for the visible entries. */
    enum class Checks { defaults, all, none };
    void applyChecks (Checks checks);

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
    Column sortColumn = Column::check;      // Column::check = keep the analysis order.
    bool sortForwards = true;
    std::vector<Row> rows;

    bool isVisible (const FileEntry& entry) const   { return filter == nullptr || filter (entry); }
    void applySort();
    void rebuildRows();

    template <typename Function>
    void forEachVisible (int group, Function&& f) const
    {
        for (auto& item : result.groups[(size_t) group].items)
            if (isVisible (item))
                f (item);
    }
};
