#pragma once

#include <JuceHeader.h>

/** Read-only access to the files stored in an archive.
    Supported formats: .zip, .tar, .tar.gz / .tgz (see ArchiveReader::isArchive).
*/
class ArchiveReader
{
public:
    struct Entry
    {
        juce::String path;          ///< Relative path, '/' separated.
        juce::int64 size = 0;       ///< Uncompressed size.
    };

    virtual ~ArchiveReader() = default;

    /** The files in the archive (folders and links are not listed). */
    const std::vector<Entry>& getEntries() const noexcept      { return entries; }

    /** Opens the data of an entry. For streamed formats (tar.gz) entries are fastest
        when opened in increasing order, and each stream must be released before opening the next.
    */
    virtual std::unique_ptr<juce::InputStream> openEntry (size_t index) = 0;

    /** True if the file has a supported archive extension. */
    static bool isArchive (const juce::File& file);

    /** The file name without its archive extension(s), e.g. "Photos.tar.gz" -> "Photos". */
    static juce::String stemOf (const juce::File& file);

    /** Opens an archive, or returns nullptr if the format is unsupported or the file is unreadable. */
    static std::unique_ptr<ArchiveReader> open (const juce::File& file);

protected:
    std::vector<Entry> entries;
};
