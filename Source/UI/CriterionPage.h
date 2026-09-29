#pragma once

#include "../Criteria/Criterion.h"
#include "FilterBar.h"
#include "ParametersPanel.h"
#include "ResultsTable.h"

/** The main area for one criterion: its settings, the Analyze button, the results list
    and the Move to Trash button. Each criterion has its own page, so results are kept
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
    juce::TextButton trashButton { "Move to Trash" };

    void analyse();
    void showResult (AnalysisResult result, const juce::File& root);
    void moveCheckedToTrash();
    void resetToDefaults();
    void updateSummary();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CriterionPage)
};
