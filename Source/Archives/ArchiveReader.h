#pragma once

#include "../Core/ScanContext.h"
#include <limits>
#include <optional>

/** Read-only access to the files stored in an archive, through libarchive.
    Supported: zip, 7z, rar (v4 and v5), tar (plain, gz, bz2, xz, zst), cab, iso (see isArchive()).
    Encrypted archives are reported as unreadable.
*/
namespace ArchiveReader
{
    struct Entry
    {
        juce::String path;          ///< Relative path, '/' separated.
        juce::int64 size = 0;       ///< Uncompressed size.
    };

    /** True if the file has a supported archive extension. */
    bool isArchive (const juce::File& file);

    /** The file name without its archive extension(s), e.g. "Photos.tar.gz" -> "Photos". */
    juce::String stemOf (const juce::File& file);

    /** Lists the files in the archive (folders and links are omitted), or nullopt if it can't be read (or the context
        was cancelled). Stops after `maxEntries` (some formats must be decompressed to be listed: a preview needn't read it all).
    */
    std::optional<std::vector<Entry>> list (const juce::File& archive, const ScanContext& context,
                                            size_t maxEntries = std::numeric_limits<size_t>::max());

    /** Streams every file of the archive, in archive order. The visitor returns false to stop early.
        Returns false if the archive couldn't be read completely.
    */
    bool forEachFile (const juce::File& archive, const ScanContext& context,
                      const std::function<bool (const Entry&, juce::InputStream&)>& visitor);
}
