/*  Tests of the installer/junk lists, archive matching by content, and shell icons. */

#include "TestHelpers.h"
#include "../Core/ContentHasher.h"
#include "../Core/Settings.h"
#include "../Criteria/AllCriteria.h"
#include "../Platform/ComInit.h"
#include "../Platform/ShellIcon.h"
#include "../UI/ParametersPanel.h"

class FilterTests final : public juce::UnitTest
{
public:
    FilterTests() : UnitTest ("Lists, archives by content, icons", "Redoondant") {}

    void runTest() override
    {
        testInstallersAndJunk();
        testArchivesByContent();
        testIcons();
        testDuplicateAlgorithms();
    }

private:
    static juce::StringArray names (const AnalysisResult& result)
    {
        juce::StringArray found;

        for (const auto& group : result.groups)
            for (const auto& item : group.items)
                found.add (item.name());

        found.sort (true);
        return found;
    }

    static void writeZip (const juce::File& zip, const juce::File& base, const juce::StringArray& relativePaths, const juce::String& rootName = {})
    {
        juce::ZipFile::Builder builder;

        for (const auto& path : relativePaths)
            builder.addFile (base.getChildFile (path), 9, rootName.isEmpty() ? path : rootName + "/" + path);

        juce::FileOutputStream out (zip);
        builder.writeToStream (out, nullptr);
    }

    void testInstallersAndJunk()
    {
        beginTest ("Installers & junk: each list can be switched off");
        {
            TempFolder temp;

            for (const auto* fileName : { "setup.exe", "song.peak", "clip.wav.asd", "x.reapeaks", "p.RPP-bak", "a.gpk", "a.mrk", "keep.txt" })
                temp.write (fileName, "x");

            const auto criterion = Criteria::createInstallersAndJunkFiles();
            const auto matches = [&] (bool installers, bool junk)
            {
                return names (run (*criterion, temp.root, false, [&] (ParameterSet& p)
                {
                    for (auto& parameter : p.all())
                        parameter.enabled = parameter.id == "installerPatterns" ? installers : junk;
                }));
            };

            expectEquals (matches (true, true).joinIntoString (","), juce::String ("a.gpk,clip.wav.asd,p.RPP-bak,setup.exe,song.peak,x.reapeaks"));
            expectEquals (matches (true, false).joinIntoString (","), juce::String ("setup.exe"));
            expectEquals (matches (false, true).joinIntoString (","), juce::String ("a.gpk,clip.wav.asd,p.RPP-bak,song.peak,x.reapeaks"));
            expectEquals (matches (false, false).size(), 0);
        }

        beginTest ("Toggleable lists are persisted and reset");
        {
            TempFolder temp;
            juce::PropertiesFile::Options options;
            options.filenameSuffix = ".settings";
            juce::PropertiesFile file (temp.root.getChildFile ("t.settings"), options);
            SettingsScope scope (file, "installers.param");

            auto parameters = Criteria::createInstallersAndJunkFiles()->createParameters();
            expect (parameters.isEnabled ("junkPatterns"));

            scope.set ("junkPatterns.on", false);
            scope.set ("installerPatterns", "zzz");

            ParametersPanel panel (parameters, scope);
            expect (! parameters.isEnabled ("junkPatterns"));
            expect (parameters.isEnabled ("installerPatterns"));
            expectEquals (parameters.getText ("installerPatterns"), juce::String ("zzz"));

            panel.resetToDefaults();
            expect (parameters.isEnabled ("junkPatterns"));
            expect (parameters.getText ("installerPatterns").startsWith ("exe;"));
        }
    }

    void testArchivesByContent()
    {
        beginTest ("Archives: ignoring the folder name");
        {
            TempFolder temp;
            temp.write ("content/a.txt", "alpha");
            temp.write ("content/sub/b.txt", "beta");

            // The same files, in a folder with another name, as a flat zip and as a zip with a differently named root.
            temp.write ("Renamed/a.txt", "alpha");
            temp.write ("Renamed/sub/b.txt", "beta");
            writeZip (temp.root.getChildFile ("flat.zip"), temp.root.getChildFile ("content"), { "a.txt", "sub/b.txt" });
            writeZip (temp.root.getChildFile ("rooted.zip"), temp.root.getChildFile ("content"), { "a.txt", "sub/b.txt" }, "Other");

            temp.root.getChildFile ("content").deleteRecursively();      // Only "Renamed" remains.

            const auto criterion = Criteria::createArchiveMirrors();

            expect (run (*criterion, temp.root).groups.empty());       // Default: names must match.

            const auto ignoringName = run (*criterion, temp.root, false, [] (ParameterSet& p) { setParameter (p, "ignoreName", true); });
            expectEquals ((int) ignoringName.groups.size(), 2);

            for (const auto& group : ignoringName.groups)
            {
                expect (findItem (group, "Renamed")->selected);
                expect (! (findItem (group, "flat.zip") != nullptr ? findItem (group, "flat.zip") : findItem (group, "rooted.zip"))->selected);
            }

            temp.write ("Renamed/a.txt", "ALPHA");      // Same size, other bytes.
            expect (run (*criterion, temp.root, false, [] (ParameterSet& p) { setParameter (p, "ignoreName", true); }).groups.empty());

            temp.write ("Renamed/a.txt", "alpha");
            temp.write ("Renamed/extra.txt", "one more file");
            expect (run (*criterion, temp.root, false, [] (ParameterSet& p) { setParameter (p, "ignoreName", true); }).groups.empty());
        }
    }

    void testDuplicateAlgorithms()
    {
        beginTest ("Duplicates: both algorithms agree");
        {
            TempFolder temp;
            temp.write ("a.txt", "same content");
            temp.write ("b copy.txt", "same content");
            temp.write ("c.txt", "same length!");
            temp.write ("sub/d.txt", "unique");

            const auto criterion = Criteria::createDuplicateFiles();

            for (const int algorithm : { 0, 1 })
            {
                const auto result = run (*criterion, temp.root, true, [&] (ParameterSet& p) { setParameter (p, "algorithm", algorithm); });
                expectEquals ((int) result.groups.size(), 1);
                expectEquals (names (result).joinIntoString (","), juce::String ("a.txt,b copy.txt"));
                expect (findItem (result.groups[0], "b copy.txt")->selected);
            }

            expectEquals (ContentHasher::checksum (temp.root.getChildFile ("a.txt"), {}), ContentHasher::checksum (temp.root.getChildFile ("b copy.txt"), {}));
            expect (ContentHasher::checksum (temp.root.getChildFile ("a.txt"), {}) != ContentHasher::checksum (temp.root.getChildFile ("c.txt"), {}));

            ScanContext cancelled;
            cancelled.shouldStop = [] { return true; };
            expect (ContentHasher::checksum (temp.root.getChildFile ("a.txt"), cancelled).isEmpty());
        }
    }

    void testIcons()
    {
        beginTest ("System icons");
        {
            const ScopedComInit com;
            TempFolder temp;
            const auto text = temp.write ("note.txt", "x");

            expect (ShellIcon::get (text, 32).isValid());
            expect (ShellIcon::get (juce::File::getSpecialLocation (juce::File::currentExecutableFile), 32).isValid());
            expect (ShellIcon::get (temp.root, 32).isValid());      // Folders too.
        }
    }
};

static FilterTests filterTests;
