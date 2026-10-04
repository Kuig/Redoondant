/*  Tests of bulk check states and of the "Multiple criteria" criterion. */

#include "TestHelpers.h"
#include "../Criteria/AllCriteria.h"
#include "../Criteria/ParameterChoices.h"
#include "../UI/ResultsModel.h"

using namespace ParameterChoices;

namespace
{
    /** An entry and whether the user has it checked. */
    struct Marked
    {
        FileEntry entry;
        bool checked;
    };

    Marked mark (const juce::File& file, bool checked)    { return { FileScanner::makeEntry (file), checked }; }

    /** A criterion returning a fixed list. */
    class FakeCriterion final : public Criterion
    {
    public:
        FakeCriterion (const juce::String& name, std::vector<Marked> list, bool exhaustive = false)
            : Criterion ({ name.toLowerCase(), name, {}, exhaustive ? Trait::exhaustive : Trait::none })
        {
            for (auto& marked : list)
            {
                items.push_back (marked.entry);

                if (marked.checked)
                    checked.push_back (marked.entry);
            }
        }

        AnalysisResult analyse (const ScanContext&, const ParameterSet&) const override   { return AnalysisResult::flat (items); }

        std::vector<FileEntry> items, checked;
    };

    std::vector<ResultItem> allItems (const ListedResult& result)
    {
        std::vector<ResultItem> items;

        for (const auto& group : result.groups)
            items.insert (items.end(), group.items.begin(), group.items.end());

        return items;
    }
}

class OverlapTests final : public juce::UnitTest
{
public:
    OverlapTests() : UnitTest ("Multiple criteria", "Redoondant") {}

