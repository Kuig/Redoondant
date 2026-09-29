# Redoondant

A small JUCE desktop app that finds files and folders you probably no longer need
(by default in your Downloads folder), lets you review them, and moves the checked
ones to the Recycle Bin, reporting how many items were removed and how much space was freed.

## Usage

1. Choose the folder to analyze (top bar; defaults to `~/Downloads`).
2. Pick a criterion on the left, adjust its settings, optionally enable **Recursive**.
3. Press **Analyze**. Candidates are listed with size and dates; grouped criteria show
   a separator row per group (its checkbox toggles the whole group).
4. Check/uncheck items (click the checkbox column, or select rows and press Space; the
   button below the list cycles through *check all*, *uncheck all* and the *default checks*),
   inspect them in the preview on the right (images, PDF first page, OS thumbnails for
   videos/Office files, text, folder and archive content, audio player, and metadata:
   media tags, EXIF, PDF info, executable version/architecture...). Double-click reveals an item in Explorer;
   right-click opens the Windows Explorer context menu (items deleted or moved from there leave the list).
5. Press **Move to Trash** (a warning alert first asks for confirmation, showing the number of
   items and their total size) or **Move to folder** (pick a destination, then confirm in an info box).
   Items inside an already-checked folder are not counted twice. Moving to a folder never
   overwrites: on a name clash the moved item becomes `name (2).ext`; folders moved across drives
   are copied first and the original is deleted only after the copy succeeded.

### Cache Cleaner

