#pragma once

#include "Core/Settings.h"
#include "Criteria/Criterion.h"
#include "UI/CleanupPage.h"
#include "UI/CriterionPage.h"
#include "UI/PreviewPanel.h"

/** The main window content: folder bar on top, criteria on the left (with the Cache Cleaner tool
    at the bottom), the selected page in the middle and the preview on the right.
*/
class MainComponent final : public juce::Component
{
public:
    /** folderToOpen: the folder to analyze instead of the saved one (from "Open in Redoondant"), if valid. */
    MainComponent (Settings& settings, const juce::File& folderToOpen = {});
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class CriteriaListModel;

    /** The bottom entry of the sidebar that opens the Cache Cleaner. */
    class ToolEntry final : public juce::Component
    {
    public:
        std::function<void()> onClick;
        void setSelected (bool shouldBeSelected);

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override   { if (onClick != nullptr) onClick(); }

    private:
        bool selected = false;
    };

    /** Criteria list above, tool entry pinned to the bottom. */
    class Sidebar final : public juce::Component
    {
    public:
        Sidebar (juce::ListBox& criteria, ToolEntry& tool);
        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        juce::ListBox& list;
        ToolEntry& toolEntry;
    };

    Settings& appSettings;
    SettingsScope settings;
    /** A criterion and its page (declared in this order so the page, which refers to the criterion, goes first). */
    struct CriterionEntry
    {
        std::unique_ptr<Criterion> criterion;
        std::unique_ptr<CriterionPage> page;
    };

    std::vector<CriterionEntry> entries;
    std::unique_ptr<CleanupPage> cleanupPage;
    RemovalPage* currentPage = nullptr;
    juce::File analysedFolder;          ///< The folder the pages were last reset for.

    juce::Label folderLabel { {}, "Folder:" };
    juce::TextEditor folderEditor;
    juce::TextButton browseButton { "Browse..." };
    juce::ToggleButton explorerMenuButton { "Explorer menu" };
    juce::TextButton resetSettingsButton { "Reset settings" };
    juce::TextButton helpButton { "?" };
    juce::TooltipWindow tooltipWindow { this };      // Shows the tooltips of all child components.
    std::unique_ptr<juce::FileChooser> chooser;

    std::unique_ptr<CriteriaListModel> criteriaModel;
    juce::ListBox criteriaList;
    ToolEntry cacheEntry;
    Sidebar sidebar { criteriaList, cacheEntry };
    PreviewPanel preview;

    /** A resizer bar that reports when the user drags it. */
    struct ResizerBar final : public juce::StretchableLayoutResizerBar
    {
        using StretchableLayoutResizerBar::StretchableLayoutResizerBar;
        std::function<void()> onMoved;

        void hasBeenMoved() override
        {
            StretchableLayoutResizerBar::hasBeenMoved();

            if (onMoved != nullptr)
                onMoved();
        }
    };

    juce::StretchableLayoutManager layout;
    ResizerBar leftBar { &layout, 1, true }, rightBar { &layout, 3, true };

    juce::File getRootFolder() const;
    std::vector<PeerCriterion> describePeers() const;
    void setRootFolder (const juce::File& folder);
    void browseForFolder();
    void confirmResetSettings();
    void setExplorerMenu (bool shouldBeInstalled);
    void showPage (int index);
    void showCleanupPage();
    void setCurrentPage (RemovalPage* page);
    void wirePage (RemovalPage& page);     ///< Connects a page to the folder bar and the preview.
    void commitFolderText();                ///< The folder text was edited: use it if it is a folder, else restore the previous one.

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
