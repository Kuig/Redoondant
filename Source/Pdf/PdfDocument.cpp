#include "PdfDocument.h"

#if JUCE_WINDOWS && ! defined (NOMINMAX)
 #define NOMINMAX    // fpdfview.h includes windows.h
#endif

#include <fpdfview.h>
#include <fpdf_doc.h>
#include <mutex>

#if JUCE_MSVC
 #pragma comment (lib, "pdfium.dll.lib")
 #pragma comment (lib, "delayimp.lib")
#endif

namespace
{
    std::mutex pdfiumLock;

    /** Loads the library and initialises PDFium once. Call with pdfiumLock held. */
    bool ensureInitialised()
    {
        static const bool available = []
        {
            juce::DynamicLibrary probe;

            if (! probe.open ("pdfium.dll"))
                return false;

            FPDF_InitLibrary();
            return true;
        }();

        return available;
    }

    /** An open PDF, read through a juce stream (so that Unicode paths work). */
    class Document
    {
    public:
        explicit Document (const juce::File& file) : stream (file)
        {
            if (! stream.openedOk() || stream.getTotalLength() > (juce::int64) std::numeric_limits<unsigned long>::max())
                return;

            access.m_FileLen = (unsigned long) stream.getTotalLength();
            access.m_GetBlock = readBlock;
            access.m_Param = &stream;
            handle = FPDF_LoadCustomDocument (&access, nullptr);
        }

        ~Document()
        {
            if (handle != nullptr)
                FPDF_CloseDocument (handle);
        }

        FPDF_DOCUMENT get() const noexcept     { return handle; }

    private:
        juce::FileInputStream stream;
        FPDF_FILEACCESS access {};
        FPDF_DOCUMENT handle = nullptr;

        static int readBlock (void* param, unsigned long position, unsigned char* buffer, unsigned long size)
        {
            auto& in = *static_cast<juce::FileInputStream*> (param);
            return in.setPosition ((juce::int64) position) && in.read (buffer, (int) size) == (int) size ? 1 : 0;
        }

        JUCE_DECLARE_NON_COPYABLE (Document)
    };

    juce::String metaText (FPDF_DOCUMENT document, const char* tag)
    {
        const auto bytes = FPDF_GetMetaText (document, tag, nullptr, 0);    // UTF-16LE, including the terminator

        if (bytes <= 2)
            return {};

        juce::HeapBlock<juce::CharPointer_UTF16::CharType> text ((bytes + 1) / 2 + 1, true);
        FPDF_GetMetaText (document, tag, text, bytes);
        return juce::String (juce::CharPointer_UTF16 (text.get())).trim();
    }

    /** "D:20240131153000+01'00'" -> the time (local, to the minute), or nullopt. */
    std::optional<juce::Time> parsePdfDate (const juce::String& date)
    {
        const auto digits = date.fromFirstOccurrenceOf ("D:", false, false).retainCharacters ("0123456789");

        if (digits.length() < 8)
            return std::nullopt;

        const auto part = [&] (int start, int length, int fallback)
        {
            return digits.length() >= start + length ? digits.substring (start, start + length).getIntValue() : fallback;
        };

        return juce::Time (part (0, 4, 1970), juce::jlimit (1, 12, part (4, 2, 1)) - 1, juce::jlimit (1, 31, part (6, 2, 1)),
                           juce::jlimit (0, 23, part (8, 2, 0)), juce::jlimit (0, 59, part (10, 2, 0)), 0, 0, true);
    }

    /** "D:20240131153000+01'00'" -> "2024-01-31 15:30". */
    juce::String formatPdfDate (const juce::String& date)
    {
        const auto digits = date.fromFirstOccurrenceOf ("D:", false, false).retainCharacters ("0123456789").substring (0, 12);

        if (digits.length() < 8)
            return date;

        auto result = digits.substring (0, 4) + "-" + digits.substring (4, 6) + "-" + digits.substring (6, 8);

        if (digits.length() >= 12)
            result << " " << digits.substring (8, 10) << ":" << digits.substring (10, 12);

        return result;
    }
}

bool PdfDocument::isPdf (const juce::File& file)
{
    return file.hasFileExtension ("pdf");
}

