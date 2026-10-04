#include "MetadataReader.h"
#include "../Pdf/PdfDocument.h"

Metadata MetadataReader::read (const juce::File& file)
{
    Metadata metadata;

    if (PdfDocument::isPdf (file))
        metadata.append (PdfDocument::readMetadata (file));

    metadata.append (readExecutableHeaders (file));
    metadata.append (readSystemProperties (file));
    return metadata;
}

Metadata MetadataReader::readDates (const juce::File& file)
{
    Metadata metadata;

    if (PdfDocument::isPdf (file))
        metadata.append (PdfDocument::readMetadata (file));     // A handful of fields, read with the document's trailer.

    metadata.append (readExecutableHeaders (file));             // Only reads executables' headers (their link date).
    metadata.append (readSystemProperties (file, true));
    return metadata;
}
