#pragma once

#include "../Core/Settings.h"
#include "../Core/Trash.h"
#include "ResultsTable.h"

/** A page that lists items with check boxes and acts on the checked ones: the results table and the
    footer (check cycling button, summary, Move to folder / Move to Trash / Delete permanently),
    with the confirmation and report boxes. Derived pages decide what is listed and what "the checked
    items" means for the actions.
*/
class RemovalPage : public juce::Component
{
public:
    /** Optional: a folder to start the "Move to folder" chooser in when none was used before. */
    std::function<juce::File()> getRootFolder;

    /** Called with the highlighted entry (or nullptr) for the preview. */
    std::function<void (const FileEntry*)> onItemSelected;

protected:
    explicit RemovalPage (SettingsScope settings);
    ~RemovalPage() override;

    SettingsScope settings;
    ResultsModel model;
    ResultsTable table { model };
    Checks checks = Checks::defaults;

    /** Lays out the footer and the table in `area`. */
    void layoutTableAndFooter (juce::Rectangle<int> area);

    /** Refreshes the summary line and the buttons' state. */
    void updateSummary();

    /** What the actions operate on: by default the checked entries. */
    virtual std::vector<FileEntry> itemsToRemove() const        { return model.getCheckedEntries(); }

    /** "12 items (340 MB)": what the check boxes currently select, as the actions see it. */
    virtual juce::String describeChecked() const;

    /** Called with the files that are no longer where they were (moved, deleted): update the list. */
    virtual void itemsGone (const juce::Array<juce::File>& files);

    /** Added to the report when some items could not be moved. */
    virtual juce::String failureAdvice() const                  { return {}; }

    /** Called whenever the check states may have changed (before the summary is refreshed). */
    virtual void checksChanged() {}

    /** The summary line; the default lists what is checked. */
    virtual juce::String summaryText (size_t checkedCount, juce::int64 checkedBytes) const;

    /** Asks the user before running an action on the checked items; `action` runs only on OK. */
    void confirm (juce::MessageBoxIconType icon, const juce::String& boxTitle, const juce::String& question,
                  const juce::String& okText, std::function<void()> action);

    static juce::String wrappable (const juce::File& path);

private:
    juce::Label summary;
    juce::TextButton moveButton { "Move to folder" }, trashButton { "Move to Trash" }, deleteButton { "Delete permanently" };
    std::unique_ptr<juce::FileChooser> chooser;

    void moveCheckedToTrash();
    void moveCheckedToFolder();
    void deleteChecked();
    void cycleChecks();

    /** Updates the list and tells the user what happened. */
    void finishRemoval (const RemovalReport& report, const juce::String& boxTitle, const juce::String& done);

    /** Drops the files that no longer exist (e.g. deleted from the context menu) from the list. */
    void forgetMissing (const juce::Array<juce::File>& files);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RemovalPage)
};
