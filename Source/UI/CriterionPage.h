#pragma once

#include "../Criteria/Peers.h"
#include "ContentDateLoader.h"
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

    /** Supplies the other criteria for the ones that combine them (called on the message thread when Analyze is pressed). */
    std::function<std::vector<PeerCriterion>()> getPeers;

    /** Drops the results (they belong to another folder). */
    void forgetResults();

    void applyHistogramBin (Property property, const Histogram::Bin& bin) override   { filterBar->applyBin (property, bin); }

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    const Criterion& criterion;
    ParameterSet parameters;

    juce::Label status;
    ParametersPanel parametersPanel;
    juce::ToggleButton recursiveToggle { "Recursive (include sub-folders)" };
    juce::TextButton analyseButton { "Analyze" }, resetButton { "Reset to defaults" };
    std::unique_ptr<FilterBar> filterBar;
    juce::ComboBox groupOrderBox;
    int separatorY = 0;        ///< Where the line between the analysis controls and the filters is drawn.
    ContentDateLoader dates { model, table, [this] { updateSummary(); } };

    void tableLayoutChanged() override                  { dates.load (false); }     // The column may have just been shown or sorted.
    juce::String progressNote() const override;
    int groupsWithoutKeptItem() const override          { return criterion.getInfo().grouped ? model.countGroupsFullyChecked() : 0; }

    Removal::Keepers keepersOf (const std::vector<FileEntry>& checked) const override
    {
        return criterion.getInfo().groupsOfCopies ? model.keepersOf (checked) : Removal::Keepers();
    }

    void analyse();
    void applyFilter();             ///< The filter bar changed: refilter the list.
    void showResult (AnalysisResult result, const juce::File& root);
    void resetToDefaults();
    void applyDefaultSort();
    void updatePathFilter();        ///< The "exclude if path contains" filter exists only for recursive analyses.

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CriterionPage)
};
