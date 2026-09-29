#pragma once

#include "../Core/Trash.h"
#include "../Criteria/Peers.h"
#include "FilterBar.h"
#include "ParametersPanel.h"
#include "ResultsTable.h"

/** The main area for one criterion: its settings, the Analyze button, the results list
    and the Move to Trash / Move to folder buttons. Each criterion has its own page, so results are kept
    while switching between criteria.
*/
class CriterionPage final : public juce::Component
{
public:
    CriterionPage (const Criterion& criterion, SettingsScope settings);
    ~CriterionPage() override;

    /** Provides the folder to analyse. */
    std::function<juce::File()> getRootFolder;

    /** Called with the highlighted entry (or nullptr) for the preview. */
    std::function<void (const FileEntry*)> onItemSelected;

    /** The criterion with its current settings and results, for criteria that combine others. */
    PeerCriterion describeAsPeer() const;

    void resized() override;

private:
    class AnalysisJob;

    const Criterion& criterion;
    SettingsScope settings;
    ParameterSet parameters;

    juce::Label title, description, status;
    ParametersPanel parametersPanel;
    juce::ToggleButton recursiveToggle { "Recursive (include sub-folders)" };
    juce::TextButton analyseButton { "Analyze" }, resetButton { "Reset to defaults" };
    std::unique_ptr<FilterBar> filterBar;

    ResultsModel model;
    ResultsTable table { model };
    juce::Label summary;
    juce::TextButton checksButton, moveButton { "Move to folder" }, trashButton { "Move to Trash" };
    std::unique_ptr<juce::FileChooser> chooser;
    ResultsModel::Checks checks = ResultsModel::Checks::defaults;

    void analyse();
    void showResult (AnalysisResult result, const juce::File& root);
    void moveCheckedToTrash();
    void moveCheckedToFolder();

    /** Asks the user before running an action on the checked items; `action` runs only on OK. */
    void confirm (juce::MessageBoxIconType icon, const juce::String& boxTitle, const juce::String& question,
                  const juce::String& okText, std::function<void()> action);

    /** Removes the moved items from the list and tells the user what happened. */
    void finishRemoval (const RemovalReport& report, const juce::String& boxTitle, const juce::String& done);

    /** "12 items (340 MB)": what the check boxes currently select, as the actions see it. */
    juce::String describeChecked() const;
    void resetToDefaults();
    void cycleChecks();

    /** Drops the files that no longer exist (e.g. deleted from the context menu) from the list. */
    void forgetMissing (const juce::Array<juce::File>& files);
    void updateSummary();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CriterionPage)
};
