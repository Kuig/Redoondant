#include "ContentKind.h"
#include "FileEntry.h"
#include "FileScanner.h"
#include "Paths.h"
#include "../Archives/ArchiveReader.h"
#include "../Pdf/PdfDocument.h"
#include <optional>

namespace
{
    using Tally = ContentKinds::Classifier::Tally;

    constexpr juce::int64 maxSampleBytes = 10 * 1024 * 1024;    // Uncompressed audio smaller than this is a sample: a song takes more.
    constexpr juce::int64 minVideoStreamBytes = 1024 * 1024;    // A .ts file larger than this is a video stream, not TypeScript.
    constexpr juce::int64 minBookBytes = 20 * 1024 * 1024;      // A PDF this large is a book (or a scanned one).
    constexpr int minBookPages = 150;

    struct KindInfo
    {
        ContentKind kind;
        const char* name;
        const char* extensions;     ///< Space separated, lowercase, without dots.
    };

    // .ts, .wav, .aif, .aiff and .pdf depend on more than the extension: see ContentKinds::ofFile.
    const KindInfo kindTable[] =
    {
        { ContentKind::sourceCode,   "Source code",       "c cc cpp cxx h hh hpp m mm cs java kt py ipynb rb go rs swift js mjs cjs tsx jsx vue php "
                                                          "html htm css scss less sh bat cmd ps1 lua r jl pl sql xml yml yaml toml cmake jucer sln vcxproj gradle" },
        { ContentKind::application,  "Applications",      "exe msi msix appx dmg pkg iso apk jar dll" },
        { ContentKind::audioPlugin,  "Audio plugins",     "vst3 vst aax component clap" },
        { ContentKind::font,         "Fonts",             "ttf otf woff woff2 fon pfb pfm afm" },
        { ContentKind::pictures,     "Pictures",          "jpg jpeg png gif bmp tif tiff webp heic heif raw cr2 cr3 nef arw dng ico" },
        { ContentKind::graphics,     "Graphics & design", "psd ai eps svg fig sketch xd indd afdesign afphoto cdr" },
        { ContentKind::video,        "Video",             "mp4 m4v mov avi mkv wmv webm flv mpg mpeg 3gp mts m2ts" },
        { ContentKind::music,        "Music",             "mp3 m4a aac ogg opus wma flac alac ape" },
        { ContentKind::samples,      "Samples",           "sf2 sfz nki" },
        { ContentKind::mediaProject, "Media projects",    "prproj aep cpr npr als rpp ptx logicx band flp maxpat pd dorico sib musx aup aup3 veg drp" },
        { ContentKind::slides,       "Slides",            "ppt pptx pptm pps ppsx pot potx odp key" },
        { ContentKind::administration, "Administration",  "xls xlsx xlsm ods numbers" },
        { ContentKind::books,        "Books",             "epub mobi azw azw3 kfx fb2 djvu cbz cbr ibooks chm" },
        { ContentKind::documents,    "Documents",         "doc docx odt rtf txt md tex bib pages" },
        { ContentKind::data,         "Data",              "csv tsv json mat parquet h5 hdf5 npy npz pkl feather arff sqlite db sav" },
        { ContentKind::other,        "Other",             "" },
    };

    const std::map<juce::String, ContentKind>& kindsByExtension()
    {
        static const std::map<juce::String, ContentKind> byExtension = []
        {
            std::map<juce::String, ContentKind> result;

            for (const auto& info : kindTable)
                for (const auto& extension : juce::StringArray::fromTokens (info.extensions, " ", {}))
                    result[extension] = info.kind;

            return result;
        }();

        return byExtension;
    }

    juce::String extensionOf (const juce::String& name)
    {
        return name.containsChar ('.') ? name.fromLastOccurrenceOf (".", false, false).toLowerCase() : juce::String();
    }

    std::optional<ContentKind> kindOfExtension (const juce::String& extension)
    {
        const auto& byExtension = kindsByExtension();
        const auto found = byExtension.find (extension);
        return found != byExtension.end() ? std::optional<ContentKind> (found->second) : std::nullopt;
    }

    const juce::StringArray sampleWords { "sample", "samples", "loop", "loops", "kick", "kicks", "snare", "snares", "hat", "hats",
                                          "hihat", "hihats", "ir", "irs", "impulse", "impulses", "bpm", "kit", "kits",
                                          "oneshot", "oneshots", "shot", "shots", "drum", "drums" };

    const juce::StringArray bookWords { "ebook", "ebooks", "edition", "isbn", "book", "books", "libri", "libro", "epub" };

