/*  Unit tests for the analysis logic. Run with: Redoondant.exe --test */

#include "TestHelpers.h"
#include "../Core/FileScanner.h"
#include "../Core/Grouping.h"
#include "../Core/Removal.h"
#include "../Platform/RecycleBin.h"
#include "../Criteria/AllCriteria.h"
#include "../Platform/ComInit.h"

namespace
{
    FileEntry entry (const juce::String& name, juce::Time modified = {})
    {
        return entryAt (juce::File::getCurrentWorkingDirectory().getChildFile (name), 0, modified);
    }
}

//==============================================================================
class GroupingTests final : public juce::UnitTest
{
public:
    GroupingTests() : UnitTest ("Grouping", "Redoondant") {}

    void runTest() override
    {
        beginTest ("Shared prefix ignores case");
        expectEquals (Grouping::sharedPrefixLength ("Report", "rePORT_v2"), 6);
        expectEquals (Grouping::sharedPrefixLength ("abc", "xyz"), 0);

        beginTest ("Names with the same start are clustered, whatever the ending");
        auto clusters = Grouping::clusterBySharedRoot ({ entry ("report.pdf"), entry ("report (1).pdf"), entry ("report_final.pdf"),
                                                         entry ("invoice.pdf"), entry ("summary.pdf") },
                                                       4, 0.6);
        expectEquals ((int) clusters.size(), 1);
        expectEquals (clusters[0].root, juce::String ("report"));
        expectEquals ((int) clusters[0].items.size(), 3);

        beginTest ("Short shared starts are not enough");
        expect (Grouping::clusterBySharedRoot ({ entry ("rep.pdf"), entry ("reply.pdf") }, 4, 0.6).empty());
        expect (Grouping::clusterBySharedRoot ({ entry ("project-alpha.pdf"), entry ("project-omega-final.pdf") }, 4, 0.9).empty());

        beginTest ("Time clusters split on gaps");
        const auto t0 = juce::Time (2026, 0, 1, 10, 0);
        auto groups = Grouping::clusterByTimeGap ({ entry ("a", t0), entry ("b", t0 + juce::RelativeTime::minutes (30)),
                                                    entry ("c", t0 + juce::RelativeTime::hours (5)),
                                                    entry ("d", t0 + juce::RelativeTime::hours (5.5)),
                                                    entry ("e", t0 + juce::RelativeTime::days (3)) },
                                                  [] (const FileEntry& e) { return e.modified; },
                                                  juce::RelativeTime::hours (2));
        expectEquals ((int) groups.size(), 2);
        expectEquals (groups[1][0].name(), juce::String ("c"));
    }
};

//==============================================================================
class RemovalTests final : public juce::UnitTest
{
public:
    RemovalTests() : UnitTest ("Removal", "Redoondant") {}

