#pragma once

#include "../Criteria/Parameter.h"
#include "../Core/Settings.h"

/** Generates editors for a ParameterSet, keeps the values in sync and persists them.
    Editors flow left to right and wrap onto new lines when space runs out.
*/
class ParametersPanel final : public juce::Component
{
public:
    /** Loads stored values from settings into the parameters. */
    ParametersPanel (ParameterSet& parameters, SettingsScope settings);
    ~ParametersPanel() override;

    void resetToDefaults();

    /** Height needed to show all editors at the given width. */
    int getHeightForWidth (int width) const;

    std::function<void()> onChange;

    void resized() override;

private:
    struct Editor;

    ParameterSet& parameters;
    SettingsScope settings;
    std::vector<std::unique_ptr<Editor>> editors;

    std::unique_ptr<Editor> createEditor (Parameter& parameter);
    void changed (Parameter& parameter, const juce::var& newValue);

    /** Places editors in lines; returns the total height. */
    int layout (int width, bool apply) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParametersPanel)
};
