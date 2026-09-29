/*  Tests of the Cache Cleaner building blocks. */

#include "TestHelpers.h"
#include "../Core/EmptyFolders.h"
#include "../Core/MoveToFolder.h"
#include "../Core/Settings.h"
#include "../Core/Trash.h"

class CleanupTests final : public juce::UnitTest
{
public:
    CleanupTests() : UnitTest ("Cache cleaner", "Redoondant") {}

    void runTest() override
    {
        testGrouping();
        testPaths();
        testEmptying();
    }

private:
    /** Names of the groups (relative to `base`) with their members, e.g. "A/B: Cache, Peak". */
    static juce::StringArray describe (const juce::File& base, const std::vector<juce::File>& folders)
    {
        juce::StringArray lines;

        for (const auto& group : EmptyFolders::groupByCommonParent (folders))
        {
            juce::StringArray names;

            for (auto index : group.members)
                names.add (folders[index].getRelativePathFrom (group.parent));

            lines.add (group.parent.getRelativePathFrom (base) + ": " + names.joinIntoString (", "));
        }

        return lines;
    }

    void testGrouping()
    {
        beginTest ("Folders are grouped under the deepest shared parent");
        {
            const auto base = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("grouping");
            const auto f = [&] (const juce::String& path) { return base.getChildFile (path); };

            const auto lines = describe (base, { f ("Roaming/Adobe/Common/Media Cache Files"), f ("Roaming/Adobe/Common/Peak Files"),
                                                 f ("Roaming/Microsoft/Teams/Cache"), f ("Roaming/Microsoft/Teams/tmp"),
                                                 f ("Local/NVIDIA/DXCache"), f ("Local/NVIDIA/GLCache"),
                                                 f ("Local/Temp"), f ("Local/pip/Cache") });

            const juce::String sep ("\\");
            expectEquals (lines.joinIntoString ("\n"),
                          "Local: pip" + sep + "Cache, Temp\n"
                          "Local" + sep + "NVIDIA: DXCache, GLCache\n"
                          "Roaming" + sep + "Adobe" + sep + "Common: Media Cache Files, Peak Files\n"
                          "Roaming" + sep + "Microsoft" + sep + "Teams: Cache, tmp");
        }
    }

    void testPaths()
    {
        beginTest ("Path helpers");

        expectEquals (EmptyFolders::expand ("%LOCALAPPDATA%\\x"), juce::SystemStats::getEnvironmentVariable ("LOCALAPPDATA", {}) + "\\x");
        expectEquals (EmptyFolders::expand ("%NO_SUCH_VARIABLE_%\\x"), juce::String ("%NO_SUCH_VARIABLE_%\\x"));
        expectEquals (EmptyFolders::expand ("50% of %"), juce::String ("50% of %"));

        const auto local = juce::SystemStats::getEnvironmentVariable ("LOCALAPPDATA", {});
        expectEquals (EmptyFolders::contract (local + "\\Some\\Cache"), juce::String ("%LOCALAPPDATA%\\Some\\Cache"));
        expectEquals (EmptyFolders::contract ("Z:\\elsewhere"), juce::String ("Z:\\elsewhere"));

        beginTest ("Defaults don't depend on the user");

        const auto user = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFileName();

        for (const auto& d : EmptyFolders::defaults())
        {
            expect (! d.path.containsIgnoreCase (user), d.path);
            expect (! EmptyFolders::isTooBroad (juce::File (EmptyFolders::expand (d.path))), d.path);
        }

        beginTest ("Broad folders are refused");
        expect (EmptyFolders::isTooBroad (juce::File ("C:\\")));
        expect (EmptyFolders::isTooBroad (juce::File::getSpecialLocation (juce::File::userHomeDirectory)));
        expect (EmptyFolders::isTooBroad (juce::File (juce::SystemStats::getEnvironmentVariable ("APPDATA", {}))));
        expect (EmptyFolders::isTooBroad (juce::File (juce::SystemStats::getEnvironmentVariable ("APPDATA", {})).getParentDirectory()));
        expect (! EmptyFolders::isTooBroad (juce::File::getSpecialLocation (juce::File::tempDirectory)));
    }

    void testEmptying()
    {
        beginTest ("Emptying a folder keeps the folder");
        {
            TempFolder temp;
            temp.write ("cache/a.bin", "aaaa");
            temp.write ("cache/sub/b.bin", "bb");
            const auto cache = temp.root.getChildFile ("cache");

            const auto contents = EmptyFolders::contentsOf (cache);
            expectEquals ((int) contents.size(), 2);

            const auto report = Trash::deletePermanently (contents);
            expectEquals (report.moved, 2);
            expectEquals (report.bytesMoved, (juce::int64) 6);
            expect (report.failures.isEmpty());
            expect (cache.isDirectory());
            expectEquals (cache.getNumberOfChildFiles (juce::File::findFilesAndDirectories), 0);
        }

        beginTest ("Contents can be moved out of the folder");
        {
            TempFolder temp;
            temp.write ("cache/a.bin", "aaaa");
            temp.write ("cache/sub/b.bin", "bb");
            const auto cache = temp.root.getChildFile ("cache");
            const auto dest = temp.root.getChildFile ("moved");
            dest.createDirectory();

            const auto report = MoveToFolder::run (EmptyFolders::contentsOf (cache), dest);
            expectEquals (report.moved, 2);
            expect (cache.isDirectory() && cache.getNumberOfChildFiles (juce::File::findFilesAndDirectories) == 0);
            expect (dest.getChildFile ("sub/b.bin").existsAsFile());
        }

        beginTest ("Permanent deletion prunes nested entries");
        {
            TempFolder temp;
            const auto inner = temp.write ("tree/x.txt", "x");
            const auto tree = inner.getParentDirectory();

            auto report = Trash::deletePermanently ({ FileEntry::fromFile (tree), FileEntry::fromFile (inner) });
            expectEquals (report.moved, 1);
            expect (! tree.exists());

        }

        beginTest ("Lists in settings");
        {
            TempFolder temp;
            juce::PropertiesFile::Options options;
            options.filenameSuffix = ".settings";
            juce::PropertiesFile file (temp.root.getChildFile ("t.settings"), options);
            SettingsScope scope (file, "cleanup");

            expect (! scope.has ("folders"));
            scope.setList ("folders", { "%TEMP%", "D:\\a b\\c" });
            expect (scope.has ("folders"));
            expectEquals (scope.getList ("folders").joinIntoString ("+"), juce::String ("%TEMP%+D:\\a b\\c"));
        }
    }
};

static CleanupTests cleanupTests;
