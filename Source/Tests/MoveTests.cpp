/*  Tests of "Move to folder". */

#include "TestHelpers.h"
#include "../Core/MoveToFolder.h"

class MoveTests final : public juce::UnitTest
{
public:
    MoveTests() : UnitTest ("Move to folder", "Redoondant") {}

    void runTest() override
    {
        beginTest ("Unique target names");
        {
            TempFolder temp;
            temp.write ("dest/report.pdf", "x");
            temp.write ("dest/report (2).pdf", "x");

            expectEquals (MoveToFolder::uniqueTarget (temp.root.getChildFile ("dest"), "free.txt").getFileName(), juce::String ("free.txt"));
            expectEquals (MoveToFolder::uniqueTarget (temp.root.getChildFile ("dest"), "report.pdf").getFileName(), juce::String ("report (3).pdf"));
        }

        beginTest ("Files and folders are moved, nothing is overwritten");
        {
            TempFolder temp;
            const auto file = temp.write ("src/a.txt", "from source");
            const auto sub = temp.write ("src/tree/deep/b.txt", "deep").getParentDirectory().getParentDirectory();
            temp.write ("dest/a.txt", "already here");
            const auto dest = temp.root.getChildFile ("dest");

            const auto report = MoveToFolder::run ({ FileEntry::fromFile (file), FileEntry::fromFile (sub) }, dest);

            expectEquals (report.moved, 2);
            expect (report.failures.isEmpty());
            expect (! file.exists() && ! sub.exists());
            expectEquals (dest.getChildFile ("a.txt").loadFileAsString(), juce::String ("already here"));
            expectEquals (dest.getChildFile ("a (2).txt").loadFileAsString(), juce::String ("from source"));
            expectEquals (dest.getChildFile ("tree/deep/b.txt").loadFileAsString(), juce::String ("deep"));
            expectEquals (report.bytesMoved, (juce::int64) (11 + 4));
        }

        beginTest ("A folder and its content are moved once");
        {
            TempFolder temp;
            const auto inner = temp.write ("src/tree/x.txt", "x");
            const auto tree = inner.getParentDirectory();
            const auto dest = temp.root.getChildFile ("dest");
            dest.createDirectory();

            const auto report = MoveToFolder::run ({ FileEntry::fromFile (tree), FileEntry::fromFile (inner) }, dest);
            expectEquals (report.moved, 1);
            expect (report.failures.isEmpty());
            expect (dest.getChildFile ("tree/x.txt").existsAsFile());
        }

        beginTest ("Refuses folder into itself, skips items already there");
        {
            TempFolder temp;
            const auto inner = temp.write ("src/tree/x.txt", "x");
            const auto tree = inner.getParentDirectory();
            const auto already = temp.write ("src/keep.txt", "k");

            auto report = MoveToFolder::run ({ FileEntry::fromFile (tree) }, tree.getChildFile ("sub"));
            expectEquals (report.moved, 0);     // The destination doesn't exist yet.
            expectEquals (report.failures.size(), 1);

            tree.getChildFile ("sub").createDirectory();
            report = MoveToFolder::run ({ FileEntry::fromFile (tree) }, tree.getChildFile ("sub"));
            expectEquals (report.moved, 0);
            expectEquals (report.failures.size(), 1);
            expect (inner.existsAsFile());

            report = MoveToFolder::run ({ FileEntry::fromFile (already) }, already.getParentDirectory());
            expectEquals (report.moved, 0);
            expect (report.failures.isEmpty());
            expect (already.existsAsFile());
        }

        beginTest ("Copy-then-delete fallback (moves between drives)");
        {
            TempFolder temp;
            temp.write ("src/tree/x.txt", "x");
            temp.write ("src/tree/sub/y.txt", "y");
            temp.write ("src/file.txt", "f");
            const auto tree = temp.root.getChildFile ("src/tree");
            const auto file = temp.root.getChildFile ("src/file.txt");

            expect (MoveToFolder::moveItem (tree, temp.root.getChildFile ("moved-tree"), true));
            expect (MoveToFolder::moveItem (file, temp.root.getChildFile ("moved-file.txt"), true));
            expect (! tree.exists() && ! file.exists());
            expect (temp.root.getChildFile ("moved-tree/sub/y.txt").existsAsFile());
            expectEquals (temp.root.getChildFile ("moved-file.txt").loadFileAsString(), juce::String ("f"));

            // An existing target is never replaced.
            temp.write ("other.txt", "o");
            expect (! MoveToFolder::moveItem (temp.root.getChildFile ("other.txt"), temp.root.getChildFile ("moved-file.txt")));
            expectEquals (temp.root.getChildFile ("moved-file.txt").loadFileAsString(), juce::String ("f"));
        }
    }
};

static MoveTests moveTests;
