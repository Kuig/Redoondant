#pragma once

/** The values of the choice parameters of criteria (a choice's value is the index of its entry; the first is the default),
    so that criteria and tests don't repeat bare indices.
*/
namespace ParameterChoices
{
    enum class DuplicateAlgorithm { sizeAndContent, checksum };

    enum class OverlapSource     { existingResults, runAllCriteria };
    enum class OverlapMarked     { checkedItems, listedItems };
    enum class OverlapFolderMode { topFolders, mostlyCandidate };
}
