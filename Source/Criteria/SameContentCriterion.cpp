#include "AllCriteria.h"
#include "../Core/FileCategory.h"
#include "../Core/FileScanner.h"
#include "../Core/Format.h"
#include "../Core/Grouping.h"
#include "../Metadata/ContentDate.h"
#include "../Metadata/MetadataReader.h"
#include <map>

namespace
{
    using Alternatives = std::vector<juce::String>;     ///< Keys tried in order; the first non-empty one counts.

    /** The metadata that identifies a work, per kind of file. Technical properties
        (size, bitrate, resolution, codec...) are deliberately left out.
    */
    const std::map<FileCategory, std::vector<Alternatives>>& identityFields()
    {
        using namespace MetadataKeys;

        static const std::map<FileCategory, std::vector<Alternatives>> fields
        {
            { FileCategory::audio,    { { artist, albumArtist }, { title }, { album }, { track } } },
            { FileCategory::video,    { { title } } },
            { FileCategory::image,    { { dateTaken }, { cameraMaker }, { cameraModel } } },
            { FileCategory::document, { { title }, { author }, { pageCount } } },
        };

        return fields;
    }

    /** With "strict identity": the field a work can't be identified without (a title, the moment a photo was taken). */
    const juce::String& strongField (FileCategory category)
    {
        return category == FileCategory::image ? MetadataKeys::dateTaken : MetadataKeys::title;
    }

    juce::String normalise (const juce::String& value)
    {
        return juce::StringArray::fromTokens (value.toLowerCase(), " \t\r\n", {}).joinIntoString (" ");
    }

    /** What a file is compared on: its identity values and duration (-1 if unknown). */
    struct Identity
    {
        juce::String signature;
        juce::StringArray values;
        double duration = -1.0;
    };

    /** @param strict   the strong field must be present, and neither the page count nor the duration (which many different
                        works share) counts towards minFields.
    */
    std::optional<Identity> identify (const FileEntry& entry, const Metadata& metadata, int minFields, bool strict)
    {
        const auto category = FileCategories::of (entry);
        const auto found = identityFields().find (category);

        if (found == identityFields().end())
            return std::nullopt;

        Identity identity;
        identity.signature = FileCategories::nameOf (category);
        int matched = 0;

        if (strict && metadata.text (strongField (category)).trim().isEmpty())
            return std::nullopt;

        for (const auto& alternatives : found->second)
        {
            juce::String value;

            for (const auto& key : alternatives)
                if (value.isEmpty())
                    value = metadata.text (key).trim();

            identity.signature << "|" << normalise (value);

            if (value.isNotEmpty())
            {
                identity.values.add (value);
                matched += strict && alternatives.front() == MetadataKeys::pageCount ? 0 : 1;
            }
        }

        if (const auto* duration = metadata.find (MetadataKeys::duration); duration != nullptr && duration->raw.isDouble())
        {
            identity.duration = (double) duration->raw;
            matched += strict ? 0 : 1;      // A duration counts as one matching field (not when strict).
        }

        if (matched < minFields)
            return std::nullopt;

        return identity;
    }

    class SameContentDifferentFormat final : public Criterion
    {
    public:
        explicit SameContentDifferentFormat (MetadataSource source)
            : Criterion ({ "sameContent", "Similar metadata",
                           "Media and documents with matching metadata (artist, title, album; date taken, camera; title, author) "
                           "but a different format, such as FLAC and MP3 or DOCX and PDF. By default the largest file of each group is kept.",
                           Trait::grouped | Trait::groupsOfCopies },
                        { DefaultSelection::followsSort, Column::size, false }),
              readMetadata (source != nullptr ? std::move (source) : MetadataSource (MetadataReader::read)) {}

        ParameterSet createParameters() const override
        {
            return { Parameter::number ("minFields", "Min. matching fields", 2),
                     Parameter::number ("tolerance", "Duration tolerance", 2, "s"),
                     Parameter::toggle ("strict", "Strict identity (title / date taken required)", false) };
        }

        AnalysisResult analyse (const ScanContext& context, const ParameterSet& parameters) const override
        {
            const int minFields = juce::jmax (1, (int) parameters.getNumber ("minFields"));
            const double tolerance = juce::jmax (0.0, parameters.getNumber ("tolerance"));
            const bool strict = parameters.getBool ("strict");

            auto files = FileScanner::keepIf (FileScanner::scanFiles (context), [] (const FileEntry& e)
            {
                return identityFields().count (FileCategories::of (e)) != 0;
            });

            std::map<juce::String, Identity> identities;     // By full path.
            std::vector<FileEntry> candidates;

            for (size_t i = 0; i < files.size() && ! context.isCancelled(); ++i)
            {
                context.progress ((double) i / (double) files.size(), "Reading metadata of " + files[i].name());

                const auto metadata = readMetadata (files[i].file);
                files[i].contentCreated = ContentDate::oldestOf (metadata, files[i]);     // Read anyway: the date column needn't read it again.

                if (auto identity = identify (files[i], metadata, minFields, strict))
                {
                    identities[files[i].file.getFullPathName()] = std::move (*identity);
                    candidates.push_back (std::move (files[i]));
                }
            }

            const auto identityOf = [&] (const FileEntry& e) -> const Identity& { return identities.at (e.file.getFullPathName()); };
            AnalysisResult result;

            for (auto& sameMetadata : Grouping::groupBy (std::move (candidates), [&] (const FileEntry& e) { return identityOf (e).signature; }))
            {
                auto clusters = Grouping::clusterByGap (std::move (sameMetadata),
                                                        [&] (const FileEntry& e) { return identityOf (e).duration; },
                                                        tolerance);

                for (auto& group : Grouping::keepMixedFormats (std::move (clusters), [&] (const std::vector<FileEntry>& g)
                                                               { return identityOf (g.front()).values.joinIntoString (" - "); }))
                    result.groups.push_back (std::move (group));
            }

            return result;
        }

    private:
        MetadataSource readMetadata;
    };
}

std::unique_ptr<Criterion> Criteria::createSameContent (MetadataSource source)
{
    return std::make_unique<SameContentDifferentFormat> (std::move (source));
}
