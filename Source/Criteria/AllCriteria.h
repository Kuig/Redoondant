#pragma once

#include "Peers.h"
#include "../Metadata/Metadata.h"

/** Factories of the built-in criteria (each implemented in its own .cpp file). */
namespace Criteria
{
    using MetadataSource = std::function<Metadata (const juce::File&)>;

    std::unique_ptr<Criterion> createDuplicateFiles();
    std::unique_ptr<Criterion> createVersionedFiles();
    std::unique_ptr<Criterion> createJunkFolders();
    std::unique_ptr<Criterion> createArchiveMirrors();
    std::unique_ptr<Criterion> createLargeFiles();
    std::unique_ptr<Criterion> createDateClusters();
    std::unique_ptr<Criterion> createEmptyItems();
    std::unique_ptr<Criterion> createInstallersAndJunkFiles();
    std::unique_ptr<Criterion> createIncompleteDownloads();
    std::unique_ptr<Criterion> createOldFiles();
    std::unique_ptr<Criterion> createManualInspection();
    std::unique_ptr<Criterion> createSameName();

    /** @param source   where metadata comes from (default: MetadataReader::read); tests inject fakes. */
    std::unique_ptr<Criterion> createSameContent (MetadataSource source = {});

    /** @param peers    the other criteria, with their settings and results (supplied by the UI). */
    std::unique_ptr<Criterion> createMultipleCriteria (PeerSource peers);

    /** All criteria, in the order shown in the sidebar. */
    std::vector<std::unique_ptr<Criterion>> createAll (PeerSource peers);
}
