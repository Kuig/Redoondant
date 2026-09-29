/*  Windows-specific services: COM initialisation, shell thumbnails, Explorer context menus
    and Property System metadata.
    Other platforms get no-op fallbacks.
*/

#include <JuceHeader.h>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <shobjidl.h>
 #include <shlobj.h>
 #include <propsys.h>
 #include <propkey.h>
 #include <propvarutil.h>
 #include <wrl/client.h>

 #pragma comment (lib, "propsys.lib")
 #pragma comment (lib, "ole32.lib")
 #pragma comment (lib, "shell32.lib")
#endif

#include "ComInit.h"
#include "ShellContextMenu.h"
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
namespace
{
    /** Hidden window owning the context menu: forwards the messages that Explorer's
        owner-drawn submenus ("Send to", "Open with"...) need to IContextMenu3.
    */
    class MenuHost
    {
    public:
        MenuHost (HWND owner, IContextMenu3* menu)
        {
            static const wchar_t* className = [] 
            {
                WNDCLASSW wc {};
                wc.lpfnWndProc = windowProc;
                wc.hInstance = GetModuleHandleW (nullptr);
                wc.lpszClassName = L"RedoondantContextMenuHost";
                RegisterClassW (&wc);
                return wc.lpszClassName;
            }();

            handle = CreateWindowExW (0, className, L"", WS_POPUP, 0, 0, 0, 0, owner, nullptr, GetModuleHandleW (nullptr), nullptr);
            SetWindowLongPtrW (handle, GWLP_USERDATA, reinterpret_cast<LONG_PTR> (menu));
        }

        ~MenuHost()     { DestroyWindow (handle); }

        HWND get() const noexcept   { return handle; }

    private:
        HWND handle = nullptr;

        static LRESULT CALLBACK windowProc (HWND window, UINT message, WPARAM wParam, LPARAM lParam)
        {
            if (auto* menu = reinterpret_cast<IContextMenu3*> (GetWindowLongPtrW (window, GWLP_USERDATA)))
            {
                switch (message)
                {
                    case WM_INITMENUPOPUP: case WM_DRAWITEM: case WM_MEASUREITEM: case WM_MENUCHAR:
                    {
                        LRESULT result = 0;

                        if (SUCCEEDED (menu->HandleMenuMsg2 (message, wParam, lParam, &result)))
                            return result;

                        break;
                    }

                    default: break;
                }
            }

            return DefWindowProcW (window, message, wParam, lParam);
        }

        JUCE_DECLARE_NON_COPYABLE (MenuHost)
    };
}

bool ShellContextMenu::show (const juce::Array<juce::File>& files, juce::Point<int> screenPosition, juce::Component& owner)
{
    auto* peer = owner.getPeer();

    if (files.isEmpty() || peer == nullptr)
        return false;

    const auto ownerWindow = static_cast<HWND> (peer->getNativeHandle());

    // Absolute item ids; all files share the same folder, so their last ids are children of one IShellFolder.
    std::vector<PIDLIST_ABSOLUTE> items;

    for (const auto& file : files)
    {
        PIDLIST_ABSOLUTE item = nullptr;

        if (SUCCEEDED (SHParseDisplayName (file.getFullPathName().toWideCharPointer(), nullptr, &item, 0, nullptr)))
            items.push_back (item);
    }

    const auto freeItems = [&] { for (auto item : items) CoTaskMemFree (item); };

    ComPtr<IShellFolder> folder;
    ComPtr<IContextMenu> menu;
    std::vector<PCUITEMID_CHILD> children;

    for (auto item : items)
        children.push_back (ILFindLastID (item));

    if (items.empty()
        || FAILED (SHBindToParent (items.front(), IID_PPV_ARGS (&folder), nullptr))
        || FAILED (folder->GetUIObjectOf (ownerWindow, (UINT) children.size(), children.data(), IID_IContextMenu, nullptr,
                                          reinterpret_cast<void**> (menu.GetAddressOf()))))
    {
        freeItems();
        return false;
    }

    constexpr UINT firstCommand = 1;
    auto* popup = CreatePopupMenu();
    menu->QueryContextMenu (popup, 0, firstCommand, 0x7fff, CMF_NORMAL);

    ComPtr<IContextMenu3> menu3;
    menu.As (&menu3);
    const MenuHost host (ownerWindow, menu3.Get());

    const auto physical = juce::Desktop::getInstance().getDisplays().logicalToPhysical (screenPosition);
    const auto command = (UINT) TrackPopupMenuEx (popup, TPM_RETURNCMD | TPM_RIGHTBUTTON, physical.x, physical.y, host.get(), nullptr);

    if (command >= firstCommand)
    {
        CMINVOKECOMMANDINFOEX info {};
        info.cbSize = sizeof (info);
        info.fMask = CMIC_MASK_UNICODE | CMIC_MASK_PTINVOKE;
        info.hwnd = ownerWindow;
        info.lpVerb = MAKEINTRESOURCEA (command - firstCommand);
        info.lpVerbW = MAKEINTRESOURCEW (command - firstCommand);
        info.nShow = SW_SHOWNORMAL;
        info.ptInvoke = { physical.x, physical.y };
        menu->InvokeCommand (reinterpret_cast<CMINVOKECOMMANDINFO*> (&info));
    }

    DestroyMenu (popup);
    freeItems();
    return command >= firstCommand;
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
bool ShellContextMenu::show (const juce::Array<juce::File>&, juce::Point<int>, juce::Component&)   { return false; }
Metadata MetadataReader::readSystemProperties (const juce::File&)         { return {}; }

#endif
