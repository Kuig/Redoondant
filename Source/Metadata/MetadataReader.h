#pragma once

#include "Metadata.h"

/** Reads the metadata of a file from every available source:
    the Windows Property System (media tags, EXIF, versions...), PE headers for executables
    and PDFium for PDF documents. May be slow: call it from a background thread
    that has initialised COM (see ComInit).
*/
namespace MetadataReader
{
    Metadata read (const juce::File& file);

    /** Only the dated items of read() (what ContentDate needs): the other properties are neither formatted nor kept. */
    Metadata readDates (const juce::File& file);

    /** Individual sources, merged by read(). */
    Metadata readSystemProperties (const juce::File& file, bool datesOnly = false);
    Metadata readExecutableHeaders (const juce::File& file);
}
