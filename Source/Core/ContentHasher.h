#pragma once

#include "ScanContext.h"

/** Content fingerprints and comparisons used to detect identical data. */
namespace ContentHasher
{
    /** Cheap fingerprint (FNV-1a of the first and last 64 KB). Equal content implies equal quick hashes. */
    juce::uint64 quickHash (const juce::File& file);

    /** Compares two streams byte by byte, stopping early on difference or cancellation. */
    bool streamsEqual (juce::InputStream& a, juce::InputStream& b, const ScanContext& context);
}
