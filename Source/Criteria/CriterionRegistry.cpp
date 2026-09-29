#include "AllCriteria.h"

std::vector<std::unique_ptr<Criterion>> Criteria::createAll()
{
    std::vector<std::unique_ptr<Criterion>> all;

    all.push_back (createDuplicateFiles());
    all.push_back (createVersionedFiles());
    all.push_back (createArchiveMirrors());
    all.push_back (createJunkFolders());
    all.push_back (createLargeFiles());
    all.push_back (createDateClusters());
    all.push_back (createEmptyItems());
    all.push_back (createIncompleteDownloads());
    all.push_back (createInstallersAndTempFiles());
    all.push_back (createOldFiles());
    all.push_back (createManualInspection());

    return all;
}
