#pragma once

#include "Criterion.h"
#include <optional>

/** Another criterion as seen by criteria that combine several (see "Multiple criteria"):
    its current settings and, if it has been analysed, its current list with the user's checks.
*/
struct PeerCriterion
{
    const Criterion* criterion = nullptr;
    ParameterSet parameters;
    std::optional<std::vector<FileEntry>> results;
    juce::File resultsRoot;     ///< The folder the results refer to.
};

/** Supplies the peers. It may be called from an analysis thread. */
using PeerSource = std::function<std::vector<PeerCriterion>()>;
