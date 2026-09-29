#include "AllCriteria.h"
#include "../Core/FileScanner.h"
#include "../Core/Format.h"
#include "../Core/Grouping.h"
#include "../Core/MessageThread.h"
#include <map>

namespace
{
    // Choice indices of the parameters (first = default).
    enum Source     { existingResults, runAllCriteria };
    enum Marked     { checkedItems, listedItems };
    enum View       { filesAndFolders, foldersOnly };
    enum FolderMode { topFolders, mostlyCandidate };

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

    class MultipleCriteria final : public Criterion
    {
    public:
        explicit MultipleCriteria (PeerSource source)
            : Criterion ({ "overlaps", "Multiple criteria",
                           "Items marked by several criteria (a folder's mark covers its content), from the lists already "
                           "analyzed or by running all criteria again. \"Folders only\" lists the folders with most candidates.",
                           true }),
              peers (std::move (source)) {}

        ParameterSet createParameters() const override
        {
            auto folderMode = Parameter::choice ("folderMode", "Folders", { "Top folders by count", "Mostly-candidate folders" });
            folderMode.editorWidth = 190;

            auto source = Parameter::choice ("source", "Data", { "Existing results", "Run all criteria" });
            source.editorWidth = 150;

            auto view = Parameter::choice ("view", "Show", { "Files & folders", "Folders only" });
            view.editorWidth = 130;

            return { source,
                     Parameter::choice ("marked", "Counts as marked", { "Checked", "Listed" }),
                     Parameter::number ("minCriteria", "Marked by at least", 2, "criteria"),
                     view,
                     folderMode,
                     Parameter::number ("minFiles", "Min. marked files", 2),
                     Parameter::number ("minShare", "Min. marked share", 80, "%") };
        }

        AnalysisResult analyse (const ScanContext& context, const ParameterSet& parameters) const override
        {
            const auto marks = collectMarks (context, parameters);
            const auto minCriteria = juce::jmax (1, (int) parameters.getNumber ("minCriteria"));

            if (parameters.getChoice ("view") == foldersOnly)
                return listFolders (context, parameters, marks, minCriteria);

            return listItems (context, marks, minCriteria, parameters.getChoice ("marked") == checkedItems);
        }

    private:
        PeerSource peers;

        Marks collectMarks (const ScanContext& context, const ParameterSet& parameters) const
        {
            const bool rerun = parameters.getChoice ("source") == runAllCriteria;
            const bool listedCounts = parameters.getChoice ("marked") == listedItems;
            const auto peerList = peers != nullptr ? callOnMessageThread ([this] { return peers(); })
                                                   : std::vector<PeerCriterion>();
            Marks marks;

            for (size_t i = 0; i < peerList.size() && ! context.isCancelled(); ++i)
            {
                const auto& peer = peerList[i];
                const auto& peerInfo = peer.criterion->getInfo();

                if (peerInfo.id == getInfo().id || (listedCounts && peerInfo.exhaustive))
                    continue;

                std::vector<FileEntry> items;

                if (rerun)
                {
                    context.progress ((double) i / (double) peerList.size(), "Running " + peerInfo.name + "...");

                    for (auto& group : peer.criterion->analyse (context, peer.parameters).groups)
                        items.insert (items.end(), group.items.begin(), group.items.end());
                }
                else if (peer.results.has_value() && peer.resultsRoot == context.root)
                {
                    items = *peer.results;
                }

                for (const auto& item : items)
                    if (listedCounts || item.selected)
                        marks.add (item, peerInfo.name);
            }

            return marks;
        }

        static AnalysisResult listItems (const ScanContext& context, const Marks& marks, int minCriteria, bool preselect)
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
                combination.items.back().selected = preselect;
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

        static AnalysisResult listFolders (const ScanContext& context, const ParameterSet& parameters,
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

            if (parameters.getChoice ("folderMode") == topFolders)
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
                                             + Format::count ((size_t) tally.files, "file") + " marked, "
                                             + Format::size (tally.markedBytes) + " of " + Format::size (tally.bytes) + ")",
                                           { folder } });

            return result;
        }
    };
}

std::unique_ptr<Criterion> Criteria::createMultipleCriteria (PeerSource source)
{
    return std::make_unique<MultipleCriteria> (std::move (source));
}
