# Redoondant

A small JUCE desktop app that finds files and folders you probably no longer need
(by default in your Downloads folder), lets you review them, and moves the checked
ones to the Recycle Bin, reporting how many items were removed and how much space was freed.

## Usage

1. Choose the folder to analyze (top bar; defaults to `~/Downloads`).
2. Pick a criterion on the left, adjust its settings, optionally enable **Recursive**.
3. Press **Analyze**. Candidates are listed with size and dates; grouped criteria show
   a separator row per group (its checkbox toggles the whole group).
4. Check/uncheck items (click the checkbox column, or select rows and press Space),
   inspect them in the preview on the right (images, text, folder content, audio player,
   metadata). Double-click reveals an item in Explorer.
5. Press **Move to Trash**. Items inside an already-checked folder are not counted twice.

All settings (folder, per-criterion parameters, Recursive flags, column layout and sort,
filters, panel sizes, window position, volume) are saved in
`%APPDATA%\Redoondant\Redoondant.settings`. Results are not saved: press Analyze again.

## Criteria

| Criterion | Finds | Checked by default |
|---|---|---|
| Duplicate files | Identical content (same size, same quick fingerprint, then byte comparison) | All but the shortest name |
| File versions | Same folder and extension, names sharing a long enough start (no assumption on the differing ending) | All but the most recently modified |
| Archives & extracted folders | `.zip`, `.tar`, `.tar.gz`/`.tgz` next to a folder with the same name and identical content | The folder |
| Junk folders | Folders with configurable names (`Build;node_modules;...`) | All |
| Large files | Files above a size threshold | None |
| Date clusters | Items grouped by modified/created date, split on time gaps | None |
| Empty files & folders | 0-byte files, folders without files | All |
| Incomplete downloads | `.crdownload`, `.part`, ... | All |
| Installers & temp files | `.exe`, `.msi`, `.iso`, `.tmp`, `~$*`, ... | None |
| Old files | Not modified for N days | None |
| Manual inspection | Everything, sortable by any column (files and folders mixed), filterable by name, type, date and size | None |

Archives: `.7z`, `.rar`, `.tar.xz`, `.tar.bz2` and encrypted zips are not supported
(they would require third-party libraries).

## Code structure

```
Source/
  Core/       FileEntry, FileScanner (tree walk with folder sizes), Grouping helpers,
              ContentHasher, FileCategory, Trash, Settings, Format
  Archives/   ArchiveReader interface + zip (juce::ZipFile) and tar/tar.gz readers
  Criteria/   Criterion base class, Parameter declarations, one file per criterion
  UI/         CriterionPage, ResultsModel/ResultsTable, ParametersPanel, FilterBar,
              PreviewPanel, AudioPlayer
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

Dependencies: JUCE 8 only (modules `juce_core`, `juce_data_structures`, `juce_events`,
`juce_graphics`, `juce_gui_basics`, `juce_audio_basics`, `juce_audio_formats`,
`juce_audio_devices`).

```
Projucer --resave Redoondant.jucer
MSBuild Builds\VisualStudio2026\Redoondant.sln /p:Configuration=Release /p:Platform=x64
```

Tests: `Redoondant.exe --test` runs the unit tests, writes `%TEMP%\Redoondant-tests.log`
and exits with the number of failures.
