#include "AllCriteria.h"
#include "ParameterChoices.h"
#include "../Core/ContentHasher.h"
#include "../Core/FileScanner.h"
#include "../Core/Format.h"
#include "../Core/Grouping.h"
#include <map>

namespace
{
    class DuplicateFiles final : public Criterion
    {
    public:
        DuplicateFiles()
            : Criterion ({ "duplicates", "Duplicate files",
                           "Files with identical content: same size then same bytes, or the same SHA-256 checksum of what is read (slower; "
                           "ignores the sizes the file system reports, which can differ for identical content). "
                           "By default the file with the shortest name of each group is kept and the others are checked.",
                           Trait::grouped | Trait::groupsOfCopies },
                        { DefaultSelection::shortestName, Column::name }) {}

        ParameterSet createParameters() const override
        {
            auto algorithm = Parameter::choice ("algorithm", "Algorithm", { "Size + content", "Checksum" }, 0);
            algorithm.editorWidth = 130;
            return { algorithm };
        }

        AnalysisResult analyse (const ScanContext& context, const ParameterSet& parameters) const override
        {
            FileScanner::Options options { false };
            options.shouldDescend = [] (const FileEntry& e) { return ! FileScanner::isVersionControlFolder (e); };     // Its objects belong to the repository.

            auto files = FileScanner::filesOnly (FileScanner::scan (context, options));
            AnalysisResult result;

            const auto add = [&result] (std::vector<FileEntry> identical, juce::int64 bytesEach)
            {
                result.groups.push_back ({ Format::count (identical.size(), "identical file") + ", " + Format::size (bytesEach) + " each",
                                           std::move (identical) });
            };

            if (parameters.getEnum<ParameterChoices::DuplicateAlgorithm> ("algorithm") == ParameterChoices::DuplicateAlgorithm::checksum)
                findByChecksum (std::move (files), context, add);
            else
                findBySizeAndContent (FileScanner::keepIf (std::move (files), [] (const FileEntry& e) { return e.size != 0; }), context, add);

            // Most space wasted first.
            std::sort (result.groups.begin(), result.groups.end(), [] (const ResultGroup& a, const ResultGroup& b)
            {
                return a.wastedSize() > b.wastedSize();
            });

            return result;
        }

    private:
        using Emit = std::function<void (std::vector<FileEntry>, juce::int64 bytesEach)>;

        /** Groups bigger than this are compared by checksum rather than by reading them all side by side. */
        static constexpr size_t maxComparedTogether = 256;

        /** Same size, then same start/end fingerprint, then the same bytes. */
        static void findBySizeAndContent (std::vector<FileEntry> files, const ScanContext& context, const Emit& emit)
        {
            auto sameSize = Grouping::groupBy (std::move (files), [] (const FileEntry& e) { return e.size; });

            for (size_t i = 0; i < sameSize.size() && ! context.isCancelled(); ++i)
            {
                context.progress ((double) i / (double) sameSize.size(), "Comparing file contents...");

                auto sameStart = Grouping::groupBy (std::move (sameSize[i]),
                                                    [] (const FileEntry& e) { return ContentHasher::quickHash (e.file); });

                for (auto& candidates : sameStart)
                {
                    const auto bytes = candidates.front().size;

                    for (auto& identical : candidates.size() > maxComparedTogether ? byChecksum (std::move (candidates), context)
                                                                                     : partitionByContent (std::move (candidates), context))
                        emit (std::move (identical), bytes);
                }
            }
        }

        /** Checksums of the content actually read: the size the file system reports is never used, since it can differ
            for identical content from one file system to another. Only files whose first 64 KB read alike are hashed.
        */
        static void findByChecksum (std::vector<FileEntry> files, const ScanContext& context, const Emit& emit)
        {
            std::map<juce::uint64, std::vector<FileEntry>> byStart;

            for (size_t i = 0; i < files.size() && ! context.isCancelled(); ++i)
            {
                if (i % 100 == 0)
                    context.progress ((double) i / (double) files.size() * 0.2, "Reading the start of the files...");

                if (const auto start = ContentHasher::startHash (files[i].file))
                    byStart[*start].push_back (std::move (files[i]));
            }

            std::vector<FileEntry> candidates;

            for (auto& [start, same] : byStart)
                if (same.size() > 1)
                    candidates.insert (candidates.end(), std::make_move_iterator (same.begin()), std::make_move_iterator (same.end()));

            for (auto& [identical, bytes] : byChecksumWithSize (std::move (candidates), context, 0.2))
                if (bytes > 0)      // Empty files are all "identical" but are not duplicates of anything.
                    emit (std::move (identical), bytes);
        }

