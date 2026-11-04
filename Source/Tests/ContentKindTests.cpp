/*  Tests of the content kinds: one file's kind, the classification of folders and archives, the criterion and the list. */

#include "TestHelpers.h"
#include "../Core/ContentKind.h"
#include "../Criteria/AllCriteria.h"
#include "../UI/ResultsModel.h"

namespace
{
    constexpr juce::int64 mb = 1024 * 1024;

    ContentKind classify (const juce::File& file)
    {
        ContentKinds::Classifier classifier ({});
        return classifier.of (FileScanner::makeEntry (file));
    }
}

class ContentKindTests final : public juce::UnitTest
{
public:
    ContentKindTests() : UnitTest ("Content kinds", "Redoondant") {}

    void runTest() override
    {
        testFiles();
        testItems();
        testCriterion();
        testList();
    }

private:
    void expectKind (ContentKind actual, ContentKind expected, const juce::String& what)
    {
        expectEquals (ContentKinds::nameOf (actual), ContentKinds::nameOf (expected), what);
    }

    void testFiles()
    {
        beginTest ("A file's kind comes from its extension, and for audio and .ts from its name and size too");

        const std::vector<std::tuple<const char*, juce::int64, ContentKind>> cases {
            { "main.cpp", 1000, ContentKind::sourceCode },
            { "Photo.JPG", 1000, ContentKind::pictures },
            { "logo.psd", 1000, ContentKind::graphics },
            { "font.ttf", 1000, ContentKind::font },
            { "report.pdf", 1000, ContentKind::documents },
            { "table.csv", 1000, ContentKind::data },
            { "session.cpr", 1000, ContentKind::mediaProject },
            { "song.flac", 1000, ContentKind::music },
            { "x64/Reverb.vst3", 1000, ContentKind::audioPlugin },
            { "blob.xyz", 1000, ContentKind::other },
            { "README", 1000, ContentKind::other },
            { "short.wav", mb, ContentKind::samples },
            { "song.wav", 20 * mb, ContentKind::music },
            { "Kick_01.wav", 20 * mb, ContentKind::samples },
            { "Drums/long take.wav", 20 * mb, ContentKind::samples },
            { "120bpm groove.aiff", 20 * mb, ContentKind::samples },
            { "Screenshot.wav", 20 * mb, ContentKind::music },          // "shot" counts as a word only on its own.
            { "deck.pptx", 1000, ContentKind::slides },
            { "budget.xlsx", 1000, ContentKind::administration },
            { "export.csv", 1000, ContentKind::data },
            { "novel.epub", 1000, ContentKind::books },
            { "scan.pdf", 25 * mb, ContentKind::books },
            { "paper.pdf", mb, ContentKind::documents },
            { "Title 978-0-13-468599-1.pdf", mb, ContentKind::books },
            { "Title 978-0-13-468599-2.pdf", mb, ContentKind::documents },     // Wrong check digit.
            { "0-306-40615-2 Title.pdf", mb, ContentKind::books },
            { "export 1699999999.pdf", mb, ContentKind::documents },           // Ten bare digits: a timestamp, not an ISBN.
            { "Books/a.pdf", mb, ContentKind::books },
            { "invoice 2024.pdf", mb, ContentKind::documents },
            { "app.ts", 10000, ContentKind::sourceCode },
            { "clip.ts", 5 * mb, ContentKind::video },
            { "Folder\\name.mp4", 1000, ContentKind::video } };

        for (const auto& [path, size, expected] : cases)
            expectKind (ContentKinds::ofFile (path, size), expected, path);
    }

