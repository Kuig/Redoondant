#include "ArchiveReaders.h"

namespace
{
    /** .zip archives, through juce::ZipFile (stored and deflated entries, no encryption). */
    class ZipArchiveReader final : public ArchiveReader
    {
    public:
        explicit ZipArchiveReader (const juce::File& file) : zip (file)
        {
            for (int i = 0; i < zip.getNumEntries(); ++i)
            {
                const auto* entry = zip.getEntry (i);
                const auto path = entry->filename.replaceCharacter ('\\', '/');

                if (path.endsWithChar ('/') || entry->isSymbolicLink)
                    continue;

                entries.push_back ({ path, entry->uncompressedSize });
                zipIndices.push_back (i);
            }
        }

        std::unique_ptr<juce::InputStream> openEntry (size_t index) override
        {
            return std::unique_ptr<juce::InputStream> (zip.createStreamForEntry (zipIndices[index]));
        }

    private:
        juce::ZipFile zip;
        std::vector<int> zipIndices;
    };
}

std::unique_ptr<ArchiveReader> ArchiveReaders::createZipReader (const juce::File& file)
{
    return std::make_unique<ZipArchiveReader> (file);
}
