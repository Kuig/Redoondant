#include "ArchiveReaders.h"

namespace
{
    enum class Format { zip, tar, tarGz };

    struct Suffix
    {
        const char* text;
        Format format;
    };

    // Longest suffixes first, so ".tar.gz" wins over ".gz".
    const Suffix suffixes[] =
    {
        { ".tar.gz", Format::tarGz },
        { ".tgz",    Format::tarGz },
        { ".tar",    Format::tar },
        { ".zip",    Format::zip },
    };

    const Suffix* findSuffix (const juce::File& file)
    {
        for (const auto& suffix : suffixes)
            if (file.getFileName().endsWithIgnoreCase (suffix.text))
                return &suffix;

        return nullptr;
    }
}

bool ArchiveReader::isArchive (const juce::File& file)
{
    return findSuffix (file) != nullptr;
}

juce::String ArchiveReader::stemOf (const juce::File& file)
{
    const auto name = file.getFileName();

    if (const auto* suffix = findSuffix (file))
        return name.dropLastCharacters ((int) std::strlen (suffix->text));

    return file.getFileNameWithoutExtension();
}

std::unique_ptr<ArchiveReader> ArchiveReader::open (const juce::File& file)
{
    const auto* suffix = findSuffix (file);

    if (suffix == nullptr || ! file.existsAsFile())
        return nullptr;

    switch (suffix->format)
    {
        case Format::zip:   return ArchiveReaders::createZipReader (file);
        case Format::tar:   return ArchiveReaders::createTarReader (file, false);
        case Format::tarGz: return ArchiveReaders::createTarReader (file, true);
    }

    return nullptr;
}
