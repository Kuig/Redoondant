#include "AllCriteria.h"
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
                           "Files with identical content: same size then same bytes, or the same SHA-256 checksum (slower, reads every file). "
                           "In each group the file with the shortest name is kept, the others are checked.",
                           true }) {}

        ParameterSet createParameters() const override
        {
            auto algorithm = Parameter::choice ("algorithm", "Algorithm", { "Size + content", "Checksum" }, 0);
            algorithm.editorWidth = 130;
            return { algorithm };
        }

        AnalysisResult analyse (const ScanContext& context, const ParameterSet& parameters) const override
        {
            auto files = FileScanner::filesOnly (FileScanner::scan (context, { false }));
            files.erase (std::remove_if (files.begin(), files.end(), [] (const FileEntry& e) { return e.size == 0; }), files.end());

            AnalysisResult result;
            const auto add = [&result] (std::vector<FileEntry> identical)
            {
                Grouping::selectAllButBest (identical, keepShortestName);
                result.groups.push_back ({ Format::count (identical.size(), "identical file") + ", "
                                             + Format::size (identical.front().size) + " each",
                                           std::move (identical) });
            };

            if (parameters.getChoice ("algorithm") == 1)
                findByChecksum (std::move (files), context, add);
            else
                findBySizeAndContent (std::move (files), context, add);

            // Most space wasted first.
            std::sort (result.groups.begin(), result.groups.end(), [] (const ResultGroup& a, const ResultGroup& b)
            {
                return wasted (a) > wasted (b);
            });

            return result;
        }

    private:
        using Emit = std::function<void (std::vector<FileEntry>)>;

        /** Same size, then same start/end fingerprint, then a byte-by-byte comparison. */
        static void findBySizeAndContent (std::vector<FileEntry> files, const ScanContext& context, const Emit& emit)
        {
            auto sameSize = Grouping::groupBy (std::move (files), [] (const FileEntry& e) { return e.size; });

            for (size_t i = 0; i < sameSize.size() && ! context.isCancelled(); ++i)
            {
                context.progress ((double) i / (double) sameSize.size(), "Comparing file contents...");

                auto sameStart = Grouping::groupBy (std::move (sameSize[i]),
                                                    [] (const FileEntry& e) { return ContentHasher::quickHash (e.file); });

                for (auto& candidates : sameStart)
                    for (auto& identical : partitionByContent (std::move (candidates), context))
                        emit (std::move (identical));
            }
        }

        /** Checksums of every file; files are equal when their checksums are. */
        static void findByChecksum (std::vector<FileEntry> files, const ScanContext& context, const Emit& emit)
        {
            std::map<juce::String, std::vector<FileEntry>> byChecksum;

            for (size_t i = 0; i < files.size() && ! context.isCancelled(); ++i)
            {
                context.progress ((double) i / (double) files.size(), "Checksum of " + files[i].name() + "...");
                const auto sum = ContentHasher::checksum (files[i].file, context);

                if (sum.isNotEmpty())
                    byChecksum[sum].push_back (std::move (files[i]));
            }

            for (auto& [sum, same] : byChecksum)
                if (same.size() > 1 && ! context.isCancelled())
                    emit (std::move (same));
        }

        /** Splits same-size files into sets of byte-identical files (sets of one are dropped). */
        static Grouping::Groups partitionByContent (std::vector<FileEntry> files, const ScanContext& context)
        {
            Grouping::Groups partitions;

            for (auto& file : files)
            {
                const auto match = std::find_if (partitions.begin(), partitions.end(), [&] (const std::vector<FileEntry>& p)
                {
                    return ContentHasher::filesEqual (p.front().file, file.file, context);
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

        static bool keepShortestName (const FileEntry& a, const FileEntry& b)
        {
            const auto la = a.name().length(), lb = b.name().length();
            return la != lb ? la < lb : a.created < b.created;
        }

        static juce::int64 wasted (const ResultGroup& group)
        {
            return group.items.front().size * (juce::int64) (group.items.size() - 1);
        }
    };
}

std::unique_ptr<Criterion> Criteria::createDuplicateFiles()   { return std::make_unique<DuplicateFiles>(); }
