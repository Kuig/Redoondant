#pragma once

#include "Criterion.h"

/** Factories of the built-in criteria (each implemented in its own .cpp file). */
namespace Criteria
{
    std::unique_ptr<Criterion> createDuplicateFiles();
    std::unique_ptr<Criterion> createVersionedFiles();
    std::unique_ptr<Criterion> createJunkFolders();
    std::unique_ptr<Criterion> createArchiveMirrors();
    std::unique_ptr<Criterion> createLargeFiles();
    std::unique_ptr<Criterion> createDateClusters();
    std::unique_ptr<Criterion> createEmptyItems();
    std::unique_ptr<Criterion> createInstallersAndTempFiles();
    std::unique_ptr<Criterion> createIncompleteDownloads();
    std::unique_ptr<Criterion> createOldFiles();
    std::unique_ptr<Criterion> createManualInspection();

    /** All criteria, in the order shown in the sidebar. */
    std::vector<std::unique_ptr<Criterion>> createAll();
}
