/*  Tests of the installer/junk lists, archive matching by content, and shell icons. */

#include "TestHelpers.h"
#include "../Core/ContentHasher.h"
#include "../Core/MoveToFolder.h"
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
        testLargeElements();
        testGroupCycling();
        testTypeFilter();
        testDates();
        testGroupOrder();
        testProgress();
        testMetadataDiff();
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

            expectEquals (names (run (*criterion, temp.root, true, tweak (false))).joinIntoString (","), juce::String ("big.bin"));
            expectEquals (names (run (*criterion, temp.root, true, tweak (true))).joinIntoString (","), juce::String ("big.bin,dir"));
        }
    }

    void testGroupCycling()
    {
        beginTest ("Group check boxes cycle default -> all -> none");

        const auto entry = [] (const juce::String& fileName, bool byDefault, bool missing = false)
        {
            FileEntry e;
            e.file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (fileName);
            e.selected = byDefault;
            e.missing = missing;
            return e;
        };

        AnalysisResult result;
        result.groups.push_back ({ "first", { entry ("a", true), entry ("b", false), entry ("gone", false, true) } });
        result.groups.push_back ({ "second", { entry ("c", true), entry ("d", false) } });

        ResultsModel model;
        model.setResult (std::move (result), {}, true);
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

        model.applyChecks (Checks::all);
        expect (model.getOverallState() == CheckState::all);
        expectEquals ((int) checked(), 3);              // a, b, c: "d" is hidden, "gone" is missing.
    }

    void testTypeFilter()
    {
        beginTest ("Filter bar: several types, and names to exclude");

        TempFolder temp;
        juce::PropertiesFile::Options options;
        options.filenameSuffix = ".settings";
        juce::PropertiesFile file (temp.root.getChildFile ("t.settings"), options);
        SettingsScope scope (file, "filter");

        const auto entry = [] (const juce::String& fileName)
        {
            FileEntry e;
            e.file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (fileName);
            return e;
        };

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
                return names (run (*criterion, temp.root, false, [&] (ParameterSet& p)
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
        }
    }

    void testGroupOrder()
    {
        beginTest ("Group order");

        const auto entry = [] (const juce::String& fileName, juce::int64 size, int daysAgo)
        {
            FileEntry e;
            e.file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (fileName);
            e.size = size;
            e.modified = juce::Time::getCurrentTime() - juce::RelativeTime::days (daysAgo);
            return e;
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

    void testProgress()
    {
        beginTest ("Removal progress and cancellation");

        TempFolder temp;
        std::vector<FileEntry> entries;

        for (int i = 0; i < 4; ++i)
            entries.push_back (FileEntry::fromFile (temp.write ("f" + juce::String (i) + ".txt", juce::String::repeatedString ("x", 100 * (i + 1)))));

        std::vector<double> fractions;
        juce::StringArray seen;

        const auto report = Trash::deletePermanently (entries, [&] (double fraction, const juce::String& itemName)
        {
            fractions.push_back (fraction);
            seen.add (itemName);
            return true;
        });

        expectEquals (report.moved, 4);
        expect (! report.cancelled);
        expectEquals ((int) fractions.size(), 4);
        expectEquals (fractions.front(), 0.0);
        expect (std::is_sorted (fractions.begin(), fractions.end()) && fractions.back() < 1.0);
        expectEquals (seen.joinIntoString (","), juce::String ("f0.txt,f1.txt,f2.txt,f3.txt"));

        // Cancelling stops before the next item and leaves the rest untouched.
        std::vector<FileEntry> more;

        for (int i = 0; i < 4; ++i)
            more.push_back (FileEntry::fromFile (temp.write ("g" + juce::String (i) + ".txt", "y")));

        const auto dest = temp.root.getChildFile ("dest");
        dest.createDirectory();
        int calls = 0;
        const auto partial = MoveToFolder::run (more, dest, [&] (double, const juce::String&) { return ++calls <= 2; });

        expect (partial.cancelled);
        expectEquals (partial.moved, 2);
        expectEquals (dest.getNumberOfChildFiles (juce::File::findFiles), 2);
        expectEquals (temp.root.getNumberOfChildFiles (juce::File::findFiles), 2);      // g2, g3 were left where they were.
    }

    void testMetadataDiff()
    {
        beginTest ("Metadata diff keeps only what differs");

        Metadata a, b, c;

        for (auto* m : { &a, &b, &c })
        {
            m->add ("Base.Path", "Path", m == &a ? "/a" : (m == &b ? "/b" : "/c"));      // Always different.
            m->add ("Base.Type", "Type", "Image");                                         // Shared by all.
        }

        a.add ("Photo.Camera", "Camera", "X100");
        b.add ("Photo.Camera", "Camera", "X100");
        c.add ("Photo.Camera", "Camera", "Z7");                                            // Differs in one file.
        a.add ("Photo.Lens", "Lens", "35mm");                                              // Missing in the others.
        a.add ({}, "Note", "no key");                                                      // Items without a key are kept.

        const auto result = MetadataDiff::differing ({ a, b, c });
        const auto keys = [&] (size_t i) { juce::StringArray k; for (const auto& item : result[i].items()) k.add (item.key.isEmpty() ? "-" : item.key); return k.joinIntoString (","); };

        expectEquals (keys (0), juce::String ("Base.Path,Photo.Camera,Photo.Lens,-"));
        expectEquals (keys (1), juce::String ("Base.Path,Photo.Camera"));
        expectEquals (keys (2), juce::String ("Base.Path,Photo.Camera"));

        // Identical files differ only by path.
        expectEquals ((int) MetadataDiff::differing ({ b, b })[0].items().size(), 0);
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
