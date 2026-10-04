/*  Tests of the checks: group cycling, default selection strategies and group order. */

#include "TestHelpers.h"
#include "../Core/ContentHasher.h"
#include "../Core/Histogram.h"
#include "../Core/MoveToFolder.h"
#include "../Core/Removal.h"
#include "../Core/Settings.h"
#include "../Metadata/ContentDate.h"
#include "../Metadata/MetadataDiff.h"
#include "../Criteria/AllCriteria.h"
#include "../Platform/ComInit.h"
#include "../Platform/ShellIcon.h"
#include "../Core/FileCategory.h"
#include "../UI/FilterBar.h"
#include "../UI/ParametersPanel.h"
#include "../UI/ResultsModel.h"

class SelectionTests final : public juce::UnitTest
{
public:
    SelectionTests() : UnitTest ("Checks and group order", "Redoondant") {}

    void runTest() override
    {
        testGroupCycling();
        testDefaultSelection();
        testGroupOrder();
    }

private:
    void testGroupCycling()
    {
        beginTest ("Group check boxes cycle default -> all -> none");

        const auto entry = [] (const juce::String& fileName) { return tempEntry (fileName); };

        AnalysisResult result;
        result.groups.push_back ({ "first", { entry ("a"), entry ("b"), entry ("gone") } });
        result.groups.push_back ({ "second", { entry ("c"), entry ("d") } });

        ResultsModel model;
        model.setResult (std::move (result), {}, true, [] (ResultItem& item)
        {
            item.selected = item.name() == "a" || item.name() == "c";       // Checked by whoever filled the list.
            item.missing = item.name() == "gone";
        });
        model.setFilter ([] (const FileEntry& e) { return e.name() != "d"; });      // "d" is hidden: never touched.

        const auto checked = [&] { return model.getCheckedEntries().size(); };
        expectEquals ((int) checked(), 2);              // Defaults: a, c.
        model.toggle (0);                               // Row 0 is the first group's header: -> all (not the missing one).
        expectEquals ((int) checked(), 3);              // a, b + c from the second group.
        model.toggle (0);                               // -> none
        expectEquals ((int) checked(), 1);              // c only.
        model.toggle (0);                               // -> default checks
        expectEquals ((int) checked(), 2);
        expect (model.getOverallState() == CheckState::some);

        model.applyChecks (BulkCheck::all);
        expect (model.getOverallState() == CheckState::all);
        expectEquals ((int) checked(), 3);              // a, b, c: "d" is hidden, "gone" is missing.
    }

