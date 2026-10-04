#include "AllCriteria.h"
#include "../Archives/ArchiveReader.h"
#include "../Core/ContentHasher.h"
#include "../Core/FileScanner.h"
#include <map>
#include <set>

namespace
{
    /** Relative path ('/' separated, lowercase) -> file, for every file inside a folder. */
    std::map<juce::String, FileEntry> listFolder (const juce::File& folder, const ScanContext& context)
    {
        std::map<juce::String, FileEntry> files;

        for (auto& entry : FileScanner::scanFilesTree (folder, context))
        {
            const auto key = entry.file.getRelativePathFrom (folder).replaceCharacter ('\\', '/').toLowerCase();
            files.emplace (key, std::move (entry));
        }

        return files;
    }

    /** The name of the folder that holds all the entries of an archive ("Photos/" for "Photos/a.jpg", "Photos/b.jpg"), or empty. */
    juce::String commonRootPrefix (const std::vector<ArchiveReader::Entry>& entries)
    {
        const auto first = entries.front().path.toLowerCase();
        const auto prefix = first.substring (0, first.indexOfChar ('/') + 1);

        if (prefix.isEmpty())
            return {};

        return std::all_of (entries.begin(), entries.end(), [&] (const ArchiveReader::Entry& e) { return e.path.toLowerCase().startsWith (prefix); })
                 ? prefix : juce::String();
    }

    /** True if the archive holds exactly the files of the folder, byte for byte.
        An archive whose content sits in a single root folder is also accepted, if that folder is named like
        the extracted folder (or, with `anyRootName`, whatever its name).
    */
    bool haveSameContent (const juce::File& archiveFile, const std::vector<ArchiveReader::Entry>& entries,
                          const juce::File& folder, bool anyRootName, const ScanContext& context)
    {
        const auto folderFiles = listFolder (folder, context);

        if (folderFiles.size() != entries.size())
            return false;

        const auto rootPrefix = anyRootName ? commonRootPrefix (entries) : folder.getFileName().toLowerCase() + "/";

        for (const bool stripRoot : { false, true })
        {
            if (stripRoot && rootPrefix.isEmpty())
                break;

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

            // Every entry matches a file, and no two entries the same file (an archive can hold a path twice): then the
            // counts being equal means every file of the folder is in the archive.
            std::set<const FileEntry*> matched;

            if (! std::all_of (entries.begin(), entries.end(), [&] (const ArchiveReader::Entry& e)
                               {
                                   const auto* file = match (e);
                                   return file != nullptr && matched.insert (file).second;
                               }))
                continue;

            // Same listing: compare the bytes, in a single pass over the archive.
            bool identical = true;
            const bool readAll = ArchiveReader::forEachFile (archiveFile, context, [&] (const ArchiveReader::Entry& entry, juce::InputStream& data)
            {
                const auto* file = match (entry);
                juce::FileInputStream extracted (file != nullptr ? file->file : juce::File());
                identical = file != nullptr && extracted.openedOk() && ContentHasher::streamsEqual (data, extracted, context)
                              && ! extracted.getStatus().failed();
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
                           "Archives (zip, 7z, rar, tar, tar.gz/bz2/xz/zst, cab, iso) and a folder with exactly the same content, "
                           "by default only if the folder is named like the archive. By default the folder is checked.",
                           Trait::grouped | Trait::showsFolders | Trait::groupsOfCopies },
                        { DefaultSelection::folders }) {}

        ParameterSet createParameters() const override
        {
            return { Parameter::toggle ("ignoreName", "Ignore folder name (match by content only)", false) };
        }

        AnalysisResult analyse (const ScanContext& context, const ParameterSet& parameters) const override
        {
            const bool ignoreName = parameters.getBool ("ignoreName");
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

                const auto* sibling = siblingOf (archive, folders);

                if (! ignoreName && sibling == nullptr)
                    continue;       // Nothing to compare with: don't decompress the archive just to list it.

                const auto entries = ArchiveReader::list (archive.file, context);

                if (! entries.has_value() || entries->empty())
                    continue;

                std::vector<FileEntry> matches;

                for (const auto* candidate : ignoreName ? sameSizeFolders (*entries, folders) : std::vector<const FileEntry*> { sibling })
                    if (haveSameContent (archive.file, *entries, candidate->file, ignoreName, context))
                        matches.push_back (*candidate);

                if (matches.empty())
                    continue;

                auto title = archive.name() + "  =  " + matches.front().name() + "/";

                if (matches.size() > 1)
                    title << "  (+" << juce::String ((int) matches.size() - 1) << " more)";

                std::vector<FileEntry> items { archive };

                for (auto& extracted : matches)
                    items.push_back (std::move (extracted));

                result.groups.push_back ({ title, std::move (items) });
            }

            return result;
        }

    private:
        /** The folder named like the archive next to it, if it was scanned and all its content could be listed. */
        static const FileEntry* siblingOf (const FileEntry& archive, const std::map<juce::String, FileEntry>& folders)
        {
            const auto sibling = folders.find (archive.file.getSiblingFile (ArchiveReader::stemOf (archive.file)).getFullPathName());
            return sibling != folders.end() && ! sibling->second.contentUnknown ? &sibling->second : nullptr;
        }

        /** Ignoring names: every folder with the same number of files and total size (and all its content listed). */
        static std::vector<const FileEntry*> sameSizeFolders (const std::vector<ArchiveReader::Entry>& entries,
                                                              const std::map<juce::String, FileEntry>& folders)
        {
            std::vector<const FileEntry*> candidates;
            juce::int64 total = 0;

            for (const auto& e : entries)
                total += e.size;

            // A folder with something unreadable or a link inside could hold more than what was compared.
            for (const auto& [path, folder] : folders)
                if (folder.fileCount == (int) entries.size() && folder.size == total && ! folder.contentUnknown)
                    candidates.push_back (&folder);

            return candidates;
        }
    };
}

std::unique_ptr<Criterion> Criteria::createArchiveMirrors()   { return std::make_unique<ArchiveMirrors>(); }
