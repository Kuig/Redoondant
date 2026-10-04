#pragma once

#include "ScanContext.h"
#include <optional>

/** Content fingerprints and comparisons used to detect identical data. */
namespace ContentHasher
{
    /** Cheap fingerprint (FNV-1a of the first and last 64 KB). Equal content implies equal quick hashes. */
    juce::uint64 quickHash (const juce::File& file);

    /** Fingerprint (FNV-1a) of what the first 64 KB read from the file contain, or nullopt if it can't be read.
        It never looks at the size the file system reports: equal content gives equal fingerprints on any file system.
    */
    std::optional<juce::uint64> startHash (const juce::File& file);

    /** SHA-256 of the whole file as hex, or an empty string if it can't be read completely (a read error half-way)
        or the scan was cancelled. `bytesRead` (optional) receives the number of bytes actually read.
    */
    juce::String checksum (const juce::File& file, const ScanContext& context, juce::int64* bytesRead = nullptr);

    /** Compares two streams byte by byte, stopping early on difference or cancellation. */
    bool streamsEqual (juce::InputStream& a, juce::InputStream& b, const ScanContext& context);
}
