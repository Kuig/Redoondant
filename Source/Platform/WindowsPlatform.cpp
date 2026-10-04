/*  Windows-specific services: COM initialisation, shell thumbnails, Explorer context menus,
    the Recycle Bin and Property System metadata.
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
#include "FileSystem.h"
#include "RecycleBin.h"
#include "ShellContextMenu.h"
#include "ShellIcon.h"
#include "ShellThumbnail.h"
#include "../Core/Paths.h"
#include <thread>
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
namespace
{
    /** The image the shell produces for a file with the given SIIGBF flags, or an invalid image. */
    juce::Image shellImage (const juce::File& file, int maxPixels, SIIGBF flags)
    {
        ComPtr<IShellItemImageFactory> factory;

        if (FAILED (SHCreateItemFromParsingName (file.getFullPathName().toWideCharPointer(), nullptr, IID_PPV_ARGS (&factory))))
            return {};

        HBITMAP bitmap = nullptr;

        if (FAILED (factory->GetImage ({ maxPixels, maxPixels }, flags | SIIGBF_BIGGERSIZEOK, &bitmap)))
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
}

juce::Image ShellThumbnail::get (const juce::File& file, int maxPixels)    { return shellImage (file, maxPixels, SIIGBF_THUMBNAILONLY); }
juce::Image ShellIcon::get (const juce::File& file, int pixels)            { return shellImage (file, pixels, SIIGBF_ICONONLY); }

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
namespace
{
    /** True if the Recycle Bin of the volume is set to delete for good ("Don't move files to the Recycle Bin"). */
    bool recycleBinTurnedOff (const juce::String& root)
    {
        wchar_t volume[64] = {};

        if (! GetVolumeNameForVolumeMountPointW (root.toWideCharPointer(), volume, 64))
            return false;

        const juce::String name (volume);        // e.g. Volume{GUID} in a volume-name path
        const auto open = name.indexOfChar ('{'), close = name.indexOfChar ('}');

        if (open < 0 || close < open)
            return false;

        const juce::String bucket ("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\BitBucket\\");
        auto setting = juce::WindowsRegistry::getValue (bucket + "Volume\\" + name.substring (open, close + 1) + "\\NukeOnDelete");

        if (setting.isEmpty())
            setting = juce::WindowsRegistry::getValue (bucket + "NukeOnDelete");

        return setting.getIntValue() != 0;
    }
}

namespace
{
    /** The root of the volume the file is really on ("C:\\", or the folder a drive is mounted in), or empty. */
    juce::String volumeOf (const juce::File& file)
    {
        wchar_t volume[MAX_PATH + 1] = {};

        if (! GetVolumePathNameW (file.getFullPathName().toWideCharPointer(), volume, MAX_PATH + 1))
            return {};

        return juce::String (volume);
    }

    bool volumeRecycles (const juce::String& volume)
    {
        // UNC shares and odd paths: no Recycle Bin.
        return volume.length() >= 3 && volume[1] == ':'
            && GetDriveTypeW (volume.toWideCharPointer()) == DRIVE_FIXED && ! recycleBinTurnedOff (volume);
    }
}

bool RecycleBin::isSupported (const juce::File& file)
{
    return volumeRecycles (volumeOf (file));
}

bool RecycleBin::Support::isSupported (const juce::File& file)
{
    const auto folder = Paths::key (file.getParentDirectory());

    if (const auto known = byFolder.find (folder); known != byFolder.end())
        return known->second;

    const auto volume = volumeOf (file).toLowerCase();
    auto found = byVolume.find (volume);

    if (found == byVolume.end())
        found = byVolume.emplace (volume, volumeRecycles (volume)).first;

    // Keyed by folder too: a drive mounted in a folder makes the volume depend on the path, but never within one folder.
    return byFolder[folder] = found->second;
}

namespace
{
    using Microsoft::WRL::ComPtr;

    /** Follows a shell file operation item by item: lets it stop (shouldStop) and records what became of each file. */
    class DeletionSink final : public IFileOperationProgressSink
    {
    public:
        DeletionSink (const std::vector<juce::File>& files, std::vector<RecycleBin::Outcome>& outcomes,
                      std::vector<bool>& reported, const std::function<bool()>& stop)
            : results (outcomes), seen (reported), shouldStop (stop)
        {
            for (size_t i = 0; i < files.size(); ++i)
                indexOf[Paths::key (files[i])] = i;
        }

