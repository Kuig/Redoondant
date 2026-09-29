#include "ArchiveReader.h"

namespace
{
    // Longest suffixes first, so ".tar.gz" wins over ".gz".
    const char* const suffixes[] =
    {
        ".tar.gz", ".tar.bz2", ".tar.xz", ".tar.zst",
        ".tgz", ".tbz2", ".txz", ".tar",
        ".zip", ".7z", ".rar", ".cab", ".iso",
    };

    const char* findSuffix (const juce::File& file)
    {
        for (const auto* suffix : suffixes)
            if (file.getFileName().endsWithIgnoreCase (suffix))
                return suffix;

        return nullptr;
    }
}

bool ArchiveReader::isArchive (const juce::File& file)
{
    return findSuffix (file) != nullptr;
}

juce::String ArchiveReader::stemOf (const juce::File& file)
{
    if (const auto* suffix = findSuffix (file))
        return file.getFileName().dropLastCharacters ((int) std::strlen (suffix));

    return file.getFileNameWithoutExtension();
}