    void runTest() override
    {
        beginTest ("Nested and duplicate entries are removed");
        const auto root = juce::File::getCurrentWorkingDirectory();
        const auto make = [&] (const juce::String& path) { return entryAt (root.getChildFile (path)); };

        const auto kept = Removal::withoutNested ({ make ("a"), make ("a/b.txt"), make ("a/c/d"), make ("a b"), make ("a b"), make ("e.txt") });
        expectEquals ((int) kept.size(), 3);

        if (! juce::File::areFileNamesCaseSensitive())
        {
            beginTest ("Nested entries are found whatever the case of the path");
            const auto mixed = Removal::withoutNested ({ make ("Folder"), make ("FOLDER/inner.txt"), make ("folder") });
            expectEquals ((int) mixed.size(), 1);
        }

        beginTest ("Nested entries: siblings whose names extend another's are not nested");
        {
            const auto keptInOrder = Removal::withoutNested ({ make ("x/y"), make ("x b/c"), make ("x"), make ("x b"), make ("x.txt"), make ("x/z") });
            juce::StringArray names;

            for (const auto& e : keptInOrder)
                names.add (e.file.getRelativePathFrom (root).replaceCharacter ('\\', '/'));

            expectEquals (names.joinIntoString (","), juce::String ("x,x b,x.txt"));     // In the order given.
        }

        beginTest ("Recycle Bin support is only claimed for local fixed drives");
        expect (! RecycleBin::isSupported (juce::File ("\\\\server\\share\\file.txt")));

        // Sends files to the user's real Recycle Bin, so it only runs when asked for.
        if (juce::SystemStats::getEnvironmentVariable ("REDOONDANT_TEST_RECYCLE", {}) == "1")
        {
            beginTest ("Move to Trash recycles a batch of items");
            TempFolder temp;
            std::vector<FileEntry> entries;

            for (const auto* path : { "redoondant-test-a.txt", "redoondant-test-b.txt", "redoondant-test-dir/c.txt" })
                temp.write (path, "recycle me");

            for (const auto* path : { "redoondant-test-a.txt", "redoondant-test-b.txt", "redoondant-test-dir" })
                entries.push_back (FileScanner::makeEntry (temp.root.getChildFile (path)));

            if (RecycleBin::isSupported (temp.root))
            {
                const ScopedComInit com;
                const auto report = Removal::toTrash (entries);
                expectEquals (report.processed, 3);
                expect (report.failures.isEmpty(), report.failures.joinIntoString ("; "));
                expect (report.deletedForGood.isEmpty());

                for (const auto& e : entries)
                    expect (! e.file.exists());
            }
        }
    }
};

//==============================================================================
class CriteriaTests final : public juce::UnitTest
{
public:
    CriteriaTests() : UnitTest ("Criteria", "Redoondant") {}

