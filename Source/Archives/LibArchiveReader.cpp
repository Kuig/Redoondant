/*  ArchiveReader::list / forEachFile, implemented with libarchive (static build from vcpkg, see README). */

#include "ArchiveReader.h"

#define LIBARCHIVE_STATIC
#include <archive.h>
#include <archive_entry.h>

#if JUCE_MSVC
 #if JUCE_DEBUG
  #pragma comment (lib, "archive.lib")
  #pragma comment (lib, "zsd.lib")
  #pragma comment (lib, "bz2d.lib")
  #pragma comment (lib, "lz4d.lib")
 #else
  #pragma comment (lib, "archive.lib")
  #pragma comment (lib, "zs.lib")
  #pragma comment (lib, "bz2.lib")
  #pragma comment (lib, "lz4.lib")
 #endif
 #pragma comment (lib, "lzma.lib")
 #pragma comment (lib, "zstd.lib")
 #pragma comment (lib, "advapi32.lib")
 #pragma comment (lib, "bcrypt.lib")
 #pragma comment (lib, "xmllite.lib")
#endif

namespace
{
    constexpr size_t blockSize = 64 * 1024;

    /** An archive opened for reading, freed automatically. */
    class OpenArchive
    {
    public:
        explicit OpenArchive (const juce::File& file) : handle (archive_read_new())
        {
            archive_read_support_filter_all (handle);
            archive_read_support_format_all (handle);
            opened = archive_read_open_filename_w (handle, file.getFullPathName().toWideCharPointer(), blockSize) == ARCHIVE_OK;
        }

        ~OpenArchive()      { archive_read_free (handle); }

        /** Calls onFile for each regular file; stops when it returns false.
            Returns true if the archive was read to the end (or stopped on purpose).
        */
        template <typename Callback>
        bool forEachFileEntry (const ScanContext& context, Callback&& onFile)
        {
            if (! opened)
                return false;

            archive_entry* entry = nullptr;

            for (;;)
            {
                if (context.isCancelled())
                    return false;

                const int result = archive_read_next_header (handle, &entry);

                if (result == ARCHIVE_EOF)
                    return true;

                if (result != ARCHIVE_OK && result != ARCHIVE_WARN)
                    return false;

                if (archive_entry_is_encrypted (entry))
                    return false;

                if (archive_entry_filetype (entry) != AE_IFREG)
                    continue;   // Data is skipped by the next archive_read_next_header().

                const auto* utf8 = archive_entry_pathname_utf8 (entry);
                const auto* path = utf8 != nullptr ? utf8 : archive_entry_pathname (entry);

                const ArchiveReader::Entry info { juce::String::fromUTF8 (path != nullptr ? path : "").replaceCharacter ('\\', '/'),
                                                  archive_entry_size_is_set (entry) ? (juce::int64) archive_entry_size (entry) : 0 };

                if (! onFile (info))
                    return true;
            }
        }

        archive* get() const noexcept   { return handle; }

    private:
        archive* handle;
        bool opened = false;

        JUCE_DECLARE_NON_COPYABLE (OpenArchive)
    };

    /** The data of the current archive entry as a forward-only juce::InputStream. */
    class EntryStream final : public juce::InputStream
    {
    public:
        EntryStream (archive* a, juce::int64 entrySize) : handle (a), size (entrySize) {}

        juce::int64 getTotalLength() override       { return size; }
        bool isExhausted() override                 { return finished; }
        juce::int64 getPosition() override          { return position; }
        bool setPosition (juce::int64 p) override   { return p == position; }

        /** Fills the buffer completely unless the entry ends (libarchive may return short blocks). */
        int read (void* destination, int bytes) override
        {
            int total = 0;

            while (total < bytes && ! finished)
            {
                const auto got = archive_read_data (handle, static_cast<char*> (destination) + total, (size_t) (bytes - total));

                if (got <= 0)
                    finished = true;
                else
                    total += (int) got;
            }

            position += total;
            return total;
        }

    private:
        archive* handle;
        juce::int64 size, position = 0;
        bool finished = false;
    };
}

std::optional<std::vector<ArchiveReader::Entry>> ArchiveReader::list (const juce::File& file, const ScanContext& context)
{
    OpenArchive archive (file);
    std::vector<Entry> entries;

    if (! archive.forEachFileEntry (context, [&] (const Entry& e) { entries.push_back (e); return true; }))
        return std::nullopt;

    return entries;
}

bool ArchiveReader::forEachFile (const juce::File& file, const ScanContext& context,
                                 const std::function<bool (const Entry&, juce::InputStream&)>& visitor)
{
    OpenArchive archive (file);

    return archive.forEachFileEntry (context, [&] (const Entry& e)
    {
        EntryStream stream (archive.get(), e.size);
        return visitor (e, stream);
    });
}
