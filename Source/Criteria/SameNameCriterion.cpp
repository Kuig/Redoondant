#include "AllCriteria.h"
#include "../Core/FileCategory.h"
#include "../Core/FileScanner.h"
#include "../Core/Grouping.h"

namespace
{
    class SameNameDifferentExtension final : public Criterion
    {
    public:
        SameNameDifferentExtension()
            : Criterion ({ "sameName", "Same name, different extension",
                           "Files with the same name but a different extension, such as video.mp4 and video.mkv, "
                           "or thesis.docx and thesis.pdf. Nothing is checked by default.",
                           true }) {}

        ParameterSet createParameters() const override
        {
            return { Parameter::toggle ("acrossFolders", "Match across folders", false),
                     Parameter::toggle ("sameKind", "Only the same kind of file (audio, video...)", false) };
        }

        AnalysisResult analyse (const ScanContext& context, const ParameterSet& parameters) const override
        {
            const bool acrossFolders = parameters.getBool ("acrossFolders");
            const bool sameKind = parameters.getBool ("sameKind");

            auto sameName = Grouping::groupBy (FileScanner::filesOnly (FileScanner::scan (context, { false })), [=] (const FileEntry& e)
            {
                return (acrossFolders ? juce::String() : e.file.getParentDirectory().getFullPathName())
                       + "|" + e.file.getFileNameWithoutExtension().toLowerCase()
                       + (sameKind ? "|" + FileCategories::nameOf (FileCategories::of (e)) : juce::String());
            });

            AnalysisResult result;

            for (auto& group : sameName)
            {
                const auto extensions = Grouping::extensionsOf (group);

                if (extensions.size() < 2)
                    continue;

                result.groups.push_back ({ group.front().file.getFileNameWithoutExtension() + "  (" + extensions.joinIntoString (", ") + ")",
                                           std::move (group) });
            }

            return result;
        }
    };
}

std::unique_ptr<Criterion> Criteria::createSameName()   { return std::make_unique<SameNameDifferentExtension>(); }
