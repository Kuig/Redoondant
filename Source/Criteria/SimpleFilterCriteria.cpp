/*  Criteria that simply keep the scanned entries matching a predicate. */

#include "AllCriteria.h"
#include "../Core/FileScanner.h"
#include "../Core/Format.h"
#include "../Core/DateKind.h"
#include "../Core/Paths.h"
#include "../Metadata/ContentDate.h"
#include <set>

namespace
{
    const FileScanner::Options filesOnlyScan { false };
    const FileScanner::Options fullScan {};

    //==============================================================================
    class LargeElements final : public Criterion
    {
    public:
        LargeElements()
            : Criterion ({ "largeFiles", "Large elements", "Files (and optionally folders) larger than the given size, largest first.",
                           Trait::showsFolders },
                        { DefaultSelection::none, Column::size, false }) {}

        ParameterSet createParameters() const override
        {
            return { Parameter::number ("minSize", "Larger than", 500, "MB"),
                     Parameter::toggle ("folders", "Include folders", false) };
        }

        AnalysisResult analyse (const ScanContext& context, const ParameterSet& parameters) const override
        {
            const auto minBytes = (juce::int64) (parameters.getNumber ("minSize") * Format::bytesPerMB);
            const bool folders = parameters.getBool ("folders");

            return AnalysisResult::flatSorted (FileScanner::scanAndFilter (context, folders ? fullScan : filesOnlyScan,
                                                                           [=] (const FileEntry& e) { return (folders || ! e.isDirectory) && e.size >= minBytes; }),
                                               Orderings::largestFirst);
        }
    };

    //==============================================================================
    class OldFiles final : public Criterion
    {
    public:
        explicit OldFiles (MetadataSource source)
            : Criterion ({ "oldFiles", "Old files", "Files whose creation, modification or content date (from the metadata: photo taken, "
                                                    "document created...) is older than the given number of days, oldest first." }),
              readMetadata (std::move (source)) {}

        ParameterSet createParameters() const override
        {
            auto kind = Parameter::choice ("dateKind", {}, DateKinds::names (true, " over"), (int) DateKind::modified);
            kind.editorWidth = 170;
            return { kind, Parameter::number ("days", {}, 800, "days ago") };
        }

        AnalysisResult analyse (const ScanContext& context, const ParameterSet& parameters) const override
        {
            const auto kind = parameters.getEnum<DateKind> ("dateKind");
            const auto cutoff = juce::Time::getCurrentTime() - juce::RelativeTime::days (parameters.getNumber ("days"));

            auto entries = FileScanner::scanFiles (context);

            if (kind == DateKind::content)
                ContentDate::fill (entries, context, readMetadata);

            // Files without the date (content kind) are unknown, not old.
            entries = FileScanner::keepIf (std::move (entries), [&] (const FileEntry& e)
            {
                const auto date = dateOf (e, kind);
                return date.toMilliseconds() != 0 && date < cutoff;
            });

            return AnalysisResult::flatSorted (std::move (entries), [kind] (const FileEntry& a, const FileEntry& b) { return dateOf (a, kind) < dateOf (b, kind); });
        }

    private:
        MetadataSource readMetadata;
    };

    //==============================================================================
    class EmptyItems final : public Criterion
    {
    public:
        EmptyItems()
            : Criterion ({ "empty", "Empty files & folders",
                           "Zero-byte files, and folders that contain no files (only empty sub-folders, if any). "
                           "Placeholders (.gitkeep, __init__.py...) are left out, and by default the data of version control.",
                           Trait::showsFolders },
                        { DefaultSelection::all }) {}

        ParameterSet createParameters() const override
        {
            return { Criteria::skipVersionControl() };
        }

        AnalysisResult analyse (const ScanContext& context, const ParameterSet& parameters) const override
        {
            // Empty files that only hold a place (a Python package, an empty folder kept in git) are not junk.
            static const juce::StringArray placeholders { ".gitkeep", ".keep", "__init__.py", "py.typed" };

            // A repository's empty folders (refs/heads after "git gc"...) are part of it: deleting them breaks it.
            const bool skipVersionControl = parameters.getBool ("skipVersionControl");
            auto options = fullScan;

            if (skipVersionControl)
                options.shouldDescend = [] (const FileEntry& e) { return ! FileScanner::isVersionControlFolder (e); };

            auto items = FileScanner::scanAndFilter (context, options, [skipVersionControl] (const FileEntry& e)
            {
                return e.isDirectory ? e.fileCount == 0 && ! e.contentUnknown && ! (skipVersionControl && FileScanner::isVersionControlFolder (e))
                                     : e.size == 0 && ! placeholders.contains (e.name(), true);
            });

            // Only list the outermost empty folder of a chain of empty folders.
            std::set<juce::String> emptyFolders;

            for (const auto& e : items)
                if (e.isDirectory)
                    emptyFolders.insert (Paths::key (e.file));

            items = FileScanner::keepIf (std::move (items), [&] (const FileEntry& e)
            {
                return emptyFolders.count (Paths::key (e.file.getParentDirectory())) == 0;
            });

            return AnalysisResult::flatSorted (std::move (items), Orderings::byName);
        }
    };