    void runTest() override
    {
        beginTest ("Duplicates: the longest names are checked");
        {
            TempFolder temp;
            temp.write ("photo.jpg", "same content");
            temp.write ("photo - Copy.jpg", "same content");
            temp.write ("other.jpg", "same length!");   // Same size, different bytes.
            temp.write ("sub/photo.jpg", "same content");

            auto result = run (*Criteria::createDuplicateFiles(), temp.root);
            expectEquals ((int) result.groups.size(), 1);
            expectEquals ((int) result.groups[0].items.size(), 2);
            expect (! findItem (result.groups[0], "photo.jpg")->selected);
            expect (findItem (result.groups[0], "photo - Copy.jpg")->selected);

            result = run (*Criteria::createDuplicateFiles(), temp.root, true);
            expectEquals ((int) result.groups[0].items.size(), 3);

            temp.write ("a/.git/objects/pack/p.pack", "same content");     // A repository's objects are left out by default.
            result = run (*Criteria::createDuplicateFiles(), temp.root, true);
            expectEquals ((int) result.groups[0].items.size(), 3);

            result = run (*Criteria::createDuplicateFiles(), temp.root, true, [] (ParameterSet& p) { setParameter (p, "skipVersionControl", false); });
            expectEquals ((int) result.groups[0].items.size(), 4);

            temp.write ("app/node_modules/x/photo.jpg", "same content");      // Installed dependencies are left out by default.
            temp.write ("env/.venv/pyvenv.cfg", "home = x");
            temp.write ("env/.venv/Lib/photo.jpg", "same content");
            temp.write ("env/venv/photo.jpg", "same content");                // Not a virtual environment: no pyvenv.cfg.
            result = run (*Criteria::createDuplicateFiles(), temp.root, true);
            expectEquals ((int) result.groups[0].items.size(), 4);

            result = run (*Criteria::createDuplicateFiles(), temp.root, true, [] (ParameterSet& p) { setParameter (p, "skipDependencies", false); });
            expectEquals ((int) result.groups[0].items.size(), 6);
        }

        beginTest ("Duplicates: many identical files, and files that differ late");
        {
            TempFolder temp;
            const auto big = juce::String::repeatedString ("0123456789", 30000);     // 300 KB: several blocks.

            for (int i = 0; i < 5; ++i)
                temp.write ("same" + juce::String (i) + ".bin", big);

            temp.write ("late.bin", big.dropLastCharacters (1) + "X");            // Same size, differs in the last byte.
            temp.write ("early.bin", "X" + big.substring (1));

            const auto result = run (*Criteria::createDuplicateFiles(), temp.root);
            expectEquals ((int) result.groups.size(), 1);
            expectEquals ((int) result.groups[0].items.size(), 5);
        }

        beginTest ("Duplicates: big groups are compared one file at a time");
        {
            TempFolder temp;

            for (int i = 0; i < 300; ++i)
                temp.write ("same" + juce::String (i) + ".txt", "same content");

            const auto result = run (*Criteria::createDuplicateFiles(), temp.root);
            expectEquals ((int) result.groups.size(), 1);
            expectEquals ((int) result.groups[0].items.size(), 300);
        }

        beginTest ("Scans taken from a shared tree match scans of the disk");
        {
            TempFolder temp;
            temp.write ("a.txt", "a");
            temp.write ("sub/b.txt", "bb");
            temp.write ("sub/skip/c.txt", "ccc");
            temp.write ("other/d.txt", "dddd");

            ScanContext disk;
            disk.root = temp.root;
            auto shared = disk;
            shared.tree = FileScanner::scanWholeTree (disk);

            FileScanner::Options options;
            options.shouldDescend = [] (const FileEntry& e) { return e.name() != "skip"; };

            const auto describe = [] (const std::vector<FileEntry>& entries)
            {
                juce::StringArray lines;

                for (const auto& e : entries)
                    lines.add (e.file.getFullPathName() + " " + juce::String (e.size) + " " + juce::String (e.fileCount));

                lines.sort (true);
                return lines.joinIntoString ("|");
            };

            for (const bool recursive : { false, true })
            {
                disk.recursive = shared.recursive = recursive;
                expectEquals (describe (FileScanner::scan (shared, options)), describe (FileScanner::scan (disk, options)));
                expectEquals (describe (FileScanner::scanFiles (shared)), describe (FileScanner::scanFiles (disk)));
            }

            const auto sub = temp.root.getChildFile ("sub");
            expectEquals (describe (FileScanner::scanTree (sub, shared)), describe (FileScanner::scanTree (sub, disk)));
        }

        beginTest ("Junk folders: matched ignoring case, not descended");
        {
            TempFolder temp;
            temp.write ("proj/CMakeLists.txt", "project (p)");
            temp.write ("proj/build/x.obj", "x");
            temp.write ("proj/build/Debug/y.obj", "y");
            temp.write ("proj/src/main.cpp", "int main() {}");
            temp.write ("Music/Release/song.mp3", "a song");            // A generic name outside a project.
            temp.write ("web/node_modules/lib/index.js", "x");         // A specific name: anywhere.

            const auto result = run (*Criteria::createJunkFolders(), temp.root, true);
            expectEquals (namesOf (result).joinIntoString (","), juce::String ("build,node_modules"));
            expectEquals (findItem (result.groups[0], "build")->size, (juce::int64) 2);
        }

        beginTest ("Empty items: only the outermost empty folder");
        {
            TempFolder temp;
            temp.root.getChildFile ("empty/inner").createDirectory();
            temp.write ("zero.txt", {});
            temp.write ("full/a.txt", "a");
            temp.write ("pkg/__init__.py", {});                        // Placeholders and repository data are left out.
            temp.write ("repo/.git/HEAD", "ref");
            temp.root.getChildFile ("repo/.git/refs/heads").createDirectory();

            const auto result = run (*Criteria::createEmptyItems(), temp.root, true);
            expectEquals (namesOf (result).joinIntoString (","), juce::String ("empty,zero.txt"));

            const auto withRepository = run (*Criteria::createEmptyItems(), temp.root, true, [] (ParameterSet& p) { setParameter (p, "skipVersionControl", false); });
            expectEquals (namesOf (withRepository).joinIntoString (","), juce::String ("empty,refs,zero.txt"));
        }

#if JUCE_WINDOWS
        beginTest ("Empty items: folders that are only links are not empty");
        {
            TempFolder temp;
            temp.write ("target/data.txt", "important");
            temp.root.getChildFile ("holder/inner").createDirectory();
            temp.root.getChildFile ("truly empty").createDirectory();
            const auto link = temp.root.getChildFile ("holder/inner/link");

            juce::ChildProcess mklink;
            const auto command = "cmd /c mklink /J \"" + link.getFullPathName() + "\" \"" + temp.root.getChildFile ("target").getFullPathName() + "\"";

            if (mklink.start (command) && mklink.waitForProcessToFinish (10000) && link.isSymbolicLink())
            {
                expectEquals (namesOf (run (*Criteria::createEmptyItems(), temp.root, true)).joinIntoString ("|"), juce::String ("truly empty"));
            }
            else
            {
                logMessage ("Could not create a junction: this check was skipped.");
            }

            link.deleteFile();
        }
#endif

        beginTest ("Archives: zip matching its extracted folder");
        {
            TempFolder temp;
            temp.write ("Photos/a.txt", "alpha");
            temp.write ("Photos/sub/b.txt", "beta");

            writeZip (temp.root.getChildFile ("Photos.zip"), temp.root, { "Photos/a.txt", "Photos/sub/b.txt" });

            auto result = run (*Criteria::createArchiveMirrors(), temp.root);
            expectEquals ((int) result.groups.size(), 1);
            expect (findItem (result.groups[0], "Photos")->selected);
            expect (! findItem (result.groups[0], "Photos.zip")->selected);

            temp.write ("Photos/a.txt", "ALPHA");   // Same size, different content.
            result = run (*Criteria::createArchiveMirrors(), temp.root);
            expect (result.groups.empty());
        }

        beginTest ("Versions: grouped per folder and extension");
        {
            TempFolder temp;
            temp.write ("thesis.docx", "1");
            temp.write ("thesis_v2.docx", "2");
            temp.write ("thesis_v2.pdf", "3");
            temp.write ("other/thesis_v3.docx", "4");

            const auto result = run (*Criteria::createVersionedFiles(), temp.root, true);
            expectEquals ((int) result.groups.size(), 1);
            expectEquals ((int) result.groups[0].items.size(), 2);
        }
    }
};

