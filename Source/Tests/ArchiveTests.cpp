/*  Tests of the libarchive-based reader and of the "Archives & extracted folders" criterion. */

#include "TestHelpers.h"
#include "../Archives/ArchiveReader.h"
#include "../Criteria/AllCriteria.h"

#define LIBARCHIVE_STATIC
#include <archive.h>
#include <archive_entry.h>

namespace
{
    using Files = std::vector<std::pair<juce::String, juce::String>>;   // path, content

    /** Writes an archive with libarchive (format: "7zip", "zip", "gnutar"; filter: "gzip" or empty). */
    bool writeArchive (const juce::File& target, const char* format, const char* filter, const Files& files)
    {
        auto* a = archive_write_new();
        archive_write_set_format_by_name (a, format);

        if (filter != nullptr)
            archive_write_add_filter_by_name (a, filter);

        bool ok = archive_write_open_filename_w (a, target.getFullPathName().toWideCharPointer()) == ARCHIVE_OK;

        for (const auto& [path, content] : files)
        {
            auto* e = archive_entry_new();
            archive_entry_set_pathname_utf8 (e, path.toRawUTF8());
            archive_entry_set_size (e, (la_int64_t) content.getNumBytesAsUTF8());
            archive_entry_set_filetype (e, AE_IFREG);
            archive_entry_set_perm (e, 0644);
            ok = ok && archive_write_header (a, e) == ARCHIVE_OK
                    && archive_write_data (a, content.toRawUTF8(), content.getNumBytesAsUTF8()) == (la_ssize_t) content.getNumBytesAsUTF8();
            archive_entry_free (e);
        }

        ok = archive_write_close (a) == ARCHIVE_OK && ok;
        archive_write_free (a);
        return ok;
    }
}

class ArchiveTests final : public juce::UnitTest
{
public:
    ArchiveTests() : UnitTest ("Archives", "Redoondant") {}

    void runTest() override
    {
        const Files photos { { "Photos/a.txt", "alpha" }, { "Photos/sub/b.txt", juce::String::repeatedString ("beta", 50000) } };

        beginTest ("Suffixes and stems");
        expect (ArchiveReader::isArchive (juce::File ("C:/x/Backup.TAR.GZ")));
        expect (ArchiveReader::isArchive (juce::File ("C:/x/a.rar")));
        expect (! ArchiveReader::isArchive (juce::File ("C:/x/a.txt")));
        expectEquals (ArchiveReader::stemOf (juce::File ("C:/x/Docs.tar.xz")), juce::String ("Docs"));

        for (const auto& [format, filter, extension] : { std::tuple<const char*, const char*, const char*> { "7zip", nullptr, ".7z" },
                                                         { "zip", nullptr, ".zip" },
                                                         { "gnutar", "gzip", ".tar.gz" } })
        {
            beginTest (juce::String ("Read and compare ") + extension);

            TempFolder temp;
            const auto archive = temp.root.getChildFile (juce::String ("Photos") + extension);
            expect (writeArchive (archive, format, filter, photos));

            const auto entries = ArchiveReader::list (archive, {});
            expect (entries.has_value() && entries->size() == 2);
            expectEquals ((*entries)[1].path, juce::String ("Photos/sub/b.txt"));
            expectEquals ((*entries)[1].size, (juce::int64) 200000);

            juce::String content;
            ArchiveReader::forEachFile (archive, {}, [&] (const ArchiveReader::Entry& e, juce::InputStream& in)
            {
                if (e.path.endsWith ("b.txt"))
                    content = in.readEntireStreamAsString();

                return true;
            });
            expect (content == photos[1].second);

            for (const auto& [path, text] : photos)
                temp.write (path, text);

            auto result = run (*Criteria::createArchiveMirrors(), temp.root);
            expectEquals ((int) result.groups.size(), 1);

            temp.write ("Photos/a.txt", "ALPHA");   // Same size, different bytes.
            result = run (*Criteria::createArchiveMirrors(), temp.root);
            expect (result.groups.empty());
        }

        beginTest ("Unreadable archive");
        {
            TempFolder temp;
            expect (! ArchiveReader::list (temp.write ("fake.7z", "not an archive"), {}).has_value());
        }
    }
};

static ArchiveTests archiveTests;
