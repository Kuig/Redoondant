#pragma once

#include "../Core/Settings.h"
#include "../Core/Removal.h"
#include "../Core/DefaultSelection.h"
#include "../Core/Histogram.h"
#include "RemovalActions.h"
#include "ResultsTable.h"

/** A page that lists items with check boxes and acts on the checked ones: the results table and the
    footer (check cycling button, summary, Move to folder / Move to Trash / Delete permanently).
    The actions themselves are RemovalActions. Derived pages decide what is listed and what "the checked
    items" means for the actions.
*/
class RemovalPage : public juce::Component
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

    /** Lays out the footer and the table in `area`. */
    void layoutTableAndFooter (juce::Rectangle<int> area);

    /** Refreshes the summary line and the buttons' state. */
    void updateSummary();

    /** Tells the preview what is highlighted (nothing: an empty list). */
    void notifySelection (std::vector<FileEntry> entries);

    /** Turns the checked entries into the items the actions operate on, e.g. a folder into its content.
        Runs on the worker thread, so it must be self-contained (not capture the page); empty = use the checked entries as they are.
    */
    virtual std::function<std::vector<FileEntry> (std::vector<FileEntry>)> removalExpander() const    { return {}; }

    /** "12 items (340 MB)": what the check boxes currently select, as the actions see it. */
    virtual juce::String describeChecked() const;

    /** Groups in which every item is checked: the confirmations warn that nothing of them would be kept. */
    virtual int groupsWithoutKeptItem() const                   { return 0; }

    /** Called with the files that are no longer where they were (moved, deleted): update the list. */
    virtual void itemsGone (const juce::Array<juce::File>& files);

    /** Added to the report when some items could not be moved. */
    virtual juce::String failureAdvice() const                  { return {}; }

    /** Called whenever the check states may have changed (before the summary is refreshed). */
    virtual void checksChanged() {}

    /** The table's columns were shown, hidden, moved or sorted (the layout is already saved). */
    virtual void tableLayoutChanged() {}

    /** Appended to the summary line, e.g. the progress of a background read; empty for none. */
    virtual juce::String progressNote() const                   { return {}; }

    /** The summary line; the default lists what is checked. */
    virtual juce::String summaryText (size_t checkedCount, juce::int64 checkedBytes) const;

    /** Asks the user before running an action on the checked items; `action` runs only on OK. */
    void confirm (juce::MessageBoxIconType icon, const juce::String& boxTitle, const juce::String& question,
                  const juce::String& okText, std::function<void()> action);

    static juce::String wrappable (const juce::File& path);

private:
    /** Lets RemovalActions ask this page (and its overrides) what it needs. */
    class ActionsHost final : public RemovalActions::Host
    {
    public:
        explicit ActionsHost (RemovalPage& p) : page (p) {}

        std::vector<FileEntry> checkedEntries() const override                                          { return page.model.getCheckedEntries(); }
        std::function<std::vector<FileEntry> (std::vector<FileEntry>)> expander() const override         { return page.removalExpander(); }
        juce::String describeChecked() const override                                                   { return page.describeChecked(); }
        int groupsWithoutKeptItem() const override                                                      { return page.groupsWithoutKeptItem(); }
        juce::String failureAdvice() const override                                                     { return page.failureAdvice(); }
        void itemsGone (const juce::Array<juce::File>& files) override                                  { page.removeFromList (files); }
        juce::File startFolder() const override                                                         { return page.getRootFolder != nullptr ? page.getRootFolder() : juce::File(); }

    private:
        RemovalPage& page;
    };

    juce::Label summary;
    juce::ComboBox selectionBox;
    DefaultSelection initialSelection = DefaultSelection::none;
    juce::TextButton moveButton { "Move to folder" }, trashButton { "Move to Trash" }, deleteButton { "Delete permanently" };
    ActionsHost actionsHost { *this };
    RemovalActions removal { *this, actionsHost, settings };

    void cycleChecks();
    void selectDefault (DefaultSelection selection);

    /** Drops the files from the list (itemsGone) and clears the preview. */
    void removeFromList (const juce::Array<juce::File>& files);

    /** Drops the files that no longer exist (e.g. deleted from the context menu) from the list. */
    void forgetMissing (const juce::Array<juce::File>& files);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RemovalPage)
};
