/*  Tests of the histogram bars (buckets, layers, statistics). */

#include "TestHelpers.h"
#include "../UI/ResultsModel.h"
#include "../UI/FilterBar.h"
#include "../Core/Histogram.h"

class HistogramTests final : public juce::UnitTest
{
public:
    HistogramTests() : UnitTest ("Histogram", "Redoondant") {}

    void runTest() override
    {
        testBinFilters();
        testBuckets();
        testBars();
        testFolders();
        testFolderOf();
    }

private:
    static constexpr juce::int64 mb = 1024 * 1024;

    static FileEntry entry (const juce::String& path, juce::int64 size, juce::Time modified = {}, bool folder = false)
    {
        FileEntry e;
        e.file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("histogram").getChildFile (path);
        e.size = size;
        e.modified = modified;
        e.isDirectory = folder;
        return e;
    }

    void testBuckets()
    {
        beginTest ("Bucket boundaries");

        using namespace Histogram::Buckets;
        expectEquals (sizeBucket (mb - 1), 0);
        expectEquals (sizeBucket (mb), 1);
        expectEquals (sizeBucket (10 * mb), 2);
        expectEquals (sizeBucket (100 * mb), 3);
        expectEquals (sizeBucket (1024 * mb), 4);

        // The filter range of a bucket takes exactly the sizes of the bucket.
        for (int b = 0; b < sizeNames().size(); ++b)
        {
            const auto range = sizeRange (b);
            const auto low = (juce::int64) (range.fromMB * (double) mb);
            const auto high = range.toMB > 0 ? (juce::int64) (range.toMB * (double) mb) : std::numeric_limits<juce::int64>::max();
            expectEquals (sizeBucket (low), b);
            expectEquals (sizeBucket (high), b);
        }

        const auto now = juce::Time::getCurrentTime();
        const auto ago = [&] (double days) { return now - juce::RelativeTime::days (days); };
        expectEquals (dateBucket (ago (0.5), now), 0);
        expectEquals (dateBucket (now + juce::RelativeTime::days (3), now), 0);     // A date in the future is "today".
        expectEquals (dateBucket (ago (1.5), now), 1);
        expectEquals (dateBucket (ago (7.5), now), 2);
        expectEquals (dateBucket (ago (31), now), 3);
        expectEquals (dateBucket (ago (400), now), 4);
        expectEquals (dateBucket (ago (800), now), 5);
        expectEquals (dateBucket (juce::Time(), now), unknownDate);
    }

    void testBars()
    {
        beginTest ("Bars: layers, statistics, and the optional bars");

        const auto now = juce::Time::getCurrentTime();
        const auto old = now - juce::RelativeTime::days (900);
        const auto recent = now - juce::RelativeTime::hours (2);

        Histogram::Input input;
        input.all = { entry ("a.txt", 100, recent), entry ("b.txt", 300, old), entry ("c.png", 5 * mb, old), entry ("d.mp3", 2000 * mb, recent) };
        input.visible = { input.all[1], input.all[2], input.all[3] };       // "a" is filtered out.
        input.checked = { input.all[2] };

        const auto sizes = Histogram::compute (input, Property::size, Metric::count, {}, now);
        expectEquals ((int) sizes.size(), 5);
        expectEquals (sizes[0].total.count, 2);
        expectEquals (sizes[0].filtered.count, 1);
        expectEquals (sizes[0].total.bytes, (juce::int64) 400);
        expectEquals (sizes[0].total.average(), (juce::int64) 200);
        expectEquals (sizes[0].total.largestName, juce::String ("b.txt"));
        expectEquals (sizes[1].checked.count, 1);
        expectEquals (sizes[4].total.count, 1);

        for (const auto& bin : sizes)       // Each layer is contained in the previous one.
            expect (bin.checked.count <= bin.filtered.count && bin.filtered.count <= bin.total.count);

        const auto dates = Histogram::compute (input, Property::modified, Metric::count, {}, now);
        expectEquals ((int) dates.size(), 6);                       // No unknown date: not listed.
        expectEquals (dates[0].total.count, 2);
        expectEquals (dates[5].total.count, 2);
        expect (dates[5].total.oldest <= dates[5].total.newest);

        const auto content = Histogram::compute (input, Property::contentCreated, Metric::count, {}, now);
        expectEquals ((int) content.size(), 7);                     // Nothing was read: everything is unknown.
        expectEquals (content.back().total.count, 4);
        expectEquals (content.back().label, juce::String ("Unknown date"));

        const auto types = Histogram::compute (input, Property::type, Metric::size, {}, now);
        expectEquals ((int) types.size(), 3);                       // Text, image, audio: only the types present.
    }

