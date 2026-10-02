/*  Tests of PDF support and of the name/metadata based criteria. */

#include "TestHelpers.h"
#include "../Core/Grouping.h"
#include "../Criteria/AllCriteria.h"
#include "../Pdf/PdfDocument.h"
#include <map>

namespace
{
    /** A one-page PDF with an Info dictionary, with a correct cross-reference table. */
    juce::String minimalPdf (const juce::String& title)
    {
        const juce::StringArray objects
        {
            "<< /Type /Catalog /Pages 2 0 R >>",
            "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 100] >>",
            "<< /Title (" + title + ") /Author (Tester) >>",
        };

        juce::String pdf = "%PDF-1.4\n";
        juce::Array<int> offsets;

        for (int i = 0; i < objects.size(); ++i)
        {
            offsets.add ((int) pdf.getNumBytesAsUTF8());
            pdf << (i + 1) << " 0 obj\n" << objects[i] << "\nendobj\n";
        }

        const auto xref = pdf.getNumBytesAsUTF8();
        pdf << "xref\n0 " << (objects.size() + 1) << "\n0000000000 65535 f \n";

        for (auto offset : offsets)
            pdf << juce::String (offset).paddedLeft ('0', 10) << " 00000 n \n";

        pdf << "trailer\n<< /Size " << (objects.size() + 1) << " /Root 1 0 R /Info 4 0 R >>\nstartxref\n" << (int) xref << "\n%%EOF\n";
        return pdf;
    }

    Metadata tags (const juce::String& artist, const juce::String& title, double seconds)
    {
        Metadata m;
        m.add (MetadataKeys::artist, "Artist", artist);
        m.add (MetadataKeys::title, "Title", title);
        m.add (MetadataKeys::duration, "Length", juce::String (seconds), seconds);
        return m;
    }
}

class MediaTests final : public juce::UnitTest
{
public:
    MediaTests() : UnitTest ("Media & metadata", "Redoondant") {}

    void runTest() override
    {
        beginTest ("PDF metadata and rendering");
        {
            TempFolder temp;
            const auto pdf = temp.write ("doc.pdf", minimalPdf ("Quarterly Report"));

            expect (PdfDocument::isAvailable(), "pdfium.dll must be next to the executable");
            const auto metadata = PdfDocument::readMetadata (pdf);
            expectEquals (metadata.text (MetadataKeys::title), juce::String ("Quarterly Report"));
            expectEquals (metadata.text (MetadataKeys::author), juce::String ("Tester"));
            expectEquals (metadata.text (MetadataKeys::pageCount), juce::String ("1"));

            const auto page = PdfDocument::renderPage (pdf, 0, 400);
            expectEquals (page.getWidth(), 400);
            expectEquals (page.getHeight(), 200);
        }

        beginTest ("Gap clustering on numbers");
        {
            const auto numbered = [] (double v) { FileEntry e; e.size = (juce::int64) v; return e; };
            const auto groups = Grouping::clusterByGap ({ numbered (10), numbered (11), numbered (30), numbered (31.5) },
                                                        [] (const FileEntry& e) { return (double) e.size; }, 2.0);
            expectEquals ((int) groups.size(), 2);
        }

        beginTest ("Same name, different extension");
        {
            TempFolder temp;
            temp.write ("clip.mp4", "a");
            temp.write ("Clip.mkv", "b");
            temp.write ("notes.txt", "c");
            temp.write ("other/clip.avi", "d");

            auto result = run (*Criteria::createSameName(), temp.root, true);
            expectEquals ((int) result.groups.size(), 1);
            expectEquals ((int) result.groups[0].items.size(), 2);
            expect (! result.groups[0].items[0].selected && ! result.groups[0].items[1].selected);

            result = run (*Criteria::createSameName(), temp.root, true,
                          [] (ParameterSet& p) { setParameter (p, "acrossFolders", true); });
            expectEquals ((int) result.groups[0].items.size(), 3);
        }

        beginTest ("Similar metadata, different format");
        {
            TempFolder temp;
            temp.write ("song.flac", juce::String::repeatedString ("x", 1000));
            temp.write ("song 128k.mp3", "small");
            temp.write ("other.mp3", "other");
            temp.write ("live.ogg", "live");

            std::map<juce::String, Metadata> fake
            {
                { "song.flac",     tags ("Band", "Song", 200.0) },
                { "song 128k.mp3", tags ("Band", "  song ", 201.0) },   // Normalised; duration within tolerance.
                { "other.mp3",     tags ("Band", "Other", 200.0) },
                { "live.ogg",      tags ("Band", "Song", 260.0) },      // Same tags, different duration.
            };

            const auto criterion = Criteria::createSameContent ([&] (const juce::File& f) { return fake[f.getFileName()]; });
            const auto result = run (*criterion, temp.root);

            expectEquals ((int) result.groups.size(), 1);
            expectEquals ((int) result.groups[0].items.size(), 2);
            expect (! findItem (result.groups[0], "song.flac")->selected);
            expect (findItem (result.groups[0], "song 128k.mp3")->selected);
        }
    }
};

static MediaTests mediaTests;
