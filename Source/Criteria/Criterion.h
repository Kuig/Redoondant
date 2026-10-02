#pragma once

#include "Parameter.h"
#include "../Core/AnalysisResult.h"
#include "../Core/DefaultSelection.h"
#include "../Core/ScanContext.h"

/** A rule that finds deletion candidates in a folder.

    Criteria are stateless: analyse() receives a snapshot of the parameter values and runs
    on a background thread, so it must not touch the UI. To add a new criterion, derive from
    this class and register it in CriterionRegistry.cpp; UI and persistence are generated.
*/
class Criterion
{
public:
    struct Info
    {
        juce::String id;            ///< Stable key, used for settings.
        juce::String name;
        juce::String description;
        bool grouped = false;       ///< Results are shown as separated groups.
        bool exhaustive = false;    ///< Lists (almost) everything, so being listed means nothing by itself.
        bool recursiveByDefault = false;
        bool showsFolders = false;  ///< Folders can be listed (so "folders only" / "files only" make sense).
    };

    /** What the criterion suggests for the check boxes and the sort: the user can change both. */
    struct Defaults
    {
        DefaultSelection selection = DefaultSelection::none;
        Column sortColumn = Column::check;      ///< Column::check = the order of the analysis.
        bool sortForwards = true;
    };

    explicit Criterion (Info i, Defaults d = {}) : info (std::move (i)), defaults (d) {}
    virtual ~Criterion() = default;

    const Info& getInfo() const noexcept            { return info; }
    const Defaults& getDefaults() const noexcept    { return defaults; }

    /** The parameters with their default values. */
    virtual ParameterSet createParameters() const   { return {}; }

    virtual AnalysisResult analyse (const ScanContext& context, const ParameterSet& parameters) const = 0;

private:
    Info info;
    Defaults defaults;

    JUCE_DECLARE_NON_COPYABLE (Criterion)
};
