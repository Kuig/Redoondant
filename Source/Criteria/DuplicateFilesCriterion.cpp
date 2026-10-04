#include "AllCriteria.h"
#include "../Core/ContentHasher.h"
#include "../Core/FileScanner.h"
#include "../Core/Format.h"
#include "../Core/Grouping.h"

namespace
{
    class DuplicateFiles final : public Criterion
    {
    public:
        DuplicateFiles()
            : Criterion ({ "duplicates", "Duplicate files",
                           "Files with identical content: same size, then the same quick fingerprint (first and last 64 KB), then the same bytes "
                           "(by default leaving out the data of version control and installed dependencies). "
                           "By default the file with the shortest name of each group is kept and the others are checked.",
                           Trait::grouped | Trait::groupsOfCopies },
                        { DefaultSelection::shortestName, Column::name }) {}

        ParameterSet createParameters() const override
        {
            return { Criteria::skipVersionControl(), Criteria::skipDependencies() };
        }

        AnalysisResult analyse (const ScanContext& context, const ParameterSet& parameters) const override
        {
            FileScanner::Options options { false };
            const bool skipVersionControl = parameters.getBool ("skipVersionControl");     // Its objects belong to the repository.
            const bool skipDependencies = parameters.getBool ("skipDependencies");

            options.shouldDescend = [skipVersionControl, skipDependencies] (const FileEntry& e)
            {
                return ! (skipVersionControl && FileScanner::isVersionControlFolder (e))
                    && ! (skipDependencies && FileScanner::isDependencyFolder (e));
            };

            AnalysisResult result;

            for (auto& identical : findIdentical (FileScanner::filesOnly (FileScanner::scan (context, options)), context))
                result.groups.push_back ({ Format::count (identical.size(), "identical file") + ", " + Format::size (identical.front().size) + " each",
                                           std::move (identical) });

            // Most space wasted first.
            std::sort (result.groups.begin(), result.groups.end(), [] (const ResultGroup& a, const ResultGroup& b)
            {
                return a.wastedSize() > b.wastedSize();
            });

            return result;
        }

    private:
        /** Groups bigger than this are compared one file at a time against each set found so far, instead of side by
            side (which keeps a file open and a block in memory for each of them).
        */
        static constexpr size_t maxComparedTogether = 256;

        /** Byte-identical files with the same non-zero size: same size, then the same start/end fingerprint, then the
            same bytes (the reading stops at the first difference).
        */
        static Grouping::Groups findIdentical (std::vector<FileEntry> files, const ScanContext& context)
        {
            auto sameSize = Grouping::groupBy (FileScanner::keepIf (std::move (files), [] (const FileEntry& e) { return e.size != 0; }),
                                               [] (const FileEntry& e) { return e.size; });
            Grouping::Groups identical;

            for (size_t i = 0; i < sameSize.size() && ! context.isCancelled(); ++i)
            {
                context.progress ((double) i / (double) sameSize.size(), "Comparing file contents...");

                auto sameStart = Grouping::groupBy (std::move (sameSize[i]),
                                                    [] (const FileEntry& e) { return ContentHasher::quickHash (e.file); });

                for (auto& candidates : sameStart)
                    for (auto& group : candidates.size() > maxComparedTogether ? partitionOneByOne (std::move (candidates), context)
                                                                                : partitionByContent (std::move (candidates), context))
                        identical.push_back (std::move (group));
            }

            return identical;
        }

        /** Like partitionByContent, comparing each file with the first of each set found so far. */
        static Grouping::Groups partitionOneByOne (std::vector<FileEntry> files, const ScanContext& context)
        {
            Grouping::Groups partitions;

            for (auto& file : files)
            {
                const auto match = std::find_if (partitions.begin(), partitions.end(), [&] (const std::vector<FileEntry>& p)
                {
                    juce::FileInputStream a (p.front().file), b (file.file);
                    return a.openedOk() && b.openedOk() && ContentHasher::streamsEqual (a, b, context)
                        && ! a.getStatus().failed() && ! b.getStatus().failed();
                });

                if (match != partitions.end())
                    match->push_back (std::move (file));
                else
                    partitions.push_back ({ std::move (file) });
            }

            partitions.erase (std::remove_if (partitions.begin(), partitions.end(),
                                              [] (const std::vector<FileEntry>& p) { return p.size() < 2; }),
                              partitions.end());
            return partitions;
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