    void runTest() override
    {
        beginTest ("Check all / none / defaults");
        {
            TempFolder temp;
            ResultsModel model;
            model.setResult (AnalysisResult::flat ({ FileScanner::makeEntry (temp.write ("a.txt", "a")), FileScanner::makeEntry (temp.write ("b.txt", "b")) }),
                             temp.root, false, [] (ResultItem& item) { item.selected = item.name() == "a.txt"; });

            model.applyChecks (BulkCheck::all);
            expectEquals ((int) model.getCheckedEntries().size(), 2);
            model.applyChecks (BulkCheck::none);
            expectEquals ((int) model.getCheckedEntries().size(), 0);
            model.applyChecks (BulkCheck::defaults);
            expectEquals ((int) model.getCheckedEntries().size(), 1);

            model.setFilter ([] (const FileEntry& e) { return e.name() == "b.txt"; });
            model.applyChecks (BulkCheck::all);   // Only visible items change.
            model.setFilter ({});
            expectEquals ((int) model.getCheckedEntries().size(), 2);
        }

        beginTest ("Hidden items follow the default checks, as shown when a filter reveals them");
        {
            TempFolder temp;
            ResultsModel model;
            model.setDefaultSelection (DefaultSelection::all);
            model.setFilter ([] (const FileEntry& e) { return e.name() == "a.txt"; });
            model.setResult (AnalysisResult::flat ({ FileScanner::makeEntry (temp.write ("a.txt", "a")), FileScanner::makeEntry (temp.write ("b.txt", "b")) }),
                             temp.root, false);
            model.setDefaultSelection (DefaultSelection::none);
            model.setDefaultSelection (DefaultSelection::all);
            model.setFilter ({});
            expectEquals ((int) model.getCheckedEntries().size(), 2);

            model.setFilter ([] (const FileEntry& e) { return e.name() == "a.txt"; });
            model.setDefaultSelection (DefaultSelection::none);
            model.setFilter ({});
            expectEquals ((int) model.getCheckedEntries().size(), 0);     // Before, b.txt stayed checked while hidden.
        }

        TempFolder temp;
        const auto x = temp.write ("a/x.txt", "1234");
        const auto y = temp.write ("a/y.txt", "1234");
        temp.write ("a/keep.txt", "1234");
        const auto z = temp.write ("b/z.txt", "1234");
        const auto top = temp.write ("top.txt", "1234");
        const auto folderA = temp.root.getChildFile ("a");

        FakeCriterion alpha ("Alpha", { mark (x, true), mark (y, true), mark (top, true), mark (z, false) });
        FakeCriterion beta ("Beta", { mark (folderA, true), mark (z, true) });
        FakeCriterion gamma ("Gamma", { mark (x, false), mark (y, false), mark (z, false), mark (top, false) }, true);

        juce::File betaRoot = temp.root;
        const auto peers = [&]
        {
            std::vector<PeerCriterion> list;

            for (const auto* c : { &alpha, &beta, &gamma })
                list.push_back ({ c, {}, static_cast<const FakeCriterion*> (c)->items, static_cast<const FakeCriterion*> (c)->checked,
                                  c == &beta ? betaRoot : temp.root, c == &gamma ? DefaultSelection::none : DefaultSelection::all });

            return list;
        };

        const auto itemsCriterion = Criteria::createMultipleCriteriaItems();
        const auto foldersCriterion = Criteria::createMultipleCriteriaFolders();

        // Tests start from: checked items, at least 2 criteria.
        const auto analyseWith = [&] (const Criterion& criterion, std::function<void (ParameterSet&)> tweak)
        {
            return run (criterion, temp.root, false, [&] (ParameterSet& p)
            {
                setParameter (p, "marked", (int) OverlapMarked::checkedItems);
                setParameter (p, "minCriteria", 2);
                setParameter (p, "minShare", 80);

                if (tweak != nullptr)
                    tweak (p);
            }, peers());     // The peers are taken at the time of each analysis, as the page does.
        };

        const auto analyse = [&] (std::function<void (ParameterSet&)> tweak) { return analyseWith (*itemsCriterion, std::move (tweak)); };
        const auto analyseFolders = [&] (std::function<void (ParameterSet&)> tweak) { return analyseWith (*foldersCriterion, std::move (tweak)); };

        beginTest ("Checked items, folder marks cover their content");
        {
            const auto result = analyse ({});
            expectEquals ((int) result.groups.size(), 1);
            expect (result.groups[0].title.startsWith ("Alpha + Beta"));
            expectEquals ((int) result.groups[0].items.size(), 2);     // x and y
        }

        beginTest ("Listed items, exhaustive criteria ignored");
        {
            const auto result = analyse ([] (ParameterSet& p) { setParameter (p, "marked", (int) OverlapMarked::listedItems); });
            expectEquals ((int) allItems (result).size(), 3);          // x, y, z (Gamma ignored)
            expect (! allItems (result)[0].selected);
        }

        beginTest ("Run all criteria");
        {
            const auto result = analyse ([] (ParameterSet& p) { setParameter (p, "source", (int) OverlapSource::runAllCriteria); });
            expectEquals ((int) allItems (result).size(), 3);          // Each peer's default selection (all) decides what is checked: x, y, z.
        }

        beginTest ("Results of another folder are ignored");
        {
            betaRoot = temp.root.getChildFile ("b");
            expect (analyse ({}).groups.empty());
            betaRoot = temp.root;
        }

        beginTest ("The same path spelled differently by two pages is one item");
        if (! juce::File::areFileNamesCaseSensitive())
        {
            FakeCriterion upper ("Upper", { mark (juce::File (x.getFullPathName().toUpperCase()), true) });
            std::vector<PeerCriterion> list { { &alpha, {}, alpha.items, alpha.checked, temp.root, DefaultSelection::all },
                                              { &upper, {}, upper.items, upper.checked, temp.root, DefaultSelection::all } };

            const auto result = run (*itemsCriterion, temp.root, false, [] (ParameterSet& p)
            {
                setParameter (p, "marked", (int) OverlapMarked::checkedItems);
                setParameter (p, "minCriteria", 2);
            }, list);

            expectEquals ((int) allItems (result).size(), 1);
            expect (result.groups[0].title.startsWith ("Alpha + Upper"));
        }

        beginTest ("Folders only: top folders by count");
        {
            const auto result = analyseFolders ({});
            expectEquals ((int) result.groups.size(), 1);
            expectEquals (result.groups[0].items[0].name(), juce::String ("a"));
        }

        beginTest ("Folders only: mostly-candidate folders");
        {
            auto result = analyseFolders ([] (ParameterSet& p) { setParameter (p, "folderMode", (int) OverlapFolderMode::mostlyCandidate); setParameter (p, "minShare", 60); });
            expectEquals ((int) result.groups.size(), 1);  // a: 2 of 3 files

            result = analyseFolders ([] (ParameterSet& p) { setParameter (p, "folderMode", (int) OverlapFolderMode::mostlyCandidate); setParameter (p, "minShare", 80); });
            expect (result.groups.empty());
        }
    }
};

static OverlapTests overlapTests;