    void testFolderOf()
    {
        beginTest ("Folder of an entry, relative to the analysed folder");

        const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("histogram");
        const auto reference = [&] (const juce::File& file, const juce::File& base)
        {
            const auto parent = file.getParentDirectory();
            return base == juce::File() ? parent.getFullPathName() : (parent == base ? juce::String() : parent.getRelativePathFrom (base));
        };

        for (const auto& path : { "a.txt", "sub/b.txt", "sub/deeper/c.txt", "sub with space/d.txt" })
        {
            FileEntry e;
            e.file = root.getChildFile (path);
            expectEquals (Histogram::folderOf (e, root), reference (e.file, root));
            expectEquals (Histogram::folderOf (e, {}), reference (e.file, {}));
        }

        FileEntry atRoot;
        atRoot.file = root.getChildFile ("x");
        expectEquals (Histogram::folderOf (atRoot, root), juce::String());
        expectEquals (Histogram::folderOf (atRoot, root.getChildFile ("sub")), reference (atRoot.file, root.getChildFile ("sub")));      // Outside the root.

        FileEntry onDrive;
        onDrive.file = juce::File ("C:\\file.txt");
        expectEquals (Histogram::folderOf (onDrive, {}), reference (onDrive.file, {}));
    }

    void testFolders()
    {
        beginTest ("Folder bars: ranked, limited, root and others");

        const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("histogram");
        Histogram::Input input;
        input.all.push_back (entry ("top.txt", 1));

        for (int folder = 0; folder < 12; ++folder)
            for (int i = 0; i <= folder; ++i)
                input.all.push_back (entry ("f" + juce::String (folder) + "/file" + juce::String (i), 1));

        input.visible = input.all;

        const auto bins = Histogram::compute (input, Property::folder, Metric::count, root, juce::Time::getCurrentTime());
        expectEquals ((int) bins.size(), 11);                       // Ten folders and "Others".
        expectEquals (bins[0].label, juce::String ("f11"));         // The fullest first.
        expectEquals (bins[0].total.count, 12);
        expect (bins[0].filterable && bins[0].folder == "f11");
        expectEquals (bins.back().label, juce::String ("Others"));
        expect (! bins.back().filterable);
        expectEquals (bins.back().total.count, 3 + 1);              // f0 (1), f1 (2) and the root (1), the three smallest.

        int total = 0;

        for (const auto& bin : bins)
            total += bin.total.count;

        expectEquals (total, (int) input.all.size());               // No entry is lost.
    }

    void testBinFilters()
    {
        beginTest ("A bar sets the filters that show exactly its entries");

        TempFolder temp;
        juce::PropertiesFile::Options options;
        options.filenameSuffix = ".settings";
        juce::PropertiesFile file (temp.root.getChildFile ("b.settings"), options);

        const auto now = juce::Time::getCurrentTime();
        Histogram::Input input;
        int n = 0;

        for (const auto& [fileName, size, days] : std::vector<std::tuple<const char*, juce::int64, double>> {
                 { "a.txt", mb / 2, 0.1 }, { "sub/b.png", 5 * mb, 3 }, { "sub/c.mp3", 50 * mb, 10 }, { "other/d.txt", 500 * mb, 100 },
                 { "other/e.png", 2048 * mb, 500 }, { "f.mp3", 10 * mb, 900 }, { "g.txt", mb - 1, 6.9 }, { "h.txt", 10 * mb, 29 } })
        {
            FileEntry e;
            e.file = temp.root.getChildFile (fileName);
            e.size = size;
            e.modified = e.created = now - juce::RelativeTime::days (days);
            input.all.push_back (e);
        }

        input.visible = input.all;

        for (int p = 0; p < Histogram::propertyNames().size(); ++p)
        {
            const auto property = (Property) p;

            for (const auto& bin : Histogram::compute (input, property, Metric::count, temp.root, now))
            {
                if (! bin.filterable)
                    continue;

                FilterBar bar (SettingsScope (file, "bar" + juce::String (n++)));
                bar.setRoot (temp.root);
                bar.applyBin (property, bin);
                const auto filter = bar.createFilter();

                for (const auto& e : input.all)
                {
                    Histogram::Input single;
                    single.all = { e };
                    const auto where = Histogram::compute (single, property, Metric::count, temp.root, now);
                    const bool inBar = std::any_of (where.begin(), where.end(), [&] (const Histogram::Bin& b) { return b.total.count > 0 && b.label == bin.label; });
                    expect (filter (e) == inBar, Histogram::nameOf (property) + " / " + bin.label + " / " + e.name());
                }

                bar.applyBin (property, bin);       // Again: the filter is taken off.
                const auto off = bar.createFilter();
                expect (std::all_of (input.all.begin(), input.all.end(), [&] (const FileEntry& e) { return off (e); }));
            }
        }
    }
};

static HistogramTests histogramTests;
