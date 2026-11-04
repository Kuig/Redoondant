#pragma once

#include "Parameter.h"
#include "../Core/AnalysisResult.h"
#include "../Core/DefaultSelection.h"
#include "../Core/ScanContext.h"

struct PeerCriterion;

/** A rule that finds deletion candidates in a folder.

    Criteria are stateless: analyse() receives a snapshot of the parameter values and runs
    on a background thread, so it must not touch the UI (criteria that need other criteria get them as data). To add a new criterion, derive from
    this class and register it in CriterionRegistry.cpp; UI and persistence are generated.
*/
class Criterion
{
public:
    /** Properties of a criterion, combined with `|`. */
    enum class Trait : unsigned
    {
        none = 0,
        grouped = 1,                ///< Results are shown as separated groups.
        exhaustive = 2,             ///< Lists (almost) everything, so being listed means nothing by itself.
        recursiveByDefault = 4,
        showsFolders = 8,           ///< Folders can be listed (so "folders only" / "files only" make sense).
        usesPeers = 16,             ///< Combines the other criteria: analyse() gets them as peers.
        groupsOfCopies = 32,        ///< Each group holds copies of the same thing: an item is only removed while a kept one of its group remains.
        topLevelOnly = 64           ///< Only lists the items of the analysed folder itself: Recursive is not offered.
    };

    friend constexpr Trait operator| (Trait a, Trait b)    { return static_cast<Trait> (static_cast<unsigned> (a) | static_cast<unsigned> (b)); }

    struct Info
    {
        Info (juce::String idToUse, juce::String nameToUse, juce::String descriptionToUse, Trait traits = Trait::none)
            : id (std::move (idToUse)), name (std::move (nameToUse)), description (std::move (descriptionToUse)),
              grouped (has (traits, Trait::grouped)), exhaustive (has (traits, Trait::exhaustive)),
              recursiveByDefault (has (traits, Trait::recursiveByDefault)), showsFolders (has (traits, Trait::showsFolders)),
              usesPeers (has (traits, Trait::usesPeers)), groupsOfCopies (has (traits, Trait::groupsOfCopies)),
              topLevelOnly (has (traits, Trait::topLevelOnly)) {}

        juce::String id;            ///< Stable key, used for settings.
        juce::String name;
        juce::String description;
        bool grouped, exhaustive, recursiveByDefault, showsFolders, usesPeers, groupsOfCopies, topLevelOnly;     ///< See Trait.

    private:
        static bool has (Trait all, Trait one)  { return (static_cast<unsigned> (all) & static_cast<unsigned> (one)) != 0; }
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

    /** Criteria that combine others (Trait::usesPeers) override this: `peers` are the other criteria with their
        settings and results, as the UI knew them when the analysis started. The default ignores them.
    */
    virtual AnalysisResult analyse (const ScanContext& context, const ParameterSet& parameters,
                                    const std::vector<PeerCriterion>& peers) const
    {
        juce::ignoreUnused (peers);
        return analyse (context, parameters);
    }

private:
    Info info;
    Defaults defaults;

    JUCE_DECLARE_NON_COPYABLE (Criterion)
};
