/*  Tests of criteria settings: installer/junk lists, archives by content, duplicate algorithms, large elements, dates. */

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

class CriteriaOptionTests final : public juce::UnitTest
{
public:
    CriteriaOptionTests() : UnitTest ("Criteria settings", "Redoondant") {}

    void runTest() override
    {
        testInstallersAndJunk();
        testArchivesByContent();
        testLargeElements();
        testDates();
    }

private:
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
                return namesOf (run (*criterion, temp.root, false, [&] (ParameterSet& p)
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
            TempSettings temp;
            auto scope = temp.scope ("installers.param");

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

    void testLargeElements()
    {
        beginTest ("Large elements: folders only with the flag");
        {
            TempFolder temp;
            temp.write ("big.bin", juce::String::repeatedString ("x", 3000));
            temp.write ("dir/a.bin", juce::String::repeatedString ("x", 1500));
            temp.write ("dir/b.bin", juce::String::repeatedString ("x", 1500));
            temp.write ("small.bin", "x");

            const auto criterion = Criteria::createLargeFiles();
            const auto tweak = [] (bool folders) { return [folders] (ParameterSet& p) { setParameter (p, "minSize", 2000.0 / 1048576.0); setParameter (p, "folders", folders); }; };

            expectEquals (namesOf (run (*criterion, temp.root, true, tweak (false))).joinIntoString (","), juce::String ("big.bin"));
            expectEquals (namesOf (run (*criterion, temp.root, true, tweak (true))).joinIntoString (","), juce::String ("big.bin,dir"));
        }
    }

    void testDates()
    {
        beginTest ("Content date: the oldest plausible metadata date");

        Metadata metadata;
        metadata.addDate ("a", "Taken", "x", juce::Time (2019, 4, 2, 10, 0));
        metadata.addDate ("b", "Created", "x", juce::Time (2015, 0, 15, 8, 30));
        metadata.addDate ("c", "Placeholder", "x", juce::Time (1970, 0, 1, 0, 1));                           // Before 1980: ignored.
        metadata.addDate ("d", "Future", "x", juce::Time::getCurrentTime() + juce::RelativeTime::days (30));  // Ignored.
        metadata.add ("e", "Not a date", "2001", (juce::int64) 978307200000ll);                              // Raw but not isDate.

        expect (ContentDate::oldestOf (metadata) == juce::Time (2015, 0, 15, 8, 30));
        expectEquals ((juce::int64) ContentDate::oldestOf (Metadata()).toMilliseconds(), (juce::int64) 0);

        beginTest ("Old files and date clusters use the chosen date");
        {
            TempFolder temp;
            const auto old = temp.write ("old.txt", "x");
            const auto recent = temp.write ("recent.txt", "x");
            const auto now = juce::Time::getCurrentTime();
            old.setLastModificationTime (now - juce::RelativeTime::days (900));
            recent.setLastModificationTime (now - juce::RelativeTime::days (10));

            const auto criterion = Criteria::createOldFiles();
            const auto olderThan = [&] (int kind, double days)
            {
                return namesOf (run (*criterion, temp.root, false, [&] (ParameterSet& p)
                {
                    setParameter (p, "dateKind", kind);
                    setParameter (p, "days", days);
                }));
            };

            expectEquals (olderThan (1, 800).joinIntoString (","), juce::String ("old.txt"));          // File modified over 800 days ago.
            expectEquals (olderThan (1, 5).joinIntoString (","), juce::String ("old.txt,recent.txt"));
            expectEquals (olderThan (2, 0).size(), 0);          // Content created: plain text has no real metadata date, so it is unknown, not old.

            const auto clusters = Criteria::createDateClusters();
            const auto grouped = [&] (int kind)
            {
                return (int) run (*clusters, temp.root, false, [&] (ParameterSet& p)
                {
                    setParameter (p, "date", kind);
                    setParameter (p, "gap", 1.0);
                }).groups.size();
            };

            expectEquals (grouped (1), 0);      // Modified 890 days apart: two singletons, below the minimum group size.
            expectEquals (grouped (2), 0);      // No content dates at all.

            // With a metadata source that knows a real document date, the content kind has something to work with.
            const auto written = juce::Time (2010, 5, 1, 12, 0);
            const MetadataSource fake = [written] (const juce::File& f)
            {
                Metadata m;

                if (f.getFileName() == "old.txt")
                    m.addDate ("System.Photo.DateTaken", "Date taken", "2010", written);

                return m;
            };

            const auto contentOld = namesOf (run (*Criteria::createOldFiles (fake), temp.root, false, [] (ParameterSet& p)
            {
                setParameter (p, "dateKind", (int) DateKind::content);
                setParameter (p, "days", 800);
            }));
            expectEquals (contentOld.joinIntoString (","), juce::String ("old.txt"));      // recent.txt has no content date: unknown, not old.
        }
    }
};

static CriteriaOptionTests criteriaOptionTests;
