#pragma once

#include "ScanContext.h"

/** Content fingerprints and comparisons used to detect identical data. */
namespace ContentHasher
{
    /** Cheap fingerprint (FNV-1a of the first and last 64 KB). Equal content implies equal quick hashes. */
    juce::uint64 quickHash (const juce::File& file);

    /** SHA-256 of the whole file as hex, or an empty string if it can't be read or the scan was cancelled. */
    juce::String checksum (const juce::File& file, const ScanContext& context);

    /** Compares two streams byte by byte, stopping early on difference or cancellation. */
    bool streamsEqual (juce::InputStream& a, juce::InputStream& b, const ScanContext& context);

    /** Compares two files byte by byte (false if either can't be read). */
    bool filesEqual (const juce::File& a, const juce::File& b, const ScanContext& context);
}
