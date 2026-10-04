#pragma once

#include "../Core/Settings.h"
#include "../Core/Removal.h"
#include "../Core/DefaultSelection.h"
#include "../Core/Histogram.h"
#include "RemovalActions.h"
#include "ResultsTable.h"

/** A page that lists items with check boxes and acts on the checked ones: the results table and the
    footer (check cycling button, summary, Move to folder / Move to Trash / Delete permanently).
    The actions themselves are RemovalActions, which ask the page what they need (RemovalActions::Host).
    Derived pages decide what is listed and what "the checked items" means for the actions.
*/
class RemovalPage : public juce::Component,
                    protected RemovalActions::Host
{
public:
    /** Optional: a folder to start the "Move to folder" chooser in when none was used before. */
    std::function<juce::File()> getRootFolder;

    /** Called with the highlighted entries (none: empty) for the preview. */
    std::function<void (std::vector<FileEntry>)> onSelectionChanged;

    /** Called when the list, the filters or the checks changed (for the histogram of the preview). */
    std::function<void()> onListChanged;

    /** What the histogram sums up: every entry, the visible ones (filters) and the checked ones. */
    Histogram::Input getHistogramInput() const;

    /** A bar of the histogram was clicked: set the filters, if the page has any. */
    virtual void applyHistogramBin (Property, const Histogram::Bin&) {}

protected:
    explicit RemovalPage (SettingsScope settings);
    ~RemovalPage() override;

    static constexpr int margin = 12, rowHeight = 26, gap = 8;

    SettingsScope settings;
    ResultsModel model;
    ResultsTable table { model };
    BulkCheck checks = BulkCheck::defaults;

    /** Shows the drop-down that chooses the default selection (what the check cycle calls "default checks"),
        offering `available`. The last choice is remembered; `initial` is used when there is none or it is not offered.
    */
    void setupDefaultSelection (std::vector<DefaultSelection> available, DefaultSelection initial);

    /** Goes back to the `initial` selection given to setupDefaultSelection. */
    void resetDefaultSelection();

    /** Empties the list, as before the first analysis (the analysed folder is no longer the current one). */
    void discardResults();

    /** Shows the page's title and description at its top (see layoutHeading). */
    void setHeading (const juce::String& title, const juce::String& description);

    /** Lays out the title and description at the top of `area`, and removes them from it. */
    void layoutHeading (juce::Rectangle<int>& area);

    /** Lays out the footer and the table in `area`. */
    void layoutTableAndFooter (juce::Rectangle<int> area);

    /** Refreshes the summary line and the buttons' state. */
    void updateSummary();

    /** Tells the preview what is highlighted (nothing: an empty list). */
    void notifySelection (std::vector<FileEntry> entries);

    // What the actions ask (RemovalActions::Host); the defaults suit a plain list of items.
    std::vector<FileEntry> checkedEntries() const override      { return model.getCheckedEntries(); }
    std::function<std::vector<FileEntry> (std::vector<FileEntry>)> removalExpander() const override    { return {}; }
    juce::String describe (const std::vector<FileEntry>& checked) const override;
    int groupsWithoutKeptItem() const override                  { return 0; }
    juce::String failureAdvice() const override                 { return {}; }
    juce::File startFolder() const override                     { return getRootFolder != nullptr ? getRootFolder() : juce::File(); }

    /** Called with the files that are no longer where they were (moved, deleted): update the list. */
    virtual void itemsGone (const juce::Array<juce::File>& files);

    /** What the list holds, in the summary and the confirmations: "item", "folder"... */
    virtual juce::String itemNoun() const                       { return "item"; }

    /** The summary line before there is a list. */
    virtual juce::String noListText() const                     { return "Press Analyze to search for candidates."; }

    /** Called whenever the check states may have changed (before the summary is refreshed). */
    virtual void checksChanged() {}

    /** The table's columns were shown, hidden, moved or sorted (the layout is already saved). */
    virtual void tableLayoutChanged() {}

    /** Appended to the summary line, e.g. the progress of a background read; empty for none. */
    virtual juce::String progressNote() const                   { return {}; }

private:
    juce::Label title, description, summary;
    juce::ComboBox selectionBox;
    DefaultSelection initialSelection = DefaultSelection::none;
    juce::TextButton moveButton { "Move to folder" }, trashButton { "Move to Trash" }, deleteButton { "Delete permanently" };
    RemovalActions removal { *this, *this, settings };

    void cycleChecks();
    void selectDefault (DefaultSelection selection);

    /** Drops the files from the list (itemsGone) and clears the preview. */
    void removedByAction (const juce::Array<juce::File>& files) final;

    /** Drops the files that no longer exist (e.g. deleted from the context menu) from the list. */
    void forgetMissing (const juce::Array<juce::File>& files);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RemovalPage)
};
