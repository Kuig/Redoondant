/*  Tests of bulk check states and of the "Multiple criteria" criterion. */

#include "TestHelpers.h"
#include "../Criteria/AllCriteria.h"
#include "../UI/ResultsModel.h"

namespace
{
    /** A criterion returning a fixed list. */
    class FakeCriterion final : public Criterion
    {
    public:
        FakeCriterion (const juce::String& name, std::vector<FileEntry> list, bool exhaustive = false)
            : Criterion ({ name.toLowerCase(), name, {}, false, false, exhaustive }), items (std::move (list)) {}

        AnalysisResult analyse (const ScanContext&, const ParameterSet&) const override   { return AnalysisResult::flat (items); }

        std::vector<FileEntry> items;
    };

    FileEntry mark (const juce::File& file, bool checked)
    {
        auto e = FileEntry::fromFile (file);
        e.selected = checked;
        return e;
    }

    std::vector<FileEntry> allItems (const AnalysisResult& result)
    {
        std::vector<FileEntry> items;

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
            model.setResult (AnalysisResult::flat ({ mark (temp.write ("a.txt", "a"), true), mark (temp.write ("b.txt", "b"), false) }),
                             temp.root, false);

            model.applyChecks (ResultsModel::Checks::all);
            expectEquals ((int) model.getCheckedEntries().size(), 2);
            model.applyChecks (ResultsModel::Checks::none);
            expectEquals ((int) model.getCheckedEntries().size(), 0);
            model.applyChecks (ResultsModel::Checks::defaults);
            expectEquals ((int) model.getCheckedEntries().size(), 1);

            model.setFilter ([] (const FileEntry& e) { return e.name() == "b.txt"; });
            model.applyChecks (ResultsModel::Checks::all);   // Only visible items change.
            model.setFilter ({});
            expectEquals ((int) model.getCheckedEntries().size(), 2);
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
                list.push_back ({ c, {}, static_cast<const FakeCriterion*> (c)->items, c == &beta ? betaRoot : temp.root });

            return list;
        };

        const auto criterion = Criteria::createMultipleCriteria (peers);
        const auto analyse = [&] (std::function<void (ParameterSet&)> tweak) { return run (*criterion, temp.root, false, tweak); };

        beginTest ("Checked items, folder marks cover their content");
        {
            const auto result = analyse ({});
            expectEquals ((int) result.groups.size(), 1);
            expect (result.groups[0].title.startsWith ("Alpha + Beta"));
            expectEquals ((int) result.groups[0].items.size(), 2);     // x and y
            expect (result.groups[0].items[0].selected);
        }

        beginTest ("Listed items, exhaustive criteria ignored");
        {
            const auto result = analyse ([] (ParameterSet& p) { setParameter (p, "marked", 1); });
            expectEquals ((int) allItems (result).size(), 3);          // x, y, z (Gamma ignored)
            expect (! allItems (result)[0].selected);
        }

        beginTest ("Run all criteria");
        {
            const auto result = analyse ([] (ParameterSet& p) { setParameter (p, "source", 1); });
            expectEquals ((int) allItems (result).size(), 2);
        }

        beginTest ("Results of another folder are ignored");
        {
            betaRoot = temp.root.getChildFile ("b");
            expect (analyse ({}).groups.empty());
            betaRoot = temp.root;
        }

        beginTest ("Folders only: top folders by count");
        {
            const auto result = analyse ([] (ParameterSet& p) { setParameter (p, "view", 1); });
            expectEquals ((int) result.groups.size(), 1);
            expectEquals (result.groups[0].items[0].name(), juce::String ("a"));
        }

        beginTest ("Folders only: mostly-candidate folders");
        {
            auto result = analyse ([] (ParameterSet& p) { setParameter (p, "view", 1); setParameter (p, "folderMode", 1); setParameter (p, "minShare", 60); });
            expectEquals ((int) result.groups.size(), 1);  // a: 2 of 3 files

            result = analyse ([] (ParameterSet& p) { setParameter (p, "view", 1); setParameter (p, "folderMode", 1); setParameter (p, "minShare", 80); });
            expect (result.groups.empty());
        }
    }
};

static OverlapTests overlapTests;