//==============================================================================
class RegistryTests final : public juce::UnitTest
{
public:
    RegistryTests() : UnitTest ("Criteria registry", "Redoondant") {}

    void runTest() override
    {
        const auto criteria = Criteria::createAll();

        beginTest ("Every criterion has a unique id and a name");
        {
            juce::StringArray ids;

            for (const auto& criterion : criteria)
            {
                const auto& info = criterion->getInfo();
                expect (info.id.isNotEmpty() && info.name.isNotEmpty() && info.description.isNotEmpty(), info.id);
                expect (! ids.contains (info.id), "Duplicate id " + info.id);
                ids.add (info.id);
            }

            expectEquals (ids.size(), 15);
        }

        beginTest ("Criteria only read the parameters they declare");
        {
            TempFolder temp;
            temp.write ("a.txt", "same");
            temp.write ("a copy.txt", "same");
            temp.write ("sub/a.mp3", "x");
            temp.write ("sub/a.flac", "y");
            temp.write ("empty.txt", {});
            temp.write ("setup.exe", "z");

            const auto before = unknownParameterLookups().load();

            for (const auto& criterion : criteria)
                for (const bool recursive : { false, true })
                    run (*criterion, temp.root, recursive);

            expectEquals (unknownParameterLookups().load(), before);
        }
    }
};

static GroupingTests groupingTests;
static RegistryTests registryTests;
static RemovalTests removalTests;
static CriteriaTests criteriaTests;
