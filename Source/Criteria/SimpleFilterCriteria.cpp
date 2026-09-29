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
    class LargeFiles final : public Criterion
    {
    public:
        LargeFiles()
            : Criterion ({ "largeFiles", "Large files", "Files larger than the given size, largest first." }) {}

        ParameterSet createParameters() const override
        {
            return { Parameter::number ("minSize", "Larger than", 500, "MB") };
        }

        AnalysisResult analyse (const ScanContext& context, const ParameterSet& parameters) const override
        {
            const auto minBytes = (juce::int64) (parameters.getNumber ("minSize") * 1024.0 * 1024.0);
            return filterScan (context, filesOnlyScan,
                               [=] (const FileEntry& e) { return ! e.isDirectory && e.size >= minBytes; },
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
            return { Parameter::number ("days", "Not modified for", 365, "days") };
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
    /** Files matching a list of extensions or wildcard patterns. */
    class PatternFiles final : public Criterion
    {
    public:
        PatternFiles (Info info, juce::String patterns, bool preselectMatches)
            : Criterion (std::move (info)), defaultPatterns (std::move (patterns)), preselect (preselectMatches) {}

        ParameterSet createParameters() const override
        {
            return { Parameter::text ("patterns", "Extensions or patterns (;)", defaultPatterns) };
        }

        AnalysisResult analyse (const ScanContext& context, const ParameterSet& parameters) const override
        {
            juce::StringArray wildcards;

            for (const auto& pattern : parameters.getList ("patterns"))
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
        juce::String defaultPatterns;
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
                           false, true, true }) {}

        AnalysisResult analyse (const ScanContext& context, const ParameterSet&) const override
        {
            return filterScan (context, fullScan, [] (const FileEntry&) { return true; }, byName, false);
        }
    };
}

std::unique_ptr<Criterion> Criteria::createLargeFiles()         { return std::make_unique<LargeFiles>(); }
std::unique_ptr<Criterion> Criteria::createOldFiles()           { return std::make_unique<OldFiles>(); }
std::unique_ptr<Criterion> Criteria::createEmptyItems()         { return std::make_unique<EmptyItems>(); }
std::unique_ptr<Criterion> Criteria::createManualInspection()   { return std::make_unique<ManualInspection>(); }

std::unique_ptr<Criterion> Criteria::createInstallersAndTempFiles()
{
    return std::make_unique<PatternFiles> (Criterion::Info { "installers", "Installers & temp files",
                                                             "Installers and disk images that were probably already used, "
                                                             "and temporary or backup files." },
                                           "exe;msi;msix;appx;dmg;pkg;iso;img;tmp;temp;bak;old;~$*", false);
}

std::unique_ptr<Criterion> Criteria::createIncompleteDownloads()
{
    return std::make_unique<PatternFiles> (Criterion::Info { "incomplete", "Incomplete downloads",
                                                             "Leftovers of interrupted or failed browser downloads." },
                                           "crdownload;part;partial;download;opdownload;!ut", true);
}
