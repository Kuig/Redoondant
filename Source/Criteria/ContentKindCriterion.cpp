#include "AllCriteria.h"
#include "../Core/ContentKind.h"
#include "../Core/FileScanner.h"
#include "../Core/Format.h"
#include <map>

namespace
{
    class ContentKindCriterion final : public Criterion
    {
    public:
        ContentKindCriterion()
            : Criterion ({ "contentKind", "Content kind",
                           "Each item of the folder (file, folder or archive) by what it holds, judged from the files inside it: "
                           "source code, applications, audio plugins, fonts, pictures, graphics, video, music, samples, "
                           "media projects, documents, data or other. Each kind is a group.",
                           Trait::grouped | Trait::showsFolders | Trait::exhaustive | Trait::topLevelOnly },
                         { DefaultSelection::none, Column::size, false }) {}

        AnalysisResult analyse (const ScanContext& context, const ParameterSet&) const override
        {
            auto topLevel = context;
            topLevel.recursive = false;     // Also when run by the criteria that combine the others.

            auto entries = FileScanner::scan (topLevel);
            ContentKinds::Classifier classifier (context);
            std::map<ContentKind, std::vector<FileEntry>> byKind;

            for (size_t i = 0; i < entries.size() && ! context.isCancelled(); ++i)
            {
                auto& entry = entries[i];
                context.progress ((double) i / (double) entries.size(), "Classifying " + entry.name() + "...");
                entry.contentKind = classifier.of (entry);
                byKind[entry.contentKind].push_back (std::move (entry));
            }

            AnalysisResult result;
            result.nestedGroups = true;

            for (const auto kind : ContentKinds::all())
            {
                const auto found = byKind.find (kind);

                if (found == byKind.end())
                    continue;

                auto& items = found->second;
                std::sort (items.begin(), items.end(), Orderings::largestFirst);
                auto title = ContentKinds::nameOf (kind) + "  -  " + Format::count (items.size(), "item") + ", " + Format::size (totalSize (items));
                result.groups.push_back ({ std::move (title), std::move (items) });
            }

            return result;
        }
    };
}

std::unique_ptr<Criterion> Criteria::createContentKind()   { return std::make_unique<ContentKindCriterion>(); }
