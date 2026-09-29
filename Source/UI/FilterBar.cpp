#include "FilterBar.h"
#include "../Core/FileCategory.h"

namespace
{
    /** Date filter choices: items modified within (or before) a number of days. */
    struct AgeOption
    {
        const char* label;
        double days;        ///< 0 = any date.
        bool newer;         ///< true: modified within `days`; false: modified before.
    };

    const AgeOption ageOptions[] =
    {
        { "Any date",               0,   true },
        { "Modified today",         1,   true },
        { "In the last 7 days",     7,   true },
        { "In the last 30 days",    30,  true },
        { "In the last year",       365, true },
        { "Older than 30 days",     30,  false },
        { "Older than 1 year",      365, false },
    };

    constexpr double bytesPerMB = 1024.0 * 1024.0;
}

ParameterSet FilterBar::createParameters()
{
    juce::StringArray types { "All types" };
    juce::StringArray ages;

    for (auto category : FileCategories::all())
        types.add (FileCategories::nameOf (category));

    for (const auto& option : ageOptions)
        ages.add (option.label);

    auto age = Parameter::choice ("age", "Date", ages);
    age.editorWidth = 150;

    return { Parameter::text ("name", "Name contains", {}, 120),
             Parameter::choice ("type", "Type", types),
             age,
             Parameter::number ("minSize", "Size from", 0, "MB"),
             Parameter::number ("maxSize", "to", 0, "MB (0 = any)") };
}

FilterBar::FilterBar (SettingsScope settings)
    : parameters (createParameters()), panel (parameters, settings)
{
    panel.onChange = [this]
    {
        if (onChange != nullptr)
            onChange();
    };

    addAndMakeVisible (panel);
}

ResultsModel::Filter FilterBar::createFilter() const
{
    const auto name = parameters.getText ("name").trim();
    const int typeIndex = parameters.getChoice ("type") - 1;     // -1 = all types
    const auto& age = ageOptions[juce::jlimit (0, (int) std::size (ageOptions) - 1, parameters.getChoice ("age"))];
    const auto minBytes = (juce::int64) (parameters.getNumber ("minSize") * bytesPerMB);
    const auto maxBytes = (juce::int64) (parameters.getNumber ("maxSize") * bytesPerMB);
    const auto cutoff = juce::Time::getCurrentTime() - juce::RelativeTime::days (age.days);

    return [=] (const FileEntry& e)
    {
        if (name.isNotEmpty() && ! e.name().containsIgnoreCase (name))
            return false;

        if (typeIndex >= 0 && FileCategories::of (e) != FileCategories::all()[(size_t) typeIndex])
            return false;

        if (age.days > 0 && (e.modified >= cutoff) != age.newer)
            return false;

        return e.size >= minBytes && (maxBytes <= 0 || e.size <= maxBytes);
    };
}
