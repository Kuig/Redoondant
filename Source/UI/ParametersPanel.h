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

    /** Shows or hides a parameter's editor. The panel's height changes, so its owner should lay it out again. */
    void setHidden (const juce::String& id, bool shouldBeHidden);

    /** Sets some parameters from outside (e.g. a histogram bar that sets filters), updating editors and settings; one onChange. */
    void setValues (const std::vector<std::pair<juce::String, juce::var>>& values);

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

    /** One editing widget per parameter kind, put into the editor (see createEditor). */
    void fillToggle (Parameter& parameter, Editor& editor);
    void fillText (Parameter& parameter, Editor& editor);       // Numbers and text.
    void fillMultiChoice (Parameter& parameter, Editor& editor);
    void fillChoice (Parameter& parameter, Editor& editor);

    /** The enable check box of parameters that have one (hasToggle). */
    void addEnableToggle (Parameter& parameter, Editor& editor);
    void changed (Parameter& parameter, const juce::var& newValue);

    /** Places editors in lines; returns the total height. */
    int layout (int width, bool apply) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParametersPanel)
};
