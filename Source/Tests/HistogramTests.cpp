/*  Tests of the histogram bars (buckets, layers, statistics). */

#include "TestHelpers.h"
#include "../Core/Histogram.h"

class HistogramTests final : public juce::UnitTest
{
public:
    HistogramTests() : UnitTest ("Histogram", "Redoondant") {}

    void runTest() override
    {
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
};

static HistogramTests histogramTests;
