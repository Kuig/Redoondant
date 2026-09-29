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
        auto archive = ArchiveReader::open (archiveFile);

        if (archive == nullptr || archive->getEntries().empty())
            return false;

        const auto folderFiles = listFolder (folder, context);
        const auto& entries = archive->getEntries();

        if (folderFiles.size() != entries.size())
            return false;

        const auto rootPrefix = folder.getFileName().toLowerCase() + "/";

        for (const bool stripRoot : { false, true })
        {
            std::vector<const FileEntry*> matches;

            for (const auto& entry : entries)
            {
                auto key = entry.path.toLowerCase();

                if (stripRoot)
                {
                    if (! key.startsWith (rootPrefix))
                        break;

                    key = key.substring (rootPrefix.length());
                }

                const auto found = folderFiles.find (key);

                if (found == folderFiles.end() || found->second.size != entry.size)
                    break;

                matches.push_back (&found->second);
            }

            if (matches.size() != entries.size())
                continue;

            for (size_t i = 0; i < entries.size(); ++i)
            {
                auto archived = archive->openEntry (i);
                juce::FileInputStream extracted (matches[i]->file);

                if (archived == nullptr || ! extracted.openedOk() || ! ContentHasher::streamsEqual (*archived, extracted, context))
                    return false;
            }

            return true;
        }

        return false;
    }

    class ArchiveMirrors final : public Criterion
    {
    public:
        ArchiveMirrors()
            : Criterion ({ "archives", "Archives & extracted folders",
                           "Archives (.zip, .tar, .tar.gz, .tgz) next to a folder with the same name and exactly the same "
                           "content. The folder is checked by default.",
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
