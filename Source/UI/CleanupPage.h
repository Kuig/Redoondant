#pragma once

#include "RemovalPage.h"
#include <set>

/** "Cache Cleaner": a user-editable list of temporary/cache folders, with their sizes, grouped by
    common parent. Checked folders are emptied (their content is moved or deleted, the folders stay).
*/
class CleanupPage final : public RemovalPage
{
public:
    explicit CleanupPage (SettingsScope settings);
    ~CleanupPage() override;

    void resized() override;

    /** Measures the folders again (in the background). */
    void refresh();

private:
    struct Folder
    {
        juce::String stored;    ///< As saved: may contain %VARIABLE% tokens.
        juce::File file;        ///< Expanded.
        bool checkedByDefault = false;
    };

    juce::Label title, description, status;
    juce::TextButton addButton { "Add folder..." }, removeButton { "Remove" },
                     resetButton { "Reset to defaults" }, refreshButton { "Refresh" };
    std::unique_ptr<juce::FileChooser> chooser;

    std::vector<Folder> folders;
    std::set<juce::String> checkedPaths;    ///< Full paths of the checked folders.
    int generation = 0;
    juce::ThreadPool measurer { 1 };

    void loadFolders();
    void saveFolders();
    void addFolder (const juce::File& folder);
    void removeSelected();
    void resetToDefaults();
    void showMeasured (std::vector<FileEntry> entries);
    void updateButtons();

    std::vector<FileEntry> itemsToRemove() const override;
    juce::String describeChecked() const override;
    void itemsGone (const juce::Array<juce::File>&) override    { refresh(); }
    void checksChanged() override;
    juce::String failureAdvice() const override;
    juce::String summaryText (size_t checkedCount, juce::int64 checkedBytes) const override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CleanupPage)
};
