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