    /** Whether the path has one of `words` as a word of its own (letters only, so "120bpm" and "Kick_01" count). */
    bool hasWord (const juce::String& path, const juce::StringArray& words)
    {
        const auto lower = path.toLowerCase();
        juce::String letters;

        for (auto p = lower.getCharPointer(); ! p.isEmpty(); ++p)
            letters += juce::CharacterFunctions::isLetter (*p) ? *p : (juce::juce_wchar) ' ';

        for (const auto& word : juce::StringArray::fromTokens (letters, " ", {}))
            if (words.contains (word))
                return true;

        return false;
    }

    /** Whether the name holds an ISBN with a valid check digit: 13 digits starting with 978 or 979, or 10 written with
        hyphens (bare, 10 digits are more often a timestamp). Hyphens are ignored.
    */
    bool hasIsbn (const juce::String& name)
    {
        const juce::String digitChars ("0123456789");

        const auto isValid = [&] (const juce::String& digits)
        {
            int sum = 0;

            if (digits.length() == 13)
            {
                if (! digits.containsOnly (digitChars) || ! (digits.startsWith ("978") || digits.startsWith ("979")))
                    return false;

                for (int i = 0; i < 13; ++i)
                    sum += (digits[i] - '0') * (i % 2 == 0 ? 1 : 3);

                return sum % 10 == 0;
            }

            if (! digits.substring (0, 9).containsOnly (digitChars) || ! (digitChars + "x").containsChar (digits[9]))
                return false;

            for (int i = 0; i < 10; ++i)
                sum += (digits[i] == 'x' ? 10 : digits[i] - '0') * (10 - i);

            return sum % 11 == 0;
        };

        const auto lower = name.toLowerCase();
        juce::String runs;

        for (auto p = lower.getCharPointer(); ! p.isEmpty(); ++p)
            runs += (digitChars + "-x").containsChar (*p) ? *p : (juce::juce_wchar) ' ';

        for (const auto& run : juce::StringArray::fromTokens (runs, " ", {}))
        {
            const auto digits = run.removeCharacters ("-");

            if ((digits.length() == 13 || (digits.length() == 10 && run.containsChar ('-'))) && isValid (digits))
                return true;
        }

        return false;
    }

    /** What makes a PDF a book without opening it: its size, an ISBN in its name, or a word such as "ebook" in its path. */
    bool looksLikeBook (const juce::String& path, juce::int64 size)
    {
        return size >= minBookBytes || hasIsbn (path.fromLastOccurrenceOf ("/", false, false)) || hasWord (path, bookWords);
    }

    bool isInstaller (const juce::String& name)
    {
        const auto extension = extensionOf (name);
        return extension == "msi" || (extension == "exe" && (name.containsIgnoreCase ("setup") || name.containsIgnoreCase ("install")));
    }

    /** The kind a file or folder name alone decides (installers aside: they also depend on how deep they are). */
    std::optional<ContentKind> markerOf (const juce::String& name, bool isFolder)
    {
        if (FileScanner::isProjectFile (name) || (isFolder && FileScanner::versionControlFolderNames().contains (name, true)))
            return ContentKind::sourceCode;

        const auto kind = kindOfExtension (extensionOf (name));

        if (kind == ContentKind::audioPlugin || kind == ContentKind::mediaProject)
            return kind;

        if (isFolder && extensionOf (name) == "app")
            return ContentKind::application;

        return std::nullopt;
    }

    /** Counts a file of the given kind `depth` levels below the item (0: directly inside it). */
    void countFile (Tally& tally, const juce::String& path, ContentKind kind, int depth)
    {
        ++tally.files;

        if (kind != ContentKind::other)
            ++tally.points[(size_t) kind];

        const auto name = path.fromLastOccurrenceOf ("/", false, false);

        if (const auto marker = markerOf (name, false))
            tally.marked[(size_t) *marker] = true;

        if (isInstaller (name) && depth <= 1)
            (depth == 0 ? tally.installerAtTop : tally.installerBelow) = true;
    }
}

juce::String ContentKinds::nameOf (ContentKind kind)
{
    for (const auto& info : kindTable)
        if (info.kind == kind)
            return info.name;

    return {};
}

const std::vector<ContentKind>& ContentKinds::all()
{
    static const std::vector<ContentKind> kinds = []
    {
        std::vector<ContentKind> result;

        for (const auto& info : kindTable)
            result.push_back (info.kind);

        return result;
    }();

    return kinds;
}

