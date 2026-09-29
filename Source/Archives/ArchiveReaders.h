#pragma once

#include "ArchiveReader.h"

/** Concrete readers, used by ArchiveReader::open(). */
namespace ArchiveReaders
{
    std::unique_ptr<ArchiveReader> createZipReader (const juce::File& file);
    std::unique_ptr<ArchiveReader> createTarReader (const juce::File& file, bool gzipped);
}
