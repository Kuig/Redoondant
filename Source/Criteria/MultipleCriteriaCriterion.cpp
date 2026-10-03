#include "AllCriteria.h"
#include "../Core/FileScanner.h"
#include "../Core/Format.h"
#include "../Core/Grouping.h"
#include "ParameterChoices.h"
#include <map>

namespace
{
    using namespace ParameterChoices;

    /** Which criteria mark each path (directly, not through a parent folder). */
    struct Marks
    {
        std::map<juce::String, std::pair<FileEntry, juce::StringArray>> byPath;

        void add (const FileEntry& entry, const juce::String& criterion)
        {
            auto& mark = byPath[entry.file.getFullPathName()];
            mark.first = entry;
            mark.second.addIfNotAlreadyThere (criterion);
        }

        /** Criteria marking the file or one of its folders, up to root. */
        juce::StringArray criteriaFor (const juce::File& file, const juce::File& root) const
        {
            juce::StringArray names;

            for (auto f = file; f != root && f != f.getParentDirectory(); f = f.getParentDirectory())
            {
                const auto found = byPath.find (f.getFullPathName());

                if (found != byPath.end())
                    names.mergeArray (found->second.second);
            }

            names.sort (true);
            return names;
        }
    };

    struct Tally
    {
        int files = 0, markedFiles = 0;
        juce::int64 bytes = 0, markedBytes = 0;
    };

    Marks collectMarks (const ScanContext& context, const ParameterSet& parameters, const std::vector<PeerCriterion>& peerList)
    {
        const bool rerun = parameters.getEnum<OverlapSource> ("source") == OverlapSource::runAllCriteria;
        const bool listedCounts = parameters.getEnum<OverlapMarked> ("marked") == OverlapMarked::listedItems;
        Marks marks;

        for (size_t i = 0; i < peerList.size() && ! context.isCancelled(); ++i)
        {
            const auto& peer = peerList[i];
            const auto& peerInfo = peer.criterion->getInfo();

            if (peerInfo.usesPeers || (listedCounts && peerInfo.exhaustive))     // Not the criteria that combine others.
                continue;

            std::vector<FileEntry> listed, checked;

            if (rerun)
            {
                context.progress ((double) i / (double) peerList.size(), "Running " + peerInfo.name + "...");

                const auto result = peer.criterion->analyse (context, peer.parameters);

                for (const auto& group : result.groups)
                    listed.insert (listed.end(), group.items.begin(), group.items.end());

                if (! listedCounts)
                    checked = DefaultSelections::checkedEntries (peer.selection, result.groups);
            }
            else if (peer.results.has_value() && peer.resultsRoot == context.root)
            {
                listed = *peer.results;
                checked = peer.checked;
            }

            for (const auto& item : listedCounts ? listed : checked)
                marks.add (item, peerInfo.name);
        }

        return marks;
    }

    AnalysisResult listItems (const ScanContext& context, const Marks& marks, int minCriteria)
    {
        struct Combination
        {
            int criteria = 0;
            juce::int64 bytes = 0;
            std::vector<FileEntry> items;
        };

        std::map<juce::String, Combination> byCombination;     // Key: "Criterion A + Criterion B"

        for (const auto& [path, mark] : marks.byPath)
        {
            const auto names = marks.criteriaFor (mark.first.file, context.root);

            if (names.size() < minCriteria)
                continue;

            auto& combination = byCombination[names.joinIntoString (" + ")];
            combination.criteria = names.size();
            combination.bytes += mark.first.size;
            combination.items.push_back (mark.first);
        }

        std::vector<std::pair<juce::String, Combination>> sorted (byCombination.begin(), byCombination.end());

        // More criteria first, then more space.
        std::sort (sorted.begin(), sorted.end(), [] (const auto& a, const auto& b)
        {
            return a.second.criteria != b.second.criteria ? a.second.criteria > b.second.criteria
                                                          : a.second.bytes > b.second.bytes;
        });

        AnalysisResult result;

        for (auto& [key, combination] : sorted)
            result.groups.push_back ({ key + "  (" + Format::count (combination.items.size(), "item") + ")",
                                       std::move (combination.items) });

        return result;
    }

