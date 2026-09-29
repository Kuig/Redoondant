/*  Unit tests for the analysis logic. Run with: Redoondant.exe --test */

#include "TestHelpers.h"
#include "../Core/FileScanner.h"
#include "../Core/Grouping.h"
#include "../Core/Trash.h"
#include "../Criteria/AllCriteria.h"

namespace
{
    FileEntry entry (const juce::String& name, juce::Time modified = {})
    {
        FileEntry e;
        e.file = juce::File::getCurrentWorkingDirectory().getChildFile (name);
        e.modified = modified;
        return e;
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
class TrashTests final : public juce::UnitTest
{
public:
    TrashTests() : UnitTest ("Trash", "Redoondant") {}

    void runTest() override
    {
        beginTest ("Nested and duplicate entries are removed");
        const auto root = juce::File::getCurrentWorkingDirectory();
        const auto make = [&] (const juce::String& path)
        {
            FileEntry e;
            e.file = root.getChildFile (path);
            return e;
        };

        const auto kept = Trash::withoutNested ({ make ("a"), make ("a/b.txt"), make ("a/c/d"), make ("a b"), make ("a b"), make ("e.txt") });
        expectEquals ((int) kept.size(), 3);
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
        }

        beginTest ("Junk folders: matched ignoring case, not descended");
        {
            TempFolder temp;
            temp.write ("proj/build/x.obj", "x");
            temp.write ("proj/build/Debug/y.obj", "y");
            temp.write ("proj/src/main.cpp", "int main() {}");

            const auto result = run (*Criteria::createJunkFolders(), temp.root, true);
            expectEquals ((int) result.groups[0].items.size(), 1);
            expectEquals (result.groups[0].items[0].name(), juce::String ("build"));
            expectEquals (result.groups[0].items[0].size, (juce::int64) 2);
        }

        beginTest ("Empty items: only the outermost empty folder");
        {
            TempFolder temp;
            temp.root.getChildFile ("empty/inner").createDirectory();
            temp.write ("zero.txt", {});
            temp.write ("full/a.txt", "a");

            const auto result = run (*Criteria::createEmptyItems(), temp.root, true);
            expectEquals ((int) result.groups[0].items.size(), 2);
        }

        beginTest ("Archives: zip matching its extracted folder");
        {
            TempFolder temp;
            temp.write ("Photos/a.txt", "alpha");
            temp.write ("Photos/sub/b.txt", "beta");

            juce::ZipFile::Builder builder;
            builder.addFile (temp.root.getChildFile ("Photos/a.txt"), 9, "Photos/a.txt");
            builder.addFile (temp.root.getChildFile ("Photos/sub/b.txt"), 9, "Photos/sub/b.txt");
            {
                juce::FileOutputStream out (temp.root.getChildFile ("Photos.zip"));
                builder.writeToStream (out, nullptr);
            }

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

static GroupingTests groupingTests;
static TrashTests trashTests;
static CriteriaTests criteriaTests;
