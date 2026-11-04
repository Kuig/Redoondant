#pragma once

#include "../Core/Histogram.h"
#include "ParametersPanel.h"
#include "ResultsModel.h"

/** Type and content kind (several can be ticked) / date / size / name filters for the results list. All filters combine (AND).
    Built on ParameterSet, so its editors and persistence come from ParametersPanel.
*/
class FilterBar final : public juce::Component,
                        private juce::Timer
{
public:
    explicit FilterBar (SettingsScope settings);

    /** The analysed folder, which the Folder filter is relative to. Call createFilter() again afterwards. */
    void setRoot (const juce::File& newRoot)    { root = newRoot; }

    /** Whether the analysis looks into sub-folders: only then the "exclude if path contains" filter is shown and applied.
        Call createFilter() again afterwards, and lay the bar out again (its height changes).
    */
    void setRecursive (bool isRecursive)
    {
        recursive = isRecursive;
        panel.setHidden ("excludePath", ! isRecursive);
    }

    /** Sets the filters so that only the entries of a histogram bar are shown (again: takes them off). */
    void applyBin (Property property, const Histogram::Bin& bin);

    /** What the filter looks at among the values filled in the background (content created date, content kind). */
    ResultsModel::FilterUses uses() const;

    /** A predicate reflecting the current filter values. */
    ResultsModel::Filter createFilter() const;

    int getHeightForWidth (int width) const     { return panel.getHeightForWidth (width); }

    /** Called when the filters changed, once typing pauses (each key would filter the whole list again). */
    std::function<void()> onChange;

    void resized() override                     { panel.setBounds (getLocalBounds()); }

private:
    ParameterSet parameters;
    ParametersPanel panel;
    juce::File root;
    bool recursive = true;

    static ParameterSet createParameters();

    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FilterBar)
};
