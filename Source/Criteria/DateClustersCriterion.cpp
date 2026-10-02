#include "AllCriteria.h"
#include "../Core/DateKind.h"
#include "../Core/FileScanner.h"
#include "../Core/Format.h"
#include "../Core/Grouping.h"
#include "../Metadata/ContentDate.h"

namespace
{
    class DateClusters final : public Criterion
    {
    public:
        DateClusters()
            : Criterion ({ "dateClusters", "Date clusters",
                           "Files and folders grouped by creation or modification date, or by the date written in the content's metadata "
                           "(photo taken, document created...; folders and files without one are left out). "
                           "A new group starts whenever the gap from the previous item exceeds the given time. Newest groups first.",
                           true, true, false, true }) {}

        ParameterSet createParameters() const override
        {
            return { Parameter::choice ("date", "Date", { "File created", "File modified", "Content created" }, 0),
                     Parameter::number ("gap", "Max. gap", 0.5, "hours"),
                     Parameter::number ("minItems", "Min. group size", 2, "items") };
        }

        AnalysisResult analyse (const ScanContext& context, const ParameterSet& parameters) const override
        {
            const auto kind = (DateKind) parameters.getChoice ("date");
            const auto timeOf = [kind] (const FileEntry& e) { return dateOf (e, kind); };
            const auto minItems = (size_t) juce::jmax (1, (int) parameters.getNumber ("minItems"));

            auto entries = FileScanner::scan (context);

            if (kind == DateKind::content)
            {
                ContentDate::fill (entries, context);
                entries.erase (std::remove_if (entries.begin(), entries.end(), [] (const FileEntry& e) { return e.contentCreated.toMilliseconds() == 0; }),
                               entries.end());
            }

            auto clusters = Grouping::clusterByTimeGap (std::move (entries), timeOf,
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