        // IUnknown (the sink lives on the stack, longer than the operation)
        IFACEMETHODIMP QueryInterface (REFIID riid, void** object) override
        {
            if (riid == IID_IUnknown || riid == __uuidof (IFileOperationProgressSink))
            {
                *object = static_cast<IFileOperationProgressSink*> (this);
                return S_OK;
            }

            *object = nullptr;
            return E_NOINTERFACE;
        }

        IFACEMETHODIMP_(ULONG) AddRef() override     { return 2; }
        IFACEMETHODIMP_(ULONG) Release() override    { return 1; }

        IFACEMETHODIMP PreDeleteItem (DWORD, IShellItem*) override
        {
            return shouldStop != nullptr && shouldStop() ? HRESULT_FROM_WIN32 (ERROR_CANCELLED) : S_OK;
        }

        IFACEMETHODIMP PostDeleteItem (DWORD, IShellItem* item, HRESULT result, IShellItem* recycled) override
        {
            PWSTR path = nullptr;

            if (item != nullptr && SUCCEEDED (item->GetDisplayName (SIGDN_FILESYSPATH, &path)))
            {
                const auto found = indexOf.find (Paths::key (juce::File (juce::String (path))));
                CoTaskMemFree (path);

                if (found != indexOf.end())     // Not the content of a listed folder.
                {
                    seen[found->second] = true;
                    results[found->second] = FAILED (result) ? RecycleBin::Outcome::failed
                                                             : (recycled != nullptr ? RecycleBin::Outcome::recycled : RecycleBin::Outcome::deletedForGood);
                }
            }

            return S_OK;
        }

        IFACEMETHODIMP StartOperations() override                                                       { return S_OK; }
        IFACEMETHODIMP FinishOperations (HRESULT) override                                              { return S_OK; }
        IFACEMETHODIMP PreRenameItem (DWORD, IShellItem*, LPCWSTR) override                             { return S_OK; }
        IFACEMETHODIMP PostRenameItem (DWORD, IShellItem*, LPCWSTR, HRESULT, IShellItem*) override       { return S_OK; }
        IFACEMETHODIMP PreMoveItem (DWORD, IShellItem*, IShellItem*, LPCWSTR) override                  { return S_OK; }
        IFACEMETHODIMP PostMoveItem (DWORD, IShellItem*, IShellItem*, LPCWSTR, HRESULT, IShellItem*) override  { return S_OK; }
        IFACEMETHODIMP PreCopyItem (DWORD, IShellItem*, IShellItem*, LPCWSTR) override                  { return S_OK; }
        IFACEMETHODIMP PostCopyItem (DWORD, IShellItem*, IShellItem*, LPCWSTR, HRESULT, IShellItem*) override  { return S_OK; }
        IFACEMETHODIMP PreNewItem (DWORD, IShellItem*, LPCWSTR) override                                { return S_OK; }
        IFACEMETHODIMP PostNewItem (DWORD, IShellItem*, LPCWSTR, LPCWSTR, DWORD, HRESULT, IShellItem*) override  { return S_OK; }
        IFACEMETHODIMP UpdateProgress (UINT, UINT) override                                             { return S_OK; }
        IFACEMETHODIMP ResetTimer() override                                                            { return S_OK; }
        IFACEMETHODIMP PauseTimer() override                                                            { return S_OK; }
        IFACEMETHODIMP ResumeTimer() override                                                           { return S_OK; }

    private:
        std::map<juce::String, size_t> indexOf;
        std::vector<RecycleBin::Outcome>& results;
        std::vector<bool>& seen;
        const std::function<bool()>& shouldStop;
    };

    /** A window of this program to own the shell's questions, so they show above the progress window (null if none is active). */
    HWND ownWindow()
    {
        const auto window = GetForegroundWindow();
        DWORD process = 0;
        GetWindowThreadProcessId (window, &process);
        return process == GetCurrentProcessId() ? window : nullptr;
    }
}

