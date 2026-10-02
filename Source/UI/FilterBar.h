#pragma once

#include "ParametersPanel.h"
#include "ResultsModel.h"

/** Type (several can be ticked) / date / size / name filters for the results list. All filters combine (AND).
    Built on ParameterSet, so its editors and persistence come from ParametersPanel.
*/
class FilterBar final : public juce::Component
{
public:
    explicit FilterBar (SettingsScope settings);

    /** A predicate reflecting the current filter values. */
    ResultsModel::Filter createFilter() const;

    int getHeightForWidth (int width) const     { return panel.getHeightForWidth (width); }

    std::function<void()> onChange;

    void resized() override                     { panel.setBounds (getLocalBounds()); }

private:
    ParameterSet parameters;
    ParametersPanel panel;

    static ParameterSet createParameters();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FilterBar)
};
