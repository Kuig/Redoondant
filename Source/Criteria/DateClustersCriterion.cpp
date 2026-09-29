#include "AllCriteria.h"
#include "../Core/FileScanner.h"
#include "../Core/Format.h"
#include "../Core/Grouping.h"

namespace
{
    class DateClusters final : public Criterion
    {
    public:
        DateClusters()
            : Criterion ({ "dateClusters", "Date clusters",
                           "Files and folders grouped by modification or creation date: a new group starts whenever "
                           "the gap from the previous item exceeds the given time. Newest groups first.",
                           true, false, true }) {}

        ParameterSet createParameters() const override
        {
            return { Parameter::choice ("date", "Date", { "Modified", "Created" }, 1),
                     Parameter::number ("gap", "Max. gap", 0.5, "hours"),
                     Parameter::number ("minItems", "Min. group size", 2, "items") };
        }

        AnalysisResult analyse (const ScanContext& context, const ParameterSet& parameters) const override
        {
            const bool useCreation = parameters.getChoice ("date") == 1;
            const auto timeOf = [useCreation] (const FileEntry& e) { return useCreation ? e.created : e.modified; };
            const auto minItems = (size_t) juce::jmax (1, (int) parameters.getNumber ("minItems"));

            auto clusters = Grouping::clusterByTimeGap (FileScanner::scan (context), timeOf,
                                                        juce::RelativeTime::hours (parameters.getNumber ("gap")), minItems);
            AnalysisResult result;

            for (auto it = clusters.rbegin(); it != clusters.rend(); ++it)
            {
                juce::int64 total = 0;

                for (const auto& e : *it)
                    total += e.size;

                result.groups.push_back ({ Format::dateRange (timeOf (it->front()), timeOf (it->back()))
                                             + "  (" + Format::count (it->size(), "item") + ", " + Format::size (total) + ")",
                                           std::move (*it) });
            }

            return result;
        }
    };
}

std::unique_ptr<Criterion> Criteria::createDateClusters()   { return std::make_unique<DateClusters>(); }
