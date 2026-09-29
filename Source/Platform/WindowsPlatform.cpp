/*  Windows-specific services: COM initialisation, shell thumbnails and Property System metadata.
    Other platforms get no-op fallbacks.
*/

#include <JuceHeader.h>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <shobjidl.h>
 #include <propsys.h>
 #include <propkey.h>
 #include <propvarutil.h>
 #include <wrl/client.h>

 #pragma comment (lib, "propsys.lib")
 #pragma comment (lib, "ole32.lib")
 #pragma comment (lib, "shell32.lib")
#endif

#include "ComInit.h"
#include "ShellThumbnail.h"
#include "../Metadata/MetadataReader.h"

#if JUCE_WINDOWS

using Microsoft::WRL::ComPtr;

namespace
{
    juce::String takeString (PWSTR text)
    {
        juce::String result (text != nullptr ? juce::String (text) : juce::String());
        CoTaskMemFree (text);
        return result;
    }

    /** Properties already shown by the app itself, or meaningless to users. */
    bool isRedundant (const juce::String& key)
    {
        static const juce::StringArray prefixes
        {
            "System.Item", "System.Parsing", "System.Size", "System.Date", "System.FileAttributes", "System.FileName",
            "System.FileExtension", "System.FileOwner", "System.Kind", "System.PerceivedType", "System.ContentType",
            "System.SFGAOFlags", "System.Link", "System.Thumbnail", "System.Offline", "System.ComputerName",
            "System.NetworkLocation", "System.Security", "System.IsShared", "System.SharedWith", "System.StorageProvider",
            "System.ZoneIdentifier", "System.AppUserModel", "System.IsFolder", "System.Shell", "System.Sharing",
        };

        for (const auto& prefix : prefixes)
            if (key.startsWith (prefix))
                return true;

        return false;
    }

    /** English label from a canonical name, independent of the Windows display language:
        "System.Music.AlbumTitle" -> "Album Title".
    */
    juce::String labelFor (const juce::String& canonical)
    {
        const auto name = canonical.fromLastOccurrenceOf (".", false, false);
        juce::String label;

        for (int i = 0; i < name.length(); ++i)
        {
            const auto c = name[i];
            const bool wordStart = i > 0 && juce::CharacterFunctions::isUpperCase (c)
                                   && (juce::CharacterFunctions::isLowerCase (name[i - 1])
                                       || (i + 1 < name.length() && juce::CharacterFunctions::isLowerCase (name[i + 1])));
            if (wordStart)
                label << ' ';

            label << c;
        }

        return label;
    }
}

//==============================================================================
ScopedComInit::ScopedComInit()
{
    initialised = SUCCEEDED (CoInitializeEx (nullptr, COINIT_MULTITHREADED));
}

ScopedComInit::~ScopedComInit()
{
    if (initialised)
        CoUninitialize();
}

//==============================================================================
juce::Image ShellThumbnail::get (const juce::File& file, int maxPixels)
{
    ComPtr<IShellItemImageFactory> factory;

    if (FAILED (SHCreateItemFromParsingName (file.getFullPathName().toWideCharPointer(), nullptr, IID_PPV_ARGS (&factory))))
        return {};

    HBITMAP bitmap = nullptr;

    if (FAILED (factory->GetImage ({ maxPixels, maxPixels }, SIIGBF_THUMBNAILONLY | SIIGBF_BIGGERSIZEOK, &bitmap)))
        return {};

    BITMAP info {};
    GetObject (bitmap, sizeof (info), &info);

    BITMAPINFO header {};
    header.bmiHeader.biSize = sizeof (BITMAPINFOHEADER);
    header.bmiHeader.biWidth = info.bmWidth;
    header.bmiHeader.biHeight = -info.bmHeight;     // Top-down rows.
    header.bmiHeader.biPlanes = 1;
    header.bmiHeader.biBitCount = 32;
    header.bmiHeader.biCompression = BI_RGB;

    // 32-bit BGRA rows, the same byte order as juce's ARGB on little-endian machines.
    const auto width = (int) info.bmWidth, height = (int) info.bmHeight;
    std::vector<juce::uint8> bgra ((size_t) width * (size_t) height * 4);

    auto* dc = GetDC (nullptr);
    const bool copied = GetDIBits (dc, bitmap, 0, (UINT) height, bgra.data(), &header, DIB_RGB_COLORS) == height;
    ReleaseDC (nullptr, dc);
    DeleteObject (bitmap);

    if (! copied)
        return {};

    // Opaque bitmaps come with alpha = 0 everywhere.
    bool hasAlpha = false;

    for (size_t i = 3; i < bgra.size() && ! hasAlpha; i += 4)
        hasAlpha = bgra[i] != 0;

    juce::Image image (juce::Image::ARGB, width, height, false);
    const juce::Image::BitmapData pixels (image, juce::Image::BitmapData::writeOnly);

    for (int y = 0; y < height; ++y)
    {
        auto* row = pixels.getLinePointer (y);
        std::memcpy (row, bgra.data() + (size_t) y * (size_t) width * 4, (size_t) width * 4);

        if (! hasAlpha)
            for (int x = 0; x < width; ++x)
                row[x * 4 + 3] = 0xff;
    }

    return image;
}

//==============================================================================
Metadata MetadataReader::readSystemProperties (const juce::File& file)
{
    Metadata metadata;
    ComPtr<IPropertyStore> store;

    if (FAILED (SHGetPropertyStoreFromParsingName (file.getFullPathName().toWideCharPointer(), nullptr,
                                                   GPS_BESTEFFORT, IID_PPV_ARGS (&store))))
        return metadata;

    DWORD count = 0;
    store->GetCount (&count);

    for (DWORD i = 0; i < count; ++i)
    {
        PROPERTYKEY key {};
        ComPtr<IPropertyDescription> description;

        if (FAILED (store->GetAt (i, &key)) || FAILED (PSGetPropertyDescription (key, IID_PPV_ARGS (&description))))
            continue;

        PWSTR name = nullptr;
        description->GetCanonicalName (&name);
        const auto canonical = takeString (name);

        PROPDESC_TYPE_FLAGS flags {};
        description->GetTypeFlags (PDTF_ISVIEWABLE, &flags);

        if (canonical.isEmpty() || isRedundant (canonical) || (flags & PDTF_ISVIEWABLE) == 0)
            continue;

        PROPVARIANT value;
        PropVariantInit (&value);

        if (SUCCEEDED (store->GetValue (key, &value)) && value.vt != VT_EMPTY)
        {
            PWSTR display = nullptr;
            PSFormatForDisplayAlloc (key, value, PDFF_DEFAULT, &display);

            juce::var raw;

            if (canonical == MetadataKeys::duration && value.vt == VT_UI8)
                raw = (double) value.uhVal.QuadPart / 1.0e7;   // 100 ns units -> seconds

            metadata.add (canonical, labelFor (canonical), takeString (display), raw);
        }

        PropVariantClear (&value);
    }

    return metadata;
}

#else

ScopedComInit::ScopedComInit()  {}
ScopedComInit::~ScopedComInit() {}

juce::Image ShellThumbnail::get (const juce::File&, int)                  { return {}; }
Metadata MetadataReader::readSystemProperties (const juce::File&)         { return {}; }

#endif
