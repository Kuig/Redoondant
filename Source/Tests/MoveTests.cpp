/*  Tests of "Move to folder". */

#include "TestHelpers.h"
#include "../Core/MoveToFolder.h"
#include "../Core/Removal.h"

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

            // A name that is only a dot-prefix has no extension to keep.
            temp.write ("dest/.gitignore", "x");
            expectEquals (MoveToFolder::uniqueTarget (temp.root.getChildFile ("dest"), ".gitignore").getFileName(), juce::String (".gitignore (2)"));
        }

        beginTest ("Files and folders are moved, nothing is overwritten");
        {
            TempFolder temp;
            const auto file = temp.write ("src/a.txt", "from source");
            const auto sub = temp.write ("src/tree/deep/b.txt", "deep").getParentDirectory().getParentDirectory();
            temp.write ("dest/a.txt", "already here");
            const auto dest = temp.root.getChildFile ("dest");

            const auto report = Removal::toFolder ({ FileEntry::fromFile (file), FileEntry::fromFile (sub) }, dest);

            expectEquals (report.processed, 2);
            expect (report.failures.isEmpty());
            expect (! file.exists() && ! sub.exists());
            expectEquals (dest.getChildFile ("a.txt").loadFileAsString(), juce::String ("already here"));
            expectEquals (dest.getChildFile ("a (2).txt").loadFileAsString(), juce::String ("from source"));
            expectEquals (dest.getChildFile ("tree/deep/b.txt").loadFileAsString(), juce::String ("deep"));
            expectEquals (report.bytes, (juce::int64) (11 + 4));
        }

        beginTest ("A folder and its content are moved once");
        {
            TempFolder temp;
            const auto inner = temp.write ("src/tree/x.txt", "x");
            const auto tree = inner.getParentDirectory();
            const auto dest = temp.root.getChildFile ("dest");
            dest.createDirectory();

            const auto report = Removal::toFolder ({ FileEntry::fromFile (tree), FileEntry::fromFile (inner) }, dest);
            expectEquals (report.processed, 1);
            expect (report.failures.isEmpty());
            expect (dest.getChildFile ("tree/x.txt").existsAsFile());
        }

        beginTest ("Refuses folder into itself, skips items already there");
        {
            TempFolder temp;
            const auto inner = temp.write ("src/tree/x.txt", "x");
            const auto tree = inner.getParentDirectory();
            const auto already = temp.write ("src/keep.txt", "k");

            auto report = Removal::toFolder ({ FileEntry::fromFile (tree) }, tree.getChildFile ("sub"));
            expectEquals (report.processed, 0);     // The destination doesn't exist yet.
            expectEquals (report.failures.size(), 1);

            tree.getChildFile ("sub").createDirectory();
            report = Removal::toFolder ({ FileEntry::fromFile (tree) }, tree.getChildFile ("sub"));
            expectEquals (report.processed, 0);
            expectEquals (report.failures.size(), 1);
            expect (inner.existsAsFile());

            report = Removal::toFolder ({ FileEntry::fromFile (already) }, already.getParentDirectory());
            expectEquals (report.processed, 0);
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

            expect (MoveToFolder::moveItem (tree, temp.root.getChildFile ("moved-tree"), true) == MoveToFolder::Outcome::moved);
            expect (MoveToFolder::moveItem (file, temp.root.getChildFile ("moved-file.txt"), true) == MoveToFolder::Outcome::moved);
            expect (! tree.exists() && ! file.exists());
            expect (temp.root.getChildFile ("moved-tree/sub/y.txt").existsAsFile());
            expectEquals (temp.root.getChildFile ("moved-file.txt").loadFileAsString(), juce::String ("f"));

            // An existing target is never replaced.
            temp.write ("other.txt", "o");
            expect (MoveToFolder::moveItem (temp.root.getChildFile ("other.txt"), temp.root.getChildFile ("moved-file.txt")) == MoveToFolder::Outcome::failed);
            expectEquals (temp.root.getChildFile ("moved-file.txt").loadFileAsString(), juce::String ("f"));
        }
#if JUCE_WINDOWS
        beginTest ("Folders with junctions are not copied between drives");
        {
            TempFolder temp;
            temp.write ("outside/precious.txt", "keep me");
            temp.write ("src/tree/x.txt", "x");
            const auto tree = temp.root.getChildFile ("src/tree");

            juce::ChildProcess mklink;
            const auto command = "cmd /c mklink /J \"" + tree.getChildFile ("link").getFullPathName() + "\" \""
                                 + temp.root.getChildFile ("outside").getFullPathName() + "\"";

            if (mklink.start (command) && mklink.waitForProcessToFinish (10000) && tree.getChildFile ("link").isSymbolicLink())
            {
                const auto target = temp.root.getChildFile ("moved-tree");
                expect (MoveToFolder::moveItem (tree, target, true) == MoveToFolder::Outcome::containsLinks);
                expect (! target.exists());
                expect (tree.getChildFile ("x.txt").existsAsFile());
                expect (temp.root.getChildFile ("outside/precious.txt").existsAsFile());
            }
            else
            {
                logMessage ("Could not create a junction: this check was skipped.");
            }

            // deleteRecursively() doesn't follow the junction, but remove it first to be sure the temp folder cleans up.
            tree.getChildFile ("link").deleteFile();
        }
#endif

        beginTest ("A source that can't be removed completely is reported, not lost");
        {
            TempFolder temp;
            const auto stuck = temp.write ("src/tree/stuck.txt", "s");
            temp.write ("src/tree/free.txt", "f");
            const auto tree = temp.root.getChildFile ("src/tree");
            const auto target = temp.root.getChildFile ("moved-tree");

            if (stuck.setReadOnly (true))      // A read-only file can't be deleted: stands in for a file in use.
            {
                expect (MoveToFolder::moveItem (tree, target, true) == MoveToFolder::Outcome::copiedOriginalKept);
                expect (target.getChildFile ("stuck.txt").existsAsFile());
                expect (target.getChildFile ("free.txt").existsAsFile());
                expect (stuck.existsAsFile());

                stuck.setReadOnly (false);
                target.getChildFile ("stuck.txt").setReadOnly (false);
            }
        }

        beginTest ("Items that are already gone are counted apart");
        {
            TempFolder temp;
            const auto gone = temp.root.getChildFile ("never-there.txt");
            FileEntry entry = FileEntry::fromFile (temp.write ("a.txt", "a"));
            entry.file = gone;

            const auto report = Removal::deletePermanently ({ entry });
            expectEquals (report.processed, 0);
            expectEquals (report.alreadyGone, 1);
            expectEquals ((int) report.bytes, 0);
            expectEquals (report.files.size(), 1);
            expect (report.failures.isEmpty());
        }
    }
};

static MoveTests moveTests;