namespace
{
    void recycleInThisThread (const std::vector<juce::File>& files, const std::function<bool()>& shouldStop,
                              std::vector<RecycleBin::Outcome>& outcomes, std::vector<bool>& reported)
    {
        ComPtr<IFileOperation> operation;

        if (FAILED (CoCreateInstance (CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_PPV_ARGS (&operation))))
            return;

        // FOF_ALLOWUNDO recycles. FOF_WANTNUKEWARNING makes Windows ask (instead of silently deleting for good) about what
        // it can't recycle; the sink then tells "deleted for good" from "recycled".
        operation->SetOperationFlags (FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT | FOF_WANTNUKEWARNING);

        if (const auto owner = ownWindow())
            operation->SetOwnerWindow (owner);

        bool anyQueued = false;

        for (const auto& file : files)
        {
            ComPtr<IShellItem> item;

            if (SUCCEEDED (SHCreateItemFromParsingName (file.getFullPathName().toWideCharPointer(), nullptr, IID_PPV_ARGS (&item))))
                anyQueued = SUCCEEDED (operation->DeleteItem (item.Get(), nullptr)) || anyQueued;
        }

        if (! anyQueued)
            return;

        DeletionSink sink (files, outcomes, reported, shouldStop);
        DWORD cookie = 0;
        const bool advised = SUCCEEDED (operation->Advise (&sink, &cookie));

        operation->PerformOperations();

        if (advised)
            operation->Unadvise (cookie);
    }
}

std::vector<RecycleBin::Outcome> RecycleBin::move (const std::vector<juce::File>& files, const std::function<bool()>& shouldStop)
{
    std::vector<Outcome> outcomes (files.size(), Outcome::failed);
    std::vector<bool> reported (files.size(), false);

    if (files.empty())
        return outcomes;

    // IFileOperation only works in a single-threaded apartment, and the caller's thread may already be multi-threaded.
    std::thread sta ([&]
    {
        if (SUCCEEDED (CoInitializeEx (nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)))
        {
            recycleInThisThread (files, shouldStop, outcomes, reported);
            CoUninitialize();
        }
    });

    sta.join();

    // Whatever the sink didn't hear about: gone means recycled (the shell reports every item it deletes for good).
    for (size_t i = 0; i < files.size(); ++i)
        if (! reported[i])
            outcomes[i] = files[i].exists() ? Outcome::failed : Outcome::recycled;
        else if (outcomes[i] != Outcome::failed && files[i].exists())
            outcomes[i] = Outcome::failed;

    return outcomes;
}

Metadata MetadataReader::readSystemProperties (const juce::File& file, bool datesOnly)
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

        if (FAILED (store->GetAt (i, &key)))
            continue;

        PROPVARIANT value;
        PropVariantInit (&value);

        struct Clear { PROPVARIANT& v; ~Clear() { PropVariantClear (&v); } } clearValue { value };

        // The value first: with datesOnly most properties are dropped before their description is looked up.
        if (FAILED (store->GetValue (key, &value)) || value.vt == VT_EMPTY || (datesOnly && value.vt != VT_FILETIME))
            continue;

        ComPtr<IPropertyDescription> description;

        if (FAILED (PSGetPropertyDescription (key, IID_PPV_ARGS (&description))))
            continue;

        PWSTR name = nullptr;
        description->GetCanonicalName (&name);
        const auto canonical = takeString (name);

        PROPDESC_TYPE_FLAGS flags {};
        description->GetTypeFlags (PDTF_ISVIEWABLE, &flags);

        if (canonical.isEmpty() || isRedundant (canonical) || (flags & PDTF_ISVIEWABLE) == 0)
            continue;

        {
            PWSTR display = nullptr;

            if (! datesOnly)
                PSFormatForDisplayAlloc (key, value, PDFF_DEFAULT, &display);

            juce::var raw;
            bool isDate = false;

            if (canonical == MetadataKeys::duration && value.vt == VT_UI8)
            {
                raw = (double) value.uhVal.QuadPart / 1.0e7;   // 100 ns units -> seconds
            }
            else if (value.vt == VT_FILETIME)
            {
                // FILETIME counts 100 ns units since 1601; juce::Time counts milliseconds since 1970.
                ULARGE_INTEGER ticks;
                ticks.LowPart = value.filetime.dwLowDateTime;
                ticks.HighPart = value.filetime.dwHighDateTime;
                raw = (juce::int64) (ticks.QuadPart / 10000ull) - 11644473600000ll;
                isDate = true;
            }

            metadata.add (MetadataItem { canonical, labelFor (canonical), takeString (display), raw, isDate });
        }
    }

    return metadata;
}

//==============================================================================
FileSystem::Rename FileSystem::rename (const juce::File& source, const juce::File& target)
{
    // No MOVEFILE_COPY_ALLOWED: a move that can't be a rename is reported, and the caller copies with its own checks.
    if (MoveFileExW (source.getFullPathName().toWideCharPointer(), target.getFullPathName().toWideCharPointer(), 0))
        return Rename::done;

    return GetLastError() == ERROR_NOT_SAME_DEVICE ? Rename::otherVolume : Rename::failed;
}

