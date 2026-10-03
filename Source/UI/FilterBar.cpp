#include "FilterBar.h"
#include "../Core/FileCategory.h"
#include "../Core/Histogram.h"

namespace
{
    constexpr double bytesPerMB = 1024.0 * 1024.0;
}

ParameterSet FilterBar::createParameters()
{
    juce::StringArray types;
    juce::StringArray ages { "Any date" };      // Then the date buckets of the histogram.

    for (auto category : FileCategories::all())
        types.add (FileCategories::nameOf (category));

    ages.addArray (Histogram::Buckets::dateNames());

    auto kind = Parameter::choice ("dateKind", {}, { "Created", "Modified", "Content created" }, (int) DateKind::modified);   // The order of DateKind.
    kind.editorWidth = 130;

    auto age = Parameter::choice ("age", {}, ages);
    age.editorWidth = 170;

    return { Parameter::text ("name", "Name contains", {}, 120),
             Parameter::text ("notName", "and not", {}, 120),
             Parameter::text ("folder", "in folder", {}, 120),
             Parameter::multiChoice ("type", {}, "All types", types),
             kind,
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
    const auto notName = parameters.getText ("notName").trim();
    const auto folder = parameters.getText ("folder").trim();
    std::vector<FileCategory> types;     // Empty = all types.

    for (const int index : parameters.getSelection ("type"))
        types.push_back (FileCategories::all()[(size_t) index]);

    const auto kind = (DateKind) juce::jlimit (0, 2, parameters.getChoice ("dateKind"));
    const int dateBucket = parameters.getChoice ("age") - 1;        // -1: any date.
    const auto minBytes = (juce::int64) (parameters.getNumber ("minSize") * bytesPerMB);
    const auto maxBytes = (juce::int64) (parameters.getNumber ("maxSize") * bytesPerMB);
    const auto now = juce::Time::getCurrentTime();
    const auto base = root;

    return [=] (const FileEntry& e)
    {
        if (name.isNotEmpty() && ! e.name().containsIgnoreCase (name))
            return false;

        if (notName.isNotEmpty() && e.name().containsIgnoreCase (notName))
            return false;

        if (! types.empty() && std::find (types.begin(), types.end(), FileCategories::of (e)) == types.end())
            return false;

        if (folder.isNotEmpty() && ! Histogram::folderOf (e, base).containsIgnoreCase (folder))
            return false;

        if (dateBucket >= 0 && Histogram::Buckets::dateBucket (dateOf (e, kind), now) != dateBucket)
            return false;

        return e.size >= minBytes && (maxBytes <= 0 || e.size <= maxBytes);
    };
}

void FilterBar::applyBin (Property property, const Histogram::Bin& bin)
{
    using Values = std::vector<std::pair<juce::String, juce::var>>;
    Values target, cleared;

    switch (property)
    {
        case Property::size:
        {
            const auto range = Histogram::Buckets::sizeRange (bin.key);
            target = { { "minSize", range.fromMB }, { "maxSize", range.toMB } };
            cleared = { { "minSize", 0 }, { "maxSize", 0 } };
            break;
        }

        case Property::folder:
            target = { { "folder", bin.folder } };
            cleared = { { "folder", juce::String() } };
            break;

        case Property::type:
            target = { { "type", juce::String (bin.key) } };
            cleared = { { "type", juce::String() } };
            break;

        case Property::modified:
        case Property::created:
        case Property::contentCreated:
            target = { { "dateKind", (int) *Histogram::dateKindOf (property) }, { "age", bin.key + 1 } };
            cleared = { { "age", 0 } };
            break;
    }

    // Clicking the bar that is already applied takes its filter off.
    const bool applied = std::all_of (target.begin(), target.end(), [this] (const auto& v) { return parameters[v.first].toString() == v.second.toString(); });
    panel.setValues (applied ? cleared : target);
}