ContentKind ContentKinds::ofFile (const juce::String& relativePath, juce::int64 size)
{
    const auto path = relativePath.replaceCharacter ('\\', '/');
    const auto extension = extensionOf (path.fromLastOccurrenceOf ("/", false, false));

    if (extension == "ts")
        return size > minVideoStreamBytes ? ContentKind::video : ContentKind::sourceCode;

    if (extension == "wav" || extension == "aif" || extension == "aiff")
        return size < maxSampleBytes || hasWord (path, sampleWords) ? ContentKind::samples : ContentKind::music;

    if (extension == "pdf")
        return looksLikeBook (path, size) ? ContentKind::books : ContentKind::documents;

    return kindOfExtension (extension).value_or (ContentKind::other);
}

//==============================================================================
void ContentKinds::Classifier::Tally::addFolder (const Tally& folder)
{
    for (size_t i = 0; i < kinds; ++i)
    {
        points[i] += folder.points[i];
        marked[i] = marked[i] || folder.marked[i];
    }

    files += folder.files;
    installerBelow = installerBelow || folder.installerAtTop;
}

ContentKind ContentKinds::Classifier::Tally::winner() const
{
    auto best = ContentKind::other;
    int bestScore = 0;

    for (const auto kind : all())
    {
        const auto i = (size_t) kind;
        const bool isMarked = marked[i] || (kind == ContentKind::application && (installerAtTop || installerBelow));
        auto score = points[i] + (isMarked ? files + 1 : 0);      // More than any kind without a marker can have.

        // Spreadsheets bring the documents and pictures that go with them (invoices, receipts, scans).
        if (kind == ContentKind::administration && points[i] > 0)
            score += points[(size_t) ContentKind::documents] + points[(size_t) ContentKind::pictures];

        if (kind != ContentKind::other && score > bestScore)
        {
            best = kind;
            bestScore = score;
        }
    }

    return best;
}

ContentKinds::Classifier::Classifier (ScanContext c, PageCounter counter)
    : context (std::move (c)), pagesOf (counter != nullptr ? std::move (counter) : PageCounter (PdfDocument::pageCount)) {}

ContentKind ContentKinds::Classifier::kindOnDisk (const juce::File& file, const juce::String& path, juce::int64 size) const
{
    const auto kind = ofFile (path, size);

    if (kind == ContentKind::documents && PdfDocument::isPdf (file) && pagesOf (file) >= minBookPages)
        return ContentKind::books;

    return kind;
}

ContentKind ContentKinds::Classifier::of (const FileEntry& entry)
{
    if (entry.isDirectory)
        return entry.file.isSymbolicLink() ? ContentKind::other : folderTally (entry.file).winner();   // Links are not followed.

    if (ArchiveReader::isArchive (entry.file))
        return archiveTally (entry.file).winner();

    Tally tally;
    const auto path = entry.file.getParentDirectory().getFileName() + "/" + entry.name();
    countFile (tally, path, kindOnDisk (entry.file, path, entry.size), 0);
    return tally.winner();
}

const ContentKinds::Classifier::Tally& ContentKinds::Classifier::folderTally (const juce::File& folder)
{
    const auto key = Paths::key (folder);

    if (const auto known = folders.find (key); known != folders.end())
        return known->second;

    Tally tally;
    const auto name = folder.getFileName();

    if (const auto marker = markerOf (name, true))
        tally.marked[(size_t) *marker] = true;

    // A repository's own data (packs, refs) says nothing more than that it is one.
    if (! FileScanner::versionControlFolderNames().contains (name, true))
    {
        for (const auto& item : juce::RangedDirectoryIterator (folder, false, "*", juce::File::findFilesAndDirectories,
                                                                juce::File::FollowSymlinks::no))
        {
            if (context.isCancelled())
                break;

            const auto file = item.getFile();

            if (! item.isDirectory())
            {
                const auto path = name + "/" + file.getFileName();
                countFile (tally, path, kindOnDisk (file, path, item.getFileSize()), 0);
            }
            else if (! file.isSymbolicLink())
                tally.addFolder (folderTally (file));
        }
    }

    return folders[key] = tally;
}

ContentKinds::Classifier::Tally ContentKinds::Classifier::archiveTally (const juce::File& archive)
{
    Tally tally;
    const auto entries = ArchiveReader::list (archive, context);

    if (! entries)
        return tally;       // Unreadable: nothing recognised.

    for (const auto& entry : *entries)
    {
        const auto segments = juce::StringArray::fromTokens (entry.path, "/", {});

        for (int i = 0; i < segments.size() - 1; ++i)
            if (const auto marker = markerOf (segments[i], true))
                tally.marked[(size_t) *marker] = true;

        countFile (tally, entry.path, ofFile (entry.path, entry.size), juce::jmax (0, segments.size() - 1));
    }

    return tally;
}
