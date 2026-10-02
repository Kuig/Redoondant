/*  Criteria that simply keep the scanned entries matching a predicate. */

#include "AllCriteria.h"
#include "../Core/FileScanner.h"
#include "../Core/Grouping.h"
#include <set>

namespace
{
    using Predicate = std::function<bool (const FileEntry&)>;
    using Ordering  = std::function<bool (const FileEntry&, const FileEntry&)>;

    /** Scans, keeps the entries accepted by the predicate, orders and optionally pre-selects them. */
    AnalysisResult filterScan (const ScanContext& context, const FileScanner::Options& options,
                               const Predicate& accept, const Ordering& order, bool preselect)
    {
        auto entries = FileScanner::scan (context, options);
        entries.erase (std::remove_if (entries.begin(), entries.end(), [&] (const FileEntry& e) { return ! accept (e); }), entries.end());
        std::sort (entries.begin(), entries.end(), order);
        Grouping::selectAll (entries, preselect);
        return AnalysisResult::flat (std::move (entries));
    }

    const FileScanner::Options filesOnlyScan { false };
    const FileScanner::Options fullScan {};

    const Ordering largestFirst = [] (const FileEntry& a, const FileEntry& b) { return a.size > b.size; };
    const Ordering oldestFirst  = [] (const FileEntry& a, const FileEntry& b) { return a.modified < b.modified; };
    const Ordering byName       = [] (const FileEntry& a, const FileEntry& b) { return a.name().compareNatural (b.name()) < 0; };

    //==============================================================================
    class LargeElements final : public Criterion
    {
    public:
        LargeElements()
            : Criterion ({ "largeFiles", "Large elements", "Files (and optionally folders) larger than the given size, largest first." }) {}

        ParameterSet createParameters() const override
        {
            return { Parameter::number ("minSize", "Larger than", 500, "MB"),
                     Parameter::toggle ("folders", "Include folders", false) };
        }

        AnalysisResult analyse (const ScanContext& context, const ParameterSet& parameters) const override
        {
            const auto minBytes = (juce::int64) (parameters.getNumber ("minSize") * 1024.0 * 1024.0);
            const bool folders = parameters.getBool ("folders");

            return filterScan (context, folders ? fullScan : filesOnlyScan,
                               [=] (const FileEntry& e) { return (folders || ! e.isDirectory) && e.size >= minBytes; },
                               largestFirst, false);
        }
    };

    //==============================================================================
    class OldFiles final : public Criterion
    {
    public:
        OldFiles()
            : Criterion ({ "oldFiles", "Old files", "Files not modified for longer than the given number of days, oldest first." }) {}

        ParameterSet createParameters() const override
        {
            return { Parameter::number ("days", "Not modified for", 800, "days") };
        }

        AnalysisResult analyse (const ScanContext& context, const ParameterSet& parameters) const override
        {
            const auto cutoff = juce::Time::getCurrentTime() - juce::RelativeTime::days (parameters.getNumber ("days"));
            return filterScan (context, filesOnlyScan,
                               [=] (const FileEntry& e) { return ! e.isDirectory && e.modified < cutoff; },
                               oldestFirst, false);
        }
    };

    //==============================================================================
    class EmptyItems final : public Criterion
    {
    public:
        EmptyItems()
            : Criterion ({ "empty", "Empty files & folders",
                           "Zero-byte files, and folders that contain no files (only empty sub-folders, if any)." }) {}

        AnalysisResult analyse (const ScanContext& context, const ParameterSet&) const override
        {
            auto result = filterScan (context, fullScan,
                                      [] (const FileEntry& e) { return e.isDirectory ? e.fileCount == 0 : e.size == 0; },
                                      byName, true);

            // Only list the outermost empty folder of a chain of empty folders.
            auto& items = result.groups.front().items;
            std::set<juce::String> emptyFolders;

            for (const auto& e : items)
                if (e.isDirectory)
                    emptyFolders.insert (e.file.getFullPathName());

            items.erase (std::remove_if (items.begin(), items.end(), [&] (const FileEntry& e)
            {
                return emptyFolders.count (e.file.getParentDirectory().getFullPathName()) > 0;
            }), items.end());

            return result;
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
        PatternFiles (Info info, std::vector<PatternList> patternLists, bool preselectMatches)
            : Criterion (std::move (info)), lists (std::move (patternLists)), preselect (preselectMatches) {}

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

            return filterScan (context, filesOnlyScan,
                               [&] (const FileEntry& e)
                               {
                                   return ! e.isDirectory
                                       && std::any_of (wildcards.begin(), wildcards.end(),
                                                       [&] (const juce::String& w) { return e.name().matchesWildcard (w, true); });
                               },
                               largestFirst, preselect);
        }

    private:
        std::vector<PatternList> lists;
        bool preselect;
    };

    //==============================================================================
    class ManualInspection final : public Criterion
    {
    public:
        ManualInspection()
            : Criterion ({ "manual", "Manual inspection",
                           "Every file and folder, sortable by any column (files and folders are mixed) "
                           "and filterable by type, date and size.",
                           false, true }) {}

        AnalysisResult analyse (const ScanContext& context, const ParameterSet&) const override
        {
            return filterScan (context, fullScan, [] (const FileEntry&) { return true; }, byName, false);
        }
    };
}

std::unique_ptr<Criterion> Criteria::createLargeFiles()         { return std::make_unique<LargeElements>(); }
std::unique_ptr<Criterion> Criteria::createOldFiles()           { return std::make_unique<OldFiles>(); }
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
        false);
}

std::unique_ptr<Criterion> Criteria::createIncompleteDownloads()
{
    return std::make_unique<PatternFiles> (
        Criterion::Info { "incomplete", "Incomplete downloads", "Leftovers of interrupted or failed browser downloads." },
        std::vector<PatternList> { { "patterns", "Extensions or patterns (;)", "crdownload;part;partial;download;opdownload;!ut" } },
        true);
}