    void testDefaultSelection()
    {
        beginTest ("Default selection strategies");

        const auto entry = [] (const juce::String& fileName, juce::int64 size = 1, bool folder = false) { return tempEntry (fileName, size, {}, folder); };

        const auto makeResult = [&]
        {
            AnalysisResult result;
            result.groups.push_back ({ "first", { entry ("bbb", 5), entry ("a", 9), entry ("cc", 1) } });
            result.groups.push_back ({ "second", { entry ("zz", 1, true), entry ("y", 2) } });
            return result;
        };

        // Names of the unchecked entries, group by group.
        const auto kept = [] (ResultsModel& model)
        {
            juce::StringArray names;

            for (const auto& group : model.getResult().groups)
                for (const auto& e : group.items)
                    if (! e.selected)
                        names.add (e.name());

            return names.joinIntoString (",");
        };

        ResultsModel model;
        model.setDefaultSelection (DefaultSelection::shortestName);
        model.setResult (makeResult(), {}, true);
        expectEquals (kept (model), juce::String ("a,y"));

        model.setDefaultSelection (DefaultSelection::longestName);
        expectEquals (kept (model), juce::String ("bbb,zz"));

        model.setDefaultSelection (DefaultSelection::none);
        expectEquals (kept (model), juce::String ("bbb,a,cc,zz,y"));

        model.setDefaultSelection (DefaultSelection::all);
        expectEquals (kept (model), juce::String());

        model.setDefaultSelection (DefaultSelection::folders);
        expectEquals ((int) model.getCheckedEntries().size(), 1);
        expectEquals (model.getCheckedEntries()[0].name(), juce::String ("zz"));

        model.setDefaultSelection (DefaultSelection::files);
        expectEquals ((int) model.getCheckedEntries().size(), 4);

        // Follows the sort order, and the filter (hidden entries are never kept nor checked).
        model.setDefaultSelection (DefaultSelection::followsSort);
        model.sort (Column::size, false);                           // Biggest first: a (9), bbb (5), cc (1) / y (2), zz (1).
        expectEquals (kept (model), juce::String ("a,y"));

        model.sort (Column::size, true);                            // Smallest first: cc / zz (ties keep the sort order of the name... stable).
        expectEquals (kept (model), juce::String ("cc,zz"));

        model.setFilter ([] (const FileEntry& e) { return e.name() != "cc"; });
        expectEquals ((int) model.getCheckedEntries().size(), 2);
        expectEquals (kept (model), juce::String ("cc,bbb,zz"));    // "cc" is hidden (unchecked), "bbb" is the first visible of its group.

        // Groups that left their default state (checked or unchecked as a whole) keep their checks.
        model.setFilter ({});
        model.sort (Column::size, false);
        model.toggle (0);                                           // First group: -> all.
        model.sort (Column::size, true);
        expectEquals ((int) model.getCheckedEntries().size(), 4);   // The first group stays all checked; the second keeps one.

        // A new folder forgets the old results.
        model.clear();
        expect (! model.hasResult());
        expectEquals (model.getNumRows(), 0);
        expectEquals ((int) model.getAllEntries().size(), 0);

        // Which strategies a criterion can offer.
        expectEquals ((int) DefaultSelections::available (false, false).size(), 2);
        expectEquals ((int) DefaultSelections::available (true, false).size(), 5);
        expectEquals ((int) DefaultSelections::available (false, true).size(), 4);
        expectEquals ((int) DefaultSelections::available (true, true).size(), 7);
    }

    void testGroupOrder()
    {
        beginTest ("Group order");

        const auto entry = [] (const juce::String& fileName, juce::int64 size, int daysAgo)
        {
            return tempEntry (fileName, size, juce::Time::getCurrentTime() - juce::RelativeTime::days (daysAgo));
        };

        AnalysisResult result;
        result.groups.push_back ({ "beta",  { entry ("a", 10, 5), entry ("b", 10, 6), entry ("c", 10, 7) } });     // 3 items, 30 bytes, newest 5 days
        result.groups.push_back ({ "alpha", { entry ("d", 500, 1), entry ("e", 500, 2) } });                        // 2 items, 1000 bytes, newest 1 day
        result.groups.push_back ({ "gamma", { entry ("f", 1, 30), entry ("g", 1, 40) } });                          // 2 items, 2 bytes, oldest 40 days

        ResultsModel model;
        model.setResult (std::move (result), {}, true);

        const auto order = [&] (GroupOrder o)
        {
            model.setGroupOrder (o);
            juce::StringArray titles;

            for (int row = 0; row < model.getNumRows(); ++row)
                if (model.getRow (row)->isHeader())
                    titles.add (model.getGroup (row)->title);

            return titles.joinIntoString (",");
        };

        expectEquals (order (GroupOrder::analysis), juce::String ("beta,alpha,gamma"));
        expectEquals (order (GroupOrder::name), juce::String ("alpha,beta,gamma"));
        expectEquals (order (GroupOrder::mostItems), juce::String ("beta,alpha,gamma"));     // Ties keep the analysis order.
        expectEquals (order (GroupOrder::largest), juce::String ("alpha,beta,gamma"));
        expectEquals (order (GroupOrder::newest), juce::String ("alpha,beta,gamma"));
        expectEquals (order (GroupOrder::oldest), juce::String ("gamma,beta,alpha"));
        expectEquals (order (GroupOrder::analysis), juce::String ("beta,alpha,gamma"));      // Back to the original order.
    }
};

static SelectionTests selectionTests;