    //==============================================================================
    /** One list of extensions or wildcard patterns; with `toggleable` the user can switch the whole list off. */
    struct PatternList
    {
        juce::String id, label, defaults;
        bool toggleable = false;
        bool enabledByDefault = true;
    };

    /** Files matching the extensions or wildcard patterns of any enabled list. */
    class PatternFiles final : public Criterion
    {
    public:
        PatternFiles (Info info, std::vector<PatternList> patternLists, DefaultSelection selection)
            : Criterion (std::move (info), { selection }), lists (std::move (patternLists)) {}

        ParameterSet createParameters() const override
        {
            ParameterSet parameters;

            for (const auto& list : lists)
                parameters.all().push_back (list.toggleable ? Parameter::textWithToggle (list.id, list.label, list.defaults, list.enabledByDefault)
                                                            : Parameter::text (list.id, list.label, list.defaults));

            return parameters;
        }

        AnalysisResult analyse (const ScanContext& context, const ParameterSet& parameters) const override
        {
            juce::StringArray wildcards;

            for (const auto& list : lists)
                if (parameters.isEnabled (list.id))
                    for (const auto& pattern : parameters.getList (list.id))
                        wildcards.add (pattern.containsAnyOf ("*?") ? pattern : "*." + pattern.trimCharactersAtStart ("."));

            return AnalysisResult::flatSorted (FileScanner::scanAndFilter (context, filesOnlyScan, [&] (const FileEntry& e)
                                               {
                                                   return ! e.isDirectory
                                                       && std::any_of (wildcards.begin(), wildcards.end(),
                                                                       [&] (const juce::String& w) { return e.name().matchesWildcard (w, true); });
                                               }),
                                               Orderings::largestFirst);
        }

    private:
        std::vector<PatternList> lists;
    };

    //==============================================================================
    class ManualInspection final : public Criterion
    {
    public:
        ManualInspection()
            : Criterion ({ "manual", "Manual inspection",
                           "Every file and folder, sortable by any column (files and folders are mixed) "
                           "and filterable by name, type, date and size.",
                           Trait::exhaustive | Trait::showsFolders }) {}

        AnalysisResult analyse (const ScanContext& context, const ParameterSet&) const override
        {
            return AnalysisResult::flatSorted (FileScanner::scan (context, fullScan), Orderings::byName);
        }
    };
}

std::unique_ptr<Criterion> Criteria::createLargeFiles()         { return std::make_unique<LargeElements>(); }
std::unique_ptr<Criterion> Criteria::createOldFiles (MetadataSource source)    { return std::make_unique<OldFiles> (std::move (source)); }
std::unique_ptr<Criterion> Criteria::createEmptyItems()         { return std::make_unique<EmptyItems>(); }
std::unique_ptr<Criterion> Criteria::createManualInspection()   { return std::make_unique<ManualInspection>(); }

std::unique_ptr<Criterion> Criteria::createInstallersAndJunkFiles()
{
    return std::make_unique<PatternFiles> (
        Criterion::Info { "installers", "Installers & junk files",
                          "Installers and disk images that were probably already used, and junk: temporary and backup files, "
                          "and the regenerable peak, analysis and backup files of audio software (Cubase, Ableton Live, Reaper, WaveLab). "
                          "Each list can be switched off." },
        std::vector<PatternList> {
            { "installerPatterns", "Installers (;)", "exe;msi;msix;appx;dmg;pkg;iso;img", true, true },
            { "junkPatterns", "Junk files (;)",
              "tmp;temp;bak;old;~$*;pek;peak;asd;reapeaks;RPP-bak;RPP-UNDO;gpk", true, true } },
        DefaultSelection::none);
}

std::unique_ptr<Criterion> Criteria::createIncompleteDownloads()
{
    return std::make_unique<PatternFiles> (
        Criterion::Info { "incomplete", "Incomplete downloads", "Leftovers of interrupted or failed browser downloads." },
        std::vector<PatternList> { { "patterns", "Extensions or patterns (;)", "crdownload;part;partial;download;opdownload;!ut" } },
        DefaultSelection::all);
}