    void testItems()
    {
        TempFolder temp;

        beginTest ("Folders: the most files win, markers decide, ties go to the first kind");
        {
            for (const auto* path : { "code/main.cpp", "code/main.h", "code/logo.png",
                                      "project/CMakeLists.txt", "project/a.png", "project/b.png", "project/c.png", "project/d.png",
                                      "installer/setup.exe", "installer/readme.txt", "installer/notes.txt", "installer/more.txt",
                                      "inside/app/setup.exe", "inside/a.txt", "inside/b.txt",
                                      "deep/a/b/setup.exe", "deep/x.txt", "deep/y.txt",
                                      "tie/a.png", "tie/b.pdf",
                                      "repo/.git/config", "repo/notes.txt", "repo/todo.txt",
                                      "unknown/blob.xyz" })
                temp.write (path, "x");

            temp.root.getChildFile ("empty").createDirectory();

            expectKind (classify (temp.root.getChildFile ("code")), ContentKind::sourceCode, "code");
            expectKind (classify (temp.root.getChildFile ("project")), ContentKind::sourceCode, "project file");
            expectKind (classify (temp.root.getChildFile ("installer")), ContentKind::application, "installer at the top");
            expectKind (classify (temp.root.getChildFile ("inside")), ContentKind::application, "installer one level down");
            expectKind (classify (temp.root.getChildFile ("deep")), ContentKind::documents, "installer too deep");
            expectKind (classify (temp.root.getChildFile ("tie")), ContentKind::pictures, "tie");
            expectKind (classify (temp.root.getChildFile ("repo")), ContentKind::sourceCode, "repository");
            expectKind (classify (temp.root.getChildFile ("unknown")), ContentKind::other, "nothing recognised");
            expectKind (classify (temp.root.getChildFile ("empty")), ContentKind::other, "empty");
            expectKind (classify (temp.root.getChildFile ("installer/readme.txt")), ContentKind::documents, "a single file");
        }

        beginTest ("Spreadsheets bring their documents and pictures; slides and long PDFs have kinds of their own");
        {
            for (const auto* path : { "admin/budget.xlsx", "admin/a.pdf", "admin/b.pdf", "admin/c.pdf", "admin/scan1.jpg", "admin/scan2.jpg",
                                      "lesson/a.pdf", "lesson/b.pdf", "lesson/deck.pptx",
                                      "talks/a.pptx", "talks/b.pptx", "talks/c.pptx", "talks/notes.pdf",
                                      "library/long.pdf", "library/short.pdf", "library/longer.pdf" })
                temp.write (path, "x");

            expectKind (classify (temp.root.getChildFile ("admin")), ContentKind::administration, "admin");
            expectKind (classify (temp.root.getChildFile ("lesson")), ContentKind::documents, "lesson");
            expectKind (classify (temp.root.getChildFile ("talks")), ContentKind::slides, "talks");

            const auto pages = [] (const juce::File& f) { return f.getFileName().startsWith ("long") ? 300 : 10; };
            ContentKinds::Classifier counting ({}, pages);
            expectKind (counting.of (FileScanner::makeEntry (temp.root.getChildFile ("library/long.pdf"))), ContentKind::books, "long.pdf");
            expectKind (counting.of (FileScanner::makeEntry (temp.root.getChildFile ("library/short.pdf"))), ContentKind::documents, "short.pdf");
            expectKind (counting.of (FileScanner::makeEntry (temp.root.getChildFile ("library"))), ContentKind::books, "library");
        }

        beginTest ("Archives are listed; archives inside them are not opened");
        {
            temp.write ("src/a.ttf", "x");
            temp.write ("src/b.otf", "x");
            temp.write ("src/one.pdf", "x");
            writeZip (temp.root.getChildFile ("fonts.zip"), temp.root.getChildFile ("src"), { "a.ttf", "b.otf" });
            writeZip (temp.root.getChildFile ("src/inner.zip"), temp.root.getChildFile ("src"), { "a.ttf", "b.otf" });
            writeZip (temp.root.getChildFile ("nested.zip"), temp.root.getChildFile ("src"), { "inner.zip", "one.pdf" });
            temp.write ("broken.zip", "not an archive");

            expectKind (classify (temp.root.getChildFile ("fonts.zip")), ContentKind::font, "fonts.zip");
            expectKind (classify (temp.root.getChildFile ("nested.zip")), ContentKind::documents, "nested.zip");
            expectKind (classify (temp.root.getChildFile ("broken.zip")), ContentKind::other, "broken.zip");
        }

        beginTest ("A folder classified after one inside it gives the same kind");
        {
            for (const auto* path : { "outer/a.mp4", "outer/inner/b.png", "outer/inner/c.png", "outer/inner/d.png", "outer/e.mp4" })
                temp.write (path, "x");

            ContentKinds::Classifier shared ({});
            const auto inner = shared.of (FileScanner::makeEntry (temp.root.getChildFile ("outer/inner")));
            const auto outer = shared.of (FileScanner::makeEntry (temp.root.getChildFile ("outer")));

            expectKind (inner, ContentKind::pictures, "inner");
            expectKind (outer, classify (temp.root.getChildFile ("outer")), "outer");
            expectKind (outer, ContentKind::pictures, "outer");
        }
    }

    void testCriterion()
    {
        beginTest ("Content kind: a group per kind, top level only, nothing checked");

        TempFolder temp;

        for (const auto* path : { "code/main.cpp", "code/main.h", "photos/a.jpg", "photos/b.jpg", "manual.pdf", "notes.txt", "blob.xyz" })
            temp.write (path, "x");

        const auto criterion = Criteria::createContentKind();
        expect (criterion->getInfo().topLevelOnly && criterion->getInfo().exhaustive);

        const auto result = run (*criterion, temp.root, true);
        juce::StringArray titles;
        int checked = 0;

        for (const auto& group : result.groups)
        {
            titles.add (group.title.upToFirstOccurrenceOf ("  -  ", false, false));

            for (const auto& item : group.items)
            {
                checked += item.selected ? 1 : 0;
                expect (item.contentKind != ContentKind::unknown, item.name());
            }
        }

        expectEquals (titles.joinIntoString (","), juce::String ("Source code,Pictures,Documents,Other"));
        expectEquals (namesOf (result).joinIntoString (","), juce::String ("blob.xyz,code,manual.pdf,notes.txt,photos"));
        expectEquals (checked, 0);
    }

    void testList()
    {
        beginTest ("The list: a kind filter follows the kinds as they arrive, and an update keeps them");

        const auto picture = tempEntry ("a.png", 10);
        const auto document = tempEntry ("b.pdf", 20);

        ResultsModel model;
        model.setResult (AnalysisResult::flat ({ picture, document }), {}, false);
        model.setFilter ([] (const FileEntry& e) { return e.contentKind == ContentKind::pictures; }, { false, true });
        expectEquals ((int) model.getVisibleCount(), 0);
        expectEquals ((int) model.itemsNeedingContentKind().size(), 2);

        model.setContentKinds ({ { picture.file, ContentKind::pictures }, { document.file, ContentKind::documents } });
        expectEquals ((int) model.getVisibleCount(), 1);
        expect (model.itemsNeedingContentKind().empty());

        auto changed = picture;
        changed.size = 99;
        model.update ({ changed });
        expectEquals ((int) model.getVisibleCount(), 1);
        expectEquals (model.getVisibleEntries().front().size, (juce::int64) 99);
    }
};

static ContentKindTests contentKindTests;
