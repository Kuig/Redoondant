#pragma once

#include "IconCache.h"
#include "ResultsModel.h"

/** Table showing a ResultsModel: a checkbox column, file details, and group separator rows.
    Click the checkbox column (or press Space) to check items; click a column header to sort.
    Double-click an item to reveal it in the file manager.
*/
class ResultsTable final : public juce::Component,
                           private juce::TableListBoxModel
{
public:
    explicit ResultsTable (ResultsModel& model);
    ~ResultsTable() override;

    /** Call after the model changed. */
    void refresh();

    /** Column widths, order and sort, as a string suitable for persistence. */
    juce::String getLayoutState() const;
    void restoreLayoutState (const juce::String& state);

    std::function<void()> onCheckedChanged;

    /** The header of the check column was clicked: the owner cycles the check states. */
    std::function<void()> onHeaderCheckClicked;

    /** Sorts by a column (what clicking its header does). */
    void setSort (Column column, bool forwards);

    bool isColumnVisible (Column column) const;

    /** Explains what clicking the check column header does (shown as its tooltip). */
    void setHeaderTooltip (const juce::String& text);
    /** The selected (highlighted) entries, group headers excluded; empty when nothing is selected. */
    std::function<void (std::vector<FileEntry>)> onSelectionChanged;
    std::function<void()> onLayoutChanged;

    /** Right-click on items: the files to show a context menu for (same folder), and where. */
    std::function<void (const juce::Array<juce::File>&, juce::Point<int>)> onContextMenu;

    /** The files of the selected (highlighted) rows, group headers excluded. */
    juce::Array<juce::File> getSelectedFiles() const;

    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;

private:
    /** Names rows after their item, for accessibility. */
    struct Table final : public juce::TableListBox
    {
        Table (ResultsTable& o) : TableListBox ({}, &o), owner (o) {}
        juce::String getNameForRow (int row) override;
        ResultsTable& owner;
    };

    ResultsModel& model;
    Table table { *this };
    IconCache icons;

    struct HeaderListener;
    struct CheckHeader;
    std::unique_ptr<HeaderListener> headerListener;

    int getNumRows() override;
    void paintRowBackground (juce::Graphics&, int row, int width, int height, bool isSelected) override;
    void paintCell (juce::Graphics&, int row, int columnId, int width, int height, bool isSelected) override;
    void cellClicked (int row, int columnId, const juce::MouseEvent&) override;
    void cellDoubleClicked (int row, int columnId, const juce::MouseEvent&) override;
    void sortOrderChanged (int columnId, bool isForwards) override;
    void selectedRowsChanged (int lastRowSelected) override;
    juce::String getCellTooltip (int row, int columnId) override;

    void toggle (int row);
    void showContextMenu (int row, juce::Point<int> screenPosition);
    static void drawCheckBox (juce::Graphics&, juce::Rectangle<float> area, CheckState state, juce::Colour colour);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ResultsTable)
};
