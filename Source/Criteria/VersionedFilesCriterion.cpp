#include "AllCriteria.h"
#include "../Core/FileScanner.h"
#include "../Core/Format.h"
#include "../Core/Grouping.h"

namespace
{
    class VersionedFiles final : public Criterion
    {
    public:
        VersionedFiles()
            : Criterion ({ "versions", "File versions",
                           "Files in the same folder, with the same extension, whose names start the same way but end "
                           "differently (e.g. report.pdf, report (1).pdf, report_final.pdf). "
                           "The most recently modified file of each group is kept.",
                           true }) {}

        ParameterSet createParameters() const override
        {
            return { Parameter::number ("minRoot", "Shared start at least", 4, "chars"),
                     Parameter::number ("minRatio", "and at least", 60, "% of the name") };
        }

        AnalysisResult analyse (const ScanContext& context, const ParameterSet& parameters) const override
        {
            const auto minRoot = juce::jmax (1, (int) parameters.getNumber ("minRoot"));
            const auto minRatio = juce::jlimit (0.0, 1.0, parameters.getNumber ("minRatio") / 100.0);

            auto files = FileScanner::filesOnly (FileScanner::scan (context, { false }));

            auto sameFolderAndType = Grouping::groupBy (std::move (files), [] (const FileEntry& e)
            {
                return e.file.getParentDirectory().getFullPathName() + "|" + e.file.getFileExtension().toLowerCase();
            });

            AnalysisResult result;

            for (auto& candidates : sameFolderAndType)
            {
                const auto extension = candidates.front().file.getFileExtension();

                for (auto& cluster : Grouping::clusterBySharedRoot (std::move (candidates), minRoot, minRatio))
                {
                    Grouping::selectAllButBest (cluster.items, [] (const FileEntry& a, const FileEntry& b) { return a.modified > b.modified; });
                    result.groups.push_back ({ cluster.root + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa6")) + extension
                                                 + "  (" + Format::count (cluster.items.size(), "version") + ")",
                                               std::move (cluster.items) });
                }
            }

            return result;
        }
    };
}

std::unique_ptr<Criterion> Criteria::createVersionedFiles()   { return std::make_unique<VersionedFiles>(); }