namespace
{
    DWORD CALLBACK copyProgress (LARGE_INTEGER, LARGE_INTEGER, LARGE_INTEGER, LARGE_INTEGER, DWORD, DWORD, HANDLE, HANDLE, LPVOID data)
    {
        const auto& shouldStop = *static_cast<const FileSystem::ShouldStop*> (data);
        return shouldStop != nullptr && shouldStop() ? PROGRESS_CANCEL : PROGRESS_CONTINUE;     // Cancel also deletes the partial copy.
    }
}

bool FileSystem::copyFile (const juce::File& source, const juce::File& target, const ShouldStop& shouldStop)
{
    BOOL cancel = FALSE;
    return CopyFileExW (source.getFullPathName().toWideCharPointer(), target.getFullPathName().toWideCharPointer(),
                        copyProgress, const_cast<ShouldStop*> (&shouldStop), &cancel, COPY_FILE_FAIL_IF_EXISTS) != 0;
}

juce::File FileSystem::finalPath (const juce::File& file)
{
    const auto handle = CreateFileW (file.getFullPathName().toWideCharPointer(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                     nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);     // Backup semantics: folders too.

    if (handle == INVALID_HANDLE_VALUE)
        return file;

    std::wstring buffer (32768, L'\0');
    const auto length = GetFinalPathNameByHandleW (handle, buffer.data(), (DWORD) buffer.size(), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    CloseHandle (handle);

    if (length == 0 || length >= buffer.size())
        return file;

    juce::String path (buffer.c_str(), (size_t) length);

    if (path.startsWith ("\\\\?\\UNC\\"))
        path = "\\\\" + path.substring (8);
    else if (path.startsWith ("\\\\?\\"))
        path = path.substring (4);

    return juce::File (path);
}

#else

juce::File FileSystem::finalPath (const juce::File& file)
{
    return file.isSymbolicLink() ? file.getLinkedTarget() : file;
}

FileSystem::Rename FileSystem::rename (const juce::File& source, const juce::File& target)
{
    return source.moveFileTo (target) ? Rename::done : Rename::otherVolume;
}

bool FileSystem::copyFile (const juce::File& source, const juce::File& target, const ShouldStop&)
{
    return ! target.exists() && source.copyFileTo (target);
}

ScopedComInit::ScopedComInit()  {}
ScopedComInit::~ScopedComInit() {}

juce::Image ShellThumbnail::get (const juce::File&, int)                  { return {}; }
juce::Image ShellIcon::get (const juce::File&, int)                       { return {}; }
bool ShellContextMenu::show (const juce::Array<juce::File>&, juce::Point<int>, juce::Component&)   { return false; }
bool RecycleBin::isSupported (const juce::File&)                          { return true; }
bool RecycleBin::Support::isSupported (const juce::File&)                 { return true; }

std::vector<RecycleBin::Outcome> RecycleBin::move (const std::vector<juce::File>& files, const std::function<bool()>& shouldStop)
{
    std::vector<Outcome> outcomes;

    for (const auto& file : files)
        outcomes.push_back ((shouldStop == nullptr || ! shouldStop()) && file.moveToTrash() ? Outcome::recycled : Outcome::failed);

    return outcomes;
}
Metadata MetadataReader::readSystemProperties (const juce::File&, bool)   { return {}; }

#endif

//==============================================================================
FileSystem::Deletion FileSystem::deleteTree (const juce::File& file, const ShouldStop& shouldStop)
{
    Deletion result;

    std::function<void (const juce::File&)> remove = [&] (const juce::File& item)
    {
        if (shouldStop != nullptr && shouldStop())
        {
            result.stopped = true;
            return;
        }

        if (item.isDirectory() && ! item.isSymbolicLink())
        {
            for (const auto& child : item.findChildFiles (juce::File::findFilesAndDirectories, false, "*",
                                                          juce::File::FollowSymlinks::no))
            {
                remove (child);

                if (result.stopped)
                    return;
            }
        }

        // A read-only attribute is no reason to keep an item the user deletes (Explorer asks once and goes on).
        const bool deleted = item.deleteFile() || (item.setReadOnly (false) && item.deleteFile());
        ++(deleted ? result.deleted : result.failed);
    };

    remove (file);
    return result;
}