        /** Files grouped by the SHA-256 and the length of what was read from them (groups of one are dropped). */
        static std::vector<std::pair<std::vector<FileEntry>, juce::int64>> byChecksumWithSize (std::vector<FileEntry> files, const ScanContext& context,
                                                                                                double progressStart = -1.0)
        {
            std::map<juce::String, std::pair<std::vector<FileEntry>, juce::int64>> bySum;

            for (size_t i = 0; i < files.size() && ! context.isCancelled(); ++i)
            {
                if (progressStart >= 0)
                    context.progress (progressStart + (1.0 - progressStart) * (double) i / (double) files.size(), "Checksum of " + files[i].name() + "...");

                juce::int64 bytes = 0;

                if (const auto sum = ContentHasher::checksum (files[i].file, context, &bytes); sum.isNotEmpty())
                {
                    auto& group = bySum[sum + ":" + juce::String (bytes)];
                    group.first.push_back (std::move (files[i]));
                    group.second = bytes;
                }
            }

            std::vector<std::pair<std::vector<FileEntry>, juce::int64>> groups;

            for (auto& [key, group] : bySum)
                if (group.first.size() > 1 && ! context.isCancelled())
                    groups.push_back (std::move (group));

            return groups;
        }

        static Grouping::Groups byChecksum (std::vector<FileEntry> files, const ScanContext& context)
        {
            Grouping::Groups groups;

            for (auto& [identical, bytes] : byChecksumWithSize (std::move (files), context))
                groups.push_back (std::move (identical));

            return groups;
        }

        /** Splits same-size files into sets of byte-identical files (sets of one are dropped), reading all of them side by
            side in large blocks: each file is read once, and a set splits as soon as a block differs.
        */
        static Grouping::Groups partitionByContent (std::vector<FileEntry> files, const ScanContext& context)
        {
            struct Candidate
            {
                FileEntry entry;
                std::unique_ptr<juce::FileInputStream> in;
                juce::HeapBlock<char> block;
                int length = 0;
            };

            // Large blocks keep a hard disk streaming instead of seeking between the files; the total memory stays bounded.
            const int blockSize = juce::jlimit (64 * 1024, 1024 * 1024, (int) ((32 * 1024 * 1024) / juce::jmax ((size_t) 1, files.size())));
            std::vector<std::vector<Candidate>> pending (1);

            for (auto& file : files)
            {
                Candidate c;
                c.in = std::make_unique<juce::FileInputStream> (file.file);
                c.entry = std::move (file);

                if (c.in->openedOk())
                {
                    c.block.malloc ((size_t) blockSize);
                    pending.front().push_back (std::move (c));
                }
            }

            Grouping::Groups identical;

            while (! pending.empty() && ! context.isCancelled())
            {
                auto set = std::move (pending.back());
                pending.pop_back();

                if (set.size() < 2)
                    continue;

                for (auto& c : set)
                {
                    c.length = c.in->read (c.block, blockSize);

                    if (c.in->getStatus().failed())
                        c.length = -1;      // Unreadable from here on: not known to be identical to anything.
                }

                std::vector<std::vector<Candidate>> parts;

                for (auto& c : set)
                {
                    if (c.length < 0)
                        continue;

                    const auto same = std::find_if (parts.begin(), parts.end(), [&] (const std::vector<Candidate>& part)
                    {
                        const auto& other = part.front();
                        return other.length == c.length && std::memcmp (other.block, c.block, (size_t) c.length) == 0;
                    });

                    if (same == parts.end())
                    {
                        parts.emplace_back();
                        parts.back().push_back (std::move (c));
                    }
                    else
                    {
                        same->push_back (std::move (c));
                    }
                }

                for (auto& part : parts)
                {
                    if (part.size() < 2)
                        continue;

                    if (part.front().length == 0)       // All at their end together: identical.
                    {
                        std::vector<FileEntry> entries;

                        for (auto& c : part)
                            entries.push_back (std::move (c.entry));

                        identical.push_back (std::move (entries));
                    }
                    else
                    {
                        pending.push_back (std::move (part));
                    }
                }
            }

            return identical;
        }
    };
}

std::unique_ptr<Criterion> Criteria::createDuplicateFiles()   { return std::make_unique<DuplicateFiles>(); }
