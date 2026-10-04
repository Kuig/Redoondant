/*  Tests of the filter bar. */

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

class FilterBarTests final : public juce::UnitTest
{
public:
    FilterBarTests() : UnitTest ("Filter bar", "Redoondant") {}

    void runTest() override
    {
        testPathExclusion();
        testTypeFilter();
    }

private:
    void testPathExclusion()
    {
        beginTest ("Filter bar: exclude by words of the path, not of the name");

        TempSettings temp;
        auto scope = temp.scope ("filter");
        scope.set ("excludePath", "Node_Modules; build");

        const auto entry = [&] (const juce::String& path) { return entryAt (temp.root.getChildFile (path)); };

        FilterBar bar (scope);
        bar.setRoot (temp.root);
        auto filter = bar.createFilter();
        expect (! filter (entry ("proj/node_modules/lib/a.js")));       // Any word, any case, anywhere in the path.
        expect (! filter (entry ("proj/BUILD/out.bin")));
        expect (filter (entry ("proj/src/a.js")));
        expect (filter (entry ("build.txt")));                          // The name does not count.
        expect (filter (entry ("src/node_modules")));                   // Nor does the name of a folder.

        bar.setRecursive (false);                                       // Without sub-folders the filter does not exist.
        filter = bar.createFilter();
        expect (filter (entry ("proj/node_modules/lib/a.js")));
    }

    void testTypeFilter()
    {
        beginTest ("Filter bar: several types, and names to exclude");

        for (const char* fileName : { "a.m", "paper.tex", "refs.BIB" })
            expect (FileCategories::of (juce::File (fileName), false) == FileCategory::text, fileName);

        TempSettings temp;
        auto scope = temp.scope ("filter");
        const auto entry = [] (const juce::String& fileName) { return tempEntry (fileName); };

        const auto accepted = [&]
        {
            const auto filter = FilterBar (scope).createFilter();
            return std::vector<bool> { filter (entry ("a.png")), filter (entry ("b.mp3")), filter (entry ("c.txt")), filter (entry ("a copy.png")) };
        };

        const auto indexOf = [] (FileCategory c)
        {
            const auto& all = FileCategories::all();
            return juce::String ((int) (std::find (all.begin(), all.end(), c) - all.begin()));
        };

        expect ((accepted() == std::vector<bool> { true, true, true, true }));      // Nothing ticked: all types.

        scope.set ("type", indexOf (FileCategory::image) + "," + indexOf (FileCategory::audio));
        expect ((accepted() == std::vector<bool> { true, true, false, true }));

        scope.set ("notName", "copy");
        expect ((accepted() == std::vector<bool> { true, true, false, false }));

        scope.set ("type", "");
        expect ((accepted() == std::vector<bool> { true, true, true, false }));

        beginTest ("multiChoice parameter values");

        auto type = Parameter::multiChoice ("type", "Type", "All types", { "x", "y", "z" });
        expect (type.selectedIndices().isEmpty());
        type.value = "2,0,9,junk";
        expectEquals (type.selectedIndices().size(), 2);                              // Out-of-range and junk ignored.
        expect (type.selectedIndices().contains (2) && type.selectedIndices().contains (0));
    }
};

static FilterBarTests filterBarTests;