The entry pinned at the bottom of the left bar lists temporary and cache folders (Windows temp and
crash dumps, shader caches, Premiere media cache, Teams, browser and VS Code caches, pip/npm...),
with their sizes, grouped under the deepest directory shared with another listed folder.
Missing folders are shown dimmed. **Add folder...** / **Remove** edit the list, **Reset to defaults** restores it
(defaults use `%APPDATA%`-style variables, so they don't depend on the user name); folders that are too
broad (drive roots, the user profile, AppData, Windows...) can't be added. The buttons act on the *content* of the
checked folders (the folders stay): **Move to Trash**, **Move to folder** or **Delete permanently** (bypasses the
Recycle Bin). Files in use by a running program are reported and skipped. Big caches of browsers/VS Code/pip/npm are listed but not checked at first.

All settings (folder, per-criterion parameters, Recursive flags, column layout and sort,
filters, panel sizes, window position, volume) are saved in
`%APPDATA%\Redoondant\Redoondant.settings`. Results are not saved: press Analyze again.

## Criteria

| Criterion | Finds | Checked by default |
|---|---|---|
| Duplicate files | Identical content (same size, same quick fingerprint, then byte comparison) | All but the shortest name |
| File versions | Same folder and extension, names sharing a long enough start (no assumption on the differing ending) | All but the most recently modified |
| Same name, different extension | e.g. `video.mp4` + `video.mkv`, `thesis.docx` + `thesis.pdf` (optionally across folders / same kind only) | None |
| Same content, different format | Media/documents with the same descriptive metadata in different formats (song FLAC + MP3: artist, title, album, track, duration ± tolerance; photo HEIC + JPG: date taken, camera; DOCX + PDF: title, author, pages) | All but the largest |
| Archives & extracted folders | Archives (zip, 7z, rar, tar, tar.gz/bz2/xz/zst, cab, iso) next to a folder with the same name and identical content | The folder |
| Junk folders | Folders with configurable names (`Build;node_modules;...`) | All |
| Large files | Files above a size threshold | None |
| Date clusters | Items grouped by modified/created date, split on time gaps | None |
| Empty files & folders | 0-byte files, folders without files | All |
| Incomplete downloads | `.crdownload`, `.part`, ... | All |
| Installers & temp files | `.exe`, `.msi`, `.iso`, `.tmp`, `~$*`, ... | None |
| Old files | Not modified for N days | None |
| Multiple criteria | Items marked by at least N criteria, grouped by combination (e.g. "Duplicate files + Old files"); a folder's mark covers its content. Data: existing results of the other pages (with your checks) or re-run all criteria; marked = checked or listed. "Folders only": top folders by number of marked files, or outermost folders with at least X% of their bytes marked | Items: all (checked mode); folders: none |
| Manual inspection | Everything, sortable by any column (files and folders mixed), filterable by name, type, date and size | None |

Encrypted archives are reported as unreadable. Metadata comes from the Windows Property
System, so what is shown for a format depends on the property handlers/codecs installed.

## Code structure

```
Source/
  Core/       FileEntry, FileScanner (tree walk with folder sizes), Grouping helpers,
              ContentHasher, FileCategory, Trash, MoveToFolder, EmptyFolders, Settings, Format
  Archives/   ArchiveReader (list / stream entries) implemented with libarchive
  Metadata/   Metadata model, MetadataReader (merges all sources), PE header reader
  Pdf/        PdfDocument: PDFium wrapper (metadata, page rendering)
  Platform/   Windows services: COM init, shell thumbnails, Property System metadata
  Criteria/   Criterion base class, Parameter declarations, one file per criterion
  UI/         RemovalPage (checked items + Move/Trash/Delete actions), CriterionPage and CleanupPage
              (Cache Cleaner) built on it, ResultsModel/ResultsTable, ParametersPanel, FilterBar,
              PreviewPanel, TextPreview, AudioPlayer
  Tests/      juce::UnitTest suites
```

Criteria are stateless: `analyse()` receives a snapshot of its parameters and runs on a
background thread (cancellable progress window). Their settings are declared as
`Parameter`s; `ParametersPanel` generates the editors and persists the values.

### Adding a criterion

1. Create `Source/Criteria/MyCriterion.cpp` with a class deriving from `Criterion`:
   pass an `Info` (id, name, description, grouped?, filterable?), override
   `createParameters()` if it has settings, and implement `analyse()` — usually
   `FileScanner::scan()` followed by the `Grouping` helpers.
2. Declare its factory in `AllCriteria.h` and add it to `CriterionRegistry.cpp`.
3. Add the file to `Redoondant.jucer` and re-save with the Projucer.

## Building

Dependencies:
- JUCE 8 (modules `juce_core`, `juce_data_structures`, `juce_events`, `juce_graphics`,
  `juce_gui_basics`, `juce_audio_basics`, `juce_audio_formats`, `juce_audio_devices`);
- **libarchive** (static, with zlib, bzip2, lzma, zstd, lz4), from `ThirdParty/vcpkg.json`;
- **PDFium** prebuilt binaries from [bblanchon/pdfium-binaries](https://github.com/bblanchon/pdfium-binaries)
  in `ThirdParty/pdfium` (`pdfium.dll` is copied next to the exe by a post-build step and
  delay-loaded: without it the app still runs, PDFs just get no preview/metadata).

Both third-party folders are git-ignored; to fetch them (network needed):

```
cd ThirdParty
"C:\Program Files\Microsoft Visual Studio\18\Community\VC\vcpkg\vcpkg.exe" install --triplet x64-windows-static-md --x-install-root=vcpkg_installed
curl -L -o pdfium.tgz https://github.com/bblanchon/pdfium-binaries/releases/latest/download/pdfium-win-x64.tgz
mkdir pdfium && tar -xzf pdfium.tgz -C pdfium && del pdfium.tgz
```

Then:

```
Projucer --resave Redoondant.jucer
MSBuild Builds\VisualStudio2026\Redoondant.sln /p:Configuration=Release /p:Platform=x64
```

Library names are linked from the source files that use them (`#pragma comment (lib, ...)`
in `LibArchiveReader.cpp` and `PdfDocument.cpp`); include/library paths are set in the `.jucer`.

Tests: `Redoondant.exe --test` runs the unit tests, writes `%TEMP%\Redoondant-tests.log`
and exits with the number of failures.
