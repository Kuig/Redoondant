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

    /** Individual sources, merged by read(). */
    Metadata readSystemProperties (const juce::File& file);
    Metadata readExecutableHeaders (const juce::File& file);
}