bool PdfDocument::isAvailable()
{
    std::lock_guard<std::mutex> lock (pdfiumLock);
    return ensureInitialised();
}

Metadata PdfDocument::readMetadata (const juce::File& file)
{
    Metadata metadata;
    std::lock_guard<std::mutex> lock (pdfiumLock);

    if (! ensureInitialised())
        return metadata;

    Document document (file);

    if (document.get() == nullptr)
    {
        metadata.add ({}, "PDF", "Unreadable or password-protected");
        return metadata;
    }

    struct Field { const char* tag; juce::String key; const char* label; bool isDate; };

    const Field fields[] =
    {
        { "Title",        MetadataKeys::title,  "Title",    false },
        { "Author",       MetadataKeys::author, "Author",   false },
        { "Subject",      "System.Subject",     "Subject",  false },
        { "Keywords",     "System.Keywords",    "Keywords", false },
        { "Creator",      "Pdf.Creator",        "Creator",  false },
        { "Producer",     "Pdf.Producer",       "Producer", false },
        { "CreationDate", "Pdf.CreationDate",   "Created (PDF)",  true },
        { "ModDate",      "Pdf.ModDate",        "Modified (PDF)", true },
    };

    for (const auto& field : fields)
    {
        const auto value = metaText (document.get(), field.tag);

        if (const auto time = field.isDate ? parsePdfDate (value) : std::nullopt)
            metadata.addDate (field.key, field.label, formatPdfDate (value), *time);
        else
            metadata.add (field.key, field.label, field.isDate ? formatPdfDate (value) : value);
    }

    const int pages = FPDF_GetPageCount (document.get());
    metadata.add (MetadataKeys::pageCount, "Pages", juce::String (pages), pages);

    FS_SIZEF size {};

    if (pages > 0 && FPDF_GetPageSizeByIndexF (document.get(), 0, &size))
        metadata.add ("Pdf.PageSize", "Page size",
                      juce::String (juce::roundToInt (size.width / 72.0f * 25.4f)) + " x "
                        + juce::String (juce::roundToInt (size.height / 72.0f * 25.4f)) + " mm");

    int version = 0;

    if (FPDF_GetFileVersion (document.get(), &version))
        metadata.add ("Pdf.Version", "PDF version", juce::String (version / 10) + "." + juce::String (version % 10));

    return metadata;
}

juce::Image PdfDocument::renderPage (const juce::File& file, int pageIndex, int maxPixels)
{
    std::lock_guard<std::mutex> lock (pdfiumLock);

    if (! ensureInitialised())
        return {};

    Document document (file);

    if (document.get() == nullptr)
        return {};

    auto* page = FPDF_LoadPage (document.get(), pageIndex);

    if (page == nullptr)
        return {};

    const auto pageWidth = FPDF_GetPageWidthF (page), pageHeight = FPDF_GetPageHeightF (page);
    const auto scale = (float) maxPixels / juce::jmax (1.0f, pageWidth, pageHeight);
    const int width = juce::jmax (1, juce::roundToInt (pageWidth * scale));
    const int height = juce::jmax (1, juce::roundToInt (pageHeight * scale));

    juce::Image image;

    if (auto* bitmap = FPDFBitmap_Create (width, height, 0))
    {
        FPDFBitmap_FillRect (bitmap, 0, 0, width, height, 0xffffffff);
        FPDF_RenderPageBitmap (bitmap, page, 0, 0, width, height, 0, FPDF_ANNOT);

        // PDFium writes BGRx rows, the same byte order as juce's ARGB on little-endian machines.
        image = juce::Image (juce::Image::ARGB, width, height, false);
        const juce::Image::BitmapData pixels (image, juce::Image::BitmapData::writeOnly);
        const auto* source = static_cast<const juce::uint8*> (FPDFBitmap_GetBuffer (bitmap));
        const int stride = FPDFBitmap_GetStride (bitmap);

        for (int y = 0; y < height; ++y)
        {
            auto* row = pixels.getLinePointer (y);
            std::memcpy (row, source + y * stride, (size_t) width * 4);

            for (int x = 0; x < width; ++x)
                row[x * 4 + 3] = 0xff;
        }

        FPDFBitmap_Destroy (bitmap);
    }

    FPDF_ClosePage (page);
    return image;
}