    AnalysisResult listFolders (const ScanContext& context, const ParameterSet& parameters,
                                       const Marks& marks, int minCriteria)
    {
        const auto& root = context.root;
        std::map<juce::String, Tally> tallies;
        std::map<juce::String, FileEntry> folders;

        context.progress (-1.0, "Counting candidates per folder...");

        for (auto& entry : FileScanner::scanTree (root, context))
        {
            if (entry.isDirectory)
            {
                folders.emplace (entry.file.getFullPathName(), std::move (entry));
                continue;
            }

            const bool marked = marks.criteriaFor (entry.file, root).size() >= minCriteria;

            for (auto dir = entry.file.getParentDirectory(); dir != root && dir.isAChildOf (root); dir = dir.getParentDirectory())
            {
                auto& tally = tallies[dir.getFullPathName()];
                ++tally.files;
                tally.bytes += entry.size;

                if (marked)
                {
                    ++tally.markedFiles;
                    tally.markedBytes += entry.size;
                }
            }
        }

        std::vector<std::pair<FileEntry, Tally>> selected;

        if (parameters.getEnum<OverlapFolderMode> ("folderMode") == OverlapFolderMode::topFolders)
        {
            const auto minFiles = juce::jmax (1, (int) parameters.getNumber ("minFiles"));

            for (const auto& [path, tally] : tallies)
                if (tally.markedFiles >= minFiles && folders.count (path) > 0 && folders.at (path).file.getParentDirectory() == root)
                    selected.emplace_back (folders.at (path), tally);

            std::sort (selected.begin(), selected.end(), [] (const auto& a, const auto& b)
            {
                return a.second.markedFiles > b.second.markedFiles;
            });
        }
        else
        {
            const auto minShare = juce::jlimit (0.0, 1.0, parameters.getNumber ("minShare") / 100.0);
            const auto qualifies = [&] (const juce::String& path)
            {
                const auto found = tallies.find (path);
                return found != tallies.end() && found->second.bytes > 0
                    && (double) found->second.markedBytes >= minShare * (double) found->second.bytes;
            };

            for (const auto& [path, tally] : tallies)
            {
                bool outermost = true;

                for (auto dir = juce::File (path).getParentDirectory(); dir != root && dir.isAChildOf (root); dir = dir.getParentDirectory())
                    outermost = outermost && ! qualifies (dir.getFullPathName());

                if (outermost && qualifies (path) && folders.count (path) > 0)
                    selected.emplace_back (folders.at (path), tally);
            }

            std::sort (selected.begin(), selected.end(), [] (const auto& a, const auto& b)
            {
                return a.second.markedBytes > b.second.markedBytes;
            });
        }

        AnalysisResult result;

        for (auto& [folder, tally] : selected)
            result.groups.push_back ({ folder.file.getRelativePathFrom (root) + "/  (" + juce::String (tally.markedFiles) + " of "
                                         + Format::count (tally.files, "file") + " marked, "
                                         + Format::size (tally.markedBytes) + " of " + Format::size (tally.bytes) + ")",
                                       { folder } });

        return result;
    }

    /** The settings both criteria have: where the marks come from, what counts as marked, and by how many criteria. */
    ParameterSet commonParameters()
    {
        auto source = Parameter::choice ("source", "Data", { "Existing results", "Run all criteria" });
        source.editorWidth = 150;

        return { source,
                 Parameter::choice ("marked", "Counts as marked", { "Checked", "Listed" }, (int) OverlapMarked::listedItems),
                 Parameter::number ("minCriteria", "Marked by at least", 1, "criteria") };
    }

    /** What the two criteria share: they combine the other criteria, handed over as peers. */
    class MultipleCriteriaBase : public Criterion
    {
    public:
        using Criterion::Criterion;
        using Criterion::analyse;

        /** Without peers there is nothing to combine. */
        AnalysisResult analyse (const ScanContext& context, const ParameterSet& parameters) const final
        {
            return analyse (context, parameters, {});
        }
    };

    //==============================================================================
    class MultipleCriteriaItems final : public MultipleCriteriaBase
    {
    public:
        MultipleCriteriaItems()
            : MultipleCriteriaBase ({ "overlaps", "Multiple criteria: items",
                                      "Items marked by several criteria (a folder's mark covers its content), from the lists already "
                                      "analyzed or by running all criteria again. Grouped by the combination of criteria that marks them.",
                                      Trait::grouped | Trait::recursiveByDefault | Trait::showsFolders | Trait::usesPeers }) {}

        ParameterSet createParameters() const override      { return commonParameters(); }

        AnalysisResult analyse (const ScanContext& context, const ParameterSet& parameters,
                                const std::vector<PeerCriterion>& peers) const override
        {
            const auto marks = collectMarks (context, parameters, peers);
            return listItems (context, marks, juce::jmax (1, (int) parameters.getNumber ("minCriteria")));
        }
    };

    //==============================================================================
    class MultipleCriteriaFolders final : public MultipleCriteriaBase
    {
    public:
        MultipleCriteriaFolders()
            : MultipleCriteriaBase ({ "overlapFolders", "Multiple criteria: folders",
                                      "The folders with most candidates: items marked by several criteria (a folder's mark covers its content), "
                                      "from the lists already analyzed or by running all criteria again. Either the top folders by number of "
                                      "marked files, or the outermost folders that are mostly candidates.",
                                      Trait::grouped | Trait::recursiveByDefault | Trait::showsFolders | Trait::usesPeers }) {}

        ParameterSet createParameters() const override
        {
            auto folderMode = Parameter::choice ("folderMode", "Folders", { "Top folders by count", "Mostly-candidate folders" });
            folderMode.editorWidth = 190;

            auto parameters = commonParameters();
            parameters.all().push_back (folderMode);
            parameters.all().push_back (Parameter::number ("minFiles", "Min. marked files", 2));
            parameters.all().push_back (Parameter::number ("minShare", "Min. marked share", 50, "%"));
            return parameters;
        }

        AnalysisResult analyse (const ScanContext& context, const ParameterSet& parameters,
                                const std::vector<PeerCriterion>& peers) const override
        {
            const auto marks = collectMarks (context, parameters, peers);
            return listFolders (context, parameters, marks, juce::jmax (1, (int) parameters.getNumber ("minCriteria")));
        }
    };
}

std::unique_ptr<Criterion> Criteria::createMultipleCriteriaItems()      { return std::make_unique<MultipleCriteriaItems>(); }
std::unique_ptr<Criterion> Criteria::createMultipleCriteriaFolders()    { return std::make_unique<MultipleCriteriaFolders>(); }
