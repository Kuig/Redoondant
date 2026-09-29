#include "AllCriteria.h"
#include "../Core/FileCategory.h"
#include "../Core/FileScanner.h"
#include "../Core/Format.h"
#include "../Core/Grouping.h"
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

    std::optional<Identity> identify (const FileEntry& entry, const Metadata& metadata, int minFields)
    {
        const auto category = FileCategories::of (entry);
        const auto found = identityFields().find (category);

        if (found == identityFields().end())
            return std::nullopt;

        Identity identity;
        identity.signature = FileCategories::nameOf (category);
        int matched = 0;

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
                ++matched;
            }
        }

        if (const auto* duration = metadata.find (MetadataKeys::duration); duration != nullptr && duration->raw.isDouble())
        {
            identity.duration = (double) duration->raw;
            ++matched;      // A duration counts as one matching field.
        }

        if (matched < minFields)
            return std::nullopt;

        return identity;
    }

    class SameContentDifferentFormat final : public Criterion
    {
    public:
        explicit SameContentDifferentFormat (Criteria::MetadataSource source)
            : Criterion ({ "sameContent", "Same content, different format",
                           "Media and documents whose descriptive metadata match but whose format differs, such as the same "
                           "song as FLAC and MP3 (same artist, title, album, track, similar duration), the same photo as HEIC "
                           "and JPG (same date taken and camera) or the same document as DOCX and PDF (same title, author, pages). "
                           "The largest file of each group is kept.",
                           true }),
              readMetadata (source != nullptr ? std::move (source) : Criteria::MetadataSource (MetadataReader::read)) {}

        ParameterSet createParameters() const override
        {
            return { Parameter::number ("minFields", "Min. matching fields", 2),
                     Parameter::number ("tolerance", "Duration tolerance", 2, "s") };
        }

        AnalysisResult analyse (const ScanContext& context, const ParameterSet& parameters) const override
        {
            const int minFields = juce::jmax (1, (int) parameters.getNumber ("minFields"));
            const double tolerance = juce::jmax (0.0, parameters.getNumber ("tolerance"));

            auto files = FileScanner::filesOnly (FileScanner::scan (context, { false }));
            files.erase (std::remove_if (files.begin(), files.end(), [] (const FileEntry& e)
            {
                return identityFields().count (FileCategories::of (e)) == 0;
            }), files.end());

            std::map<juce::String, Identity> identities;     // By full path.
            std::vector<FileEntry> candidates;

            for (size_t i = 0; i < files.size() && ! context.isCancelled(); ++i)
            {
                context.progress ((double) i / (double) files.size(), "Reading metadata of " + files[i].name());

                if (auto identity = identify (files[i], readMetadata (files[i].file), minFields))
                {
                    identities[files[i].file.getFullPathName()] = std::move (*identity);
                    candidates.push_back (std::move (files[i]));
                }
            }

            const auto identityOf = [&] (const FileEntry& e) -> const Identity& { return identities.at (e.file.getFullPathName()); };
            AnalysisResult result;

            for (auto& sameMetadata : Grouping::groupBy (std::move (candidates), [&] (const FileEntry& e) { return identityOf (e).signature; }))
            {
                for (auto& group : Grouping::clusterByGap (std::move (sameMetadata),
                                                           [&] (const FileEntry& e) { return identityOf (e).duration; },
                                                           tolerance))
                {
                    const auto extensions = Grouping::extensionsOf (group);

                    if (extensions.size() < 2)
                        continue;

                    Grouping::selectAllButBest (group, [] (const FileEntry& a, const FileEntry& b) { return a.size > b.size; });
                    result.groups.push_back ({ identityOf (group.front()).values.joinIntoString (" - ")
                                                 + "  (" + extensions.joinIntoString (", ") + ")",
                                               std::move (group) });
                }
            }

            return result;
        }

    private:
        Criteria::MetadataSource readMetadata;
    };
}

std::unique_ptr<Criterion> Criteria::createSameContent (MetadataSource source)
{
    return std::make_unique<SameContentDifferentFormat> (std::move (source));
}
