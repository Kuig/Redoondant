#pragma once

#include "../Metadata/Metadata.h"

/** PDF support through PDFium (pdfium.dll, delay-loaded: if it's missing, PDFs are simply
    treated as generic files). All calls are serialised, as PDFium isn't thread-safe.
*/
namespace PdfDocument
{
    bool isPdf (const juce::File& file);

    /** True if pdfium.dll could be loaded. */
    bool isAvailable();

    /** Title, author, dates, producer, PDF version, page count and size (canonical keys where they exist). */
    Metadata readMetadata (const juce::File& file);

    /** Renders a page on a white background, scaled so that its longest side is maxPixels.
        Returns an invalid image on failure.
    */
    juce::Image renderPage (const juce::File& file, int pageIndex, int maxPixels);
}
