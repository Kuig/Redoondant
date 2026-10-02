#pragma once

#include "../Criteria/Peers.h"
#include "FilterBar.h"
#include "ParametersPanel.h"
#include "RemovalPage.h"

/** The main area for one criterion: its settings, the Analyze button and the results list
    (with the actions of RemovalPage). Each criterion has its own page, so results are kept
    while switching between criteria.
*/
class CriterionPage final : public RemovalPage
{
public:
    CriterionPage (const Criterion& criterion, SettingsScope settings);
    ~CriterionPage() override;

    /** The criterion with its current settings and results, for criteria that combine others. */
    PeerCriterion describeAsPeer() const;

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    class AnalysisJob;

    const Criterion& criterion;
    ParameterSet parameters;

    juce::Label title, description, status;
    ParametersPanel parametersPanel;
    juce::ToggleButton recursiveToggle { "Recursive (include sub-folders)" };
    juce::TextButton analyseButton { "Analyze" }, resetButton { "Reset to defaults" };
    std::unique_ptr<FilterBar> filterBar;
    juce::ComboBox groupOrderBox;
    int separatorY = 0;        ///< Where the line between the analysis controls and the filters is drawn.

    void analyse();
    void showResult (AnalysisResult result, const juce::File& root);
    void resetToDefaults();
    void applyDefaultSort();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CriterionPage)
};
