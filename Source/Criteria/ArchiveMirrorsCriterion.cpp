#include "AllCriteria.h"
#include "../Archives/ArchiveReader.h"
#include "../Core/ContentHasher.h"
#include "../Core/FileScanner.h"
#include <map>

namespace
{
    /** Relative path ('/' separated, lowercase) -> file, for every file inside a folder. */
    std::map<juce::String, FileEntry> listFolder (const juce::File& folder, const ScanContext& context)
    {
        std::map<juce::String, FileEntry> files;

        for (auto& entry : FileScanner::filesOnly (FileScanner::scanTree (folder, context, { false })))
        {
            const auto key = entry.file.getRelativePathFrom (folder).replaceCharacter ('\\', '/').toLowerCase();
            files.emplace (key, std::move (entry));
        }

        return files;
    }

    /** True if the archive holds exactly the files of the folder, byte for byte.
        An archive whose content sits in a single root folder named like the folder is also accepted.
    */
    bool haveSameContent (const juce::File& archiveFile, const juce::File& folder, const ScanContext& context)
    {
        const auto entries = ArchiveReader::list (archiveFile, context);

        if (! entries.has_value() || entries->empty())
            return false;

        const auto folderFiles = listFolder (folder, context);

        if (folderFiles.size() != entries->size())
            return false;

        const auto rootPrefix = folder.getFileName().toLowerCase() + "/";

        for (const bool stripRoot : { false, true })
        {
            /** The folder file matching an archive entry (same relative path and size), or nullptr. */
            const auto match = [&] (const ArchiveReader::Entry& entry) -> const FileEntry*
            {
                auto key = entry.path.toLowerCase();

                if (stripRoot)
                {
                    if (! key.startsWith (rootPrefix))
                        return nullptr;

                    key = key.substring (rootPrefix.length());
                }

                const auto found = folderFiles.find (key);
                return found != folderFiles.end() && found->second.size == entry.size ? &found->second : nullptr;
            };

            if (! std::all_of (entries->begin(), entries->end(), [&] (const ArchiveReader::Entry& e) { return match (e) != nullptr; }))
                continue;

            // Same listing: compare the bytes, in a single pass over the archive.
            bool identical = true;
            const bool readAll = ArchiveReader::forEachFile (archiveFile, context, [&] (const ArchiveReader::Entry& entry, juce::InputStream& data)
            {
                const auto* file = match (entry);
                juce::FileInputStream extracted (file != nullptr ? file->file : juce::File());
                identical = file != nullptr && extracted.openedOk() && ContentHasher::streamsEqual (data, extracted, context);
                return identical;
            });

            return readAll && identical;
        }

        return false;
    }

    class ArchiveMirrors final : public Criterion
    {
    public:
        ArchiveMirrors()
            : Criterion ({ "archives", "Archives & extracted folders",
                           "Archives (zip, 7z, rar, tar, tar.gz/bz2/xz/zst, cab, iso) next to a folder with the same name "
                           "and exactly the same content. The folder is checked by default.",
                           true }) {}

        AnalysisResult analyse (const ScanContext& context, const ParameterSet&) const override
        {
            std::map<juce::String, FileEntry> folders;
            std::vector<FileEntry> archives;

            for (auto& entry : FileScanner::scan (context))
            {
                if (entry.isDirectory)
                    folders.emplace (entry.file.getFullPathName(), std::move (entry));
                else if (ArchiveReader::isArchive (entry.file))
                    archives.push_back (std::move (entry));
            }

            AnalysisResult result;

            for (size_t i = 0; i < archives.size() && ! context.isCancelled(); ++i)
            {
                auto& archive = archives[i];
                context.progress ((double) i / (double) archives.size(), "Comparing " + archive.name() + "...");

                const auto sibling = archive.file.getSiblingFile (ArchiveReader::stemOf (archive.file));
                const auto folder = folders.find (sibling.getFullPathName());

                if (folder == folders.end() || ! haveSameContent (archive.file, sibling, context))
                    continue;

                auto extracted = folder->second;
                extracted.selected = true;
                result.groups.push_back ({ archive.name() + "  =  " + extracted.name() + "/", { archive, extracted } });
            }

            return result;
        }
    };
}

std::unique_ptr<Criterion> Criteria::createArchiveMirrors()   { return std::make_unique<ArchiveMirrors>(); }
