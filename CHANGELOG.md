# Change Log
All notable changes to this project are documented in this file.
The format is based on [Keep a Changelog](https://keepachangelog.com/en/0.3.0/)
and this project adheres to [Semantic Versioning](https://semver.org/).

## [Unreleased]
### Added
- The line between the preview and the metadata can be dragged to give more room to either; the position is remembered.
- *Content kind*: a new criterion, after *Manual inspection*, that groups every item of the folder by what it holds, judged from the files
  inside folders and archives: source code, applications, audio plugins, fonts, pictures, graphics & design, video, music, samples,
  media projects, slides, administration, books, documents, data or other. Project files, `.git` folders, installers, plugin bundles and
  media projects decide the kind; an item with a spreadsheet counts its documents and pictures for administration; long PDFs are books.
  It is not recursive and checks nothing.
- *Content kind* column, filter and histogram property on every criterion page, filled in the background after each analysis.
- *Explorer menu* check box in the top bar: adds *Open in Redoondant* to the right-click menu of folders in Explorer (and of the empty
  space inside a folder; on Windows 11 under *Show more options*), for the current user only. Redoondant opens on the folder passed on
  the command line. *Reset settings* removes the entry too.

### Fixed
- *Move to folder* and *Delete permanently*: the confirmation said "0 items (0 bytes)"; the action already got the right items.

## [1.2.0] - 2026-10-04
### Added
- *Similar metadata*: option *Strict identity*, off by default: the title (date taken for photos) is required, and the page count and the
  duration don't count as matching fields, so documents of one page by the same author without a title are no longer grouped.
- Histogram in the preview area while nothing is selected: folder, size, modified, created, content created or type, by number of items
  or by total size (linear or logarithmic), with three layers (all items, items passing the filters, checked items), statistics on hover and a click
  on a bar that sets the filters to show only its items.
- *Large elements*: with *Recursive* and *Include folders* on, the results are grouped by outermost folder (the folder with the large
  elements inside it, largest group first; the elements with none inside share the last group) instead of listing a folder and its contents as unrelated rows.
- *Duplicate files*: option *Skip installed dependencies*, on by default: the content of `node_modules`, `bower_components`,
  `site-packages` and of Python virtual environments is left out (each package needs its own copy of its files; *Junk folders* removes
  these folders whole).
- Filters: *in folder*, *exclude if path contains* (words separated by `;`, matched against the folders and not the name; shown only when Recursive is on), and the kind of date to filter by (modified, created or content created).

### Changed
- *Multiple criteria* is now two pages, *Multiple criteria: items* and *Multiple criteria: folders* (the old "Show" choice), each with only the settings it uses. The settings saved for the old page that still exist (data, counts as marked, minimum criteria, minimum files and share) are kept for the items page and lost for the folders page.
- The date filter uses the same buckets as the histogram (today, 1-7 days, 7-30 days, 30 days - 1 year, 1-2 years, more than 2 years ago,
  unknown date) instead of cumulative ranges.
- Clicking the only selected item again deselects it, which brings the histogram back.

### Removed
- *Duplicate files*: the *Algorithm* choice and the *Checksum* algorithm. Files are always compared by size, then by a quick fingerprint
  (first and last 64 KB), then by content (stopping at the first difference), which is much faster.

### Fixed
- *Multiple criteria* no longer makes the analysis thread wait for the user interface: the other criteria, with their settings and results, are handed over
  when Analyze is pressed.
- Items modified after the analysis (a duplicate that was edited, a folder that got new files) are left untouched by Move to Trash, Move to folder and
  Delete permanently, and listed as "changed since the analysis".
- Move to Trash and Delete permanently warn when every item of some group is checked, so nothing of that group would be kept.
- Typing a folder that doesn't exist in the folder bar restores the previous folder instead of discarding the results of every criterion.
- An error (for example out of memory) inside an analysis or a removal no longer closes the program: the analysis reports it, and a removal lists the item as failed.
- Reading content dates, measuring the Cache Cleaner folders and building previews can no longer touch a closed page, and hiding the *Content created* column after a new analysis no longer leaves a stale date reader running.
- *Empty files & folders* no longer lists (and checks) a folder that only looks empty because it holds a junction or symbolic link, or because it could not be read.
- The Cache Cleaner refuses System32, Program Files, the user's Documents/Desktop/Pictures/Music/Videos, OneDrive and anything inside them, not only the folders that contain them; entries of the saved list that point to such folders or contain an undefined `%VARIABLE%` are skipped.
- **Move to Trash** no longer deletes for good what Windows can't recycle: items on network shares, removable drives or drives with the Recycle Bin turned off are left untouched and listed (the confirmation says how many), and an item Windows refuses to recycle is reported instead of deleted.
- Moving a folder to another drive no longer follows junctions or symbolic links (a folder containing any is left untouched), checks the copy against the original, and reports "copied, but the original could not be removed completely" instead of a plain failure when a file is in use.
- Items already gone when an action runs are counted apart instead of inflating "moved/freed"; nested paths are recognised whatever their case; a moved `.gitignore` becomes `.gitignore (2)`.
- The window froze for a long time on big lists while the content dates were being read: they now reach the screen in larger batches,
  the histogram is refreshed at most every quarter of a second and the folder filters are cheaper.
- Changing the folder to analyze now discards the results of every criterion; they used to stay on screen
  although they described the previous folder.
- *Multiple criteria* recognises the same item in the lists of two pages even when they spell its path with different upper/lower case.
- Rows hidden by a filter follow the default checks too, so a filter that reveals them shows the current default instead of an older one.
- File icons could stay missing for good in very long lists.
- Move to Trash, Move to folder and Delete permanently act on exactly the items their confirmation described: content dates arriving while
  the question is open (which can re-sort a list and change the item each group keeps) no longer change what is removed.
- The Cache Cleaner's confirmation no longer counts twice a folder listed inside another checked one.
- *Duplicate files*: the candidates of a group are compared side by side in large blocks, so each file is read once (it used
  to be read again for every other copy, alternating small blocks between two files: very slow on hard disks).
- *Duplicate files* and *Empty files & folders* leave out the data of version control (`.git`, `.svn`, `.hg`, `.bzr`, `_darcs`, `CVS`,
  `.jj`, `.pijul` folders: deleting a repository's packs or its empty `refs` folders breaks it), with the option *Skip version control
  data* (on by default; its tooltip lists the folders); *Empty files & folders* also leaves out placeholders such as `.gitkeep` and `__init__.py`.
- *Junk folders*: generic names (`Release`, `Debug`, `bin`, `obj`, `target`, `x64`, `Build`, `Builds`) only count in a folder holding a
  project file, so `Music\Release` is no longer proposed. The list is a setting that can be edited or switched off.
- *Archives & extracted folders*: an archive that holds the same path twice no longer matches a folder with an extra file; folders that
  could not be listed completely are not matched; without *Ignore folder name* an archive with no folder of the same name is no longer
  decompressed for nothing.
- *Multiple criteria* with *Run all criteria* walks the folder once instead of once per criterion, and counting the folders is faster.
- **Move to Trash** is much faster on many items (one shell operation per batch instead of one per item). When Windows can't recycle an
  item (too big for the Recycle Bin) its question now shows above the progress window, and an item deleted for good after that question is
  reported as such instead of "moved to the Recycle Bin". Drives mounted in a folder are recognised by their own volume, and the drive
  checks before the confirmation are done once per folder instead of once per item.
- The Cache Cleaner checks the real path of a folder (junctions, symbolic links and `subst` drives resolved) against the protected ones,
  and refuses `%APPDATA%\Microsoft`, `%LOCALAPPDATA%\Microsoft`, `%LOCALAPPDATA%\Packages` themselves and the folders of keys and
  credentials (`Protect`, `Credentials`, `Crypto`, `SystemCertificates`, `Vault`).
- Big lists react much faster: the filters are evaluated once when they change (not again on every click, key or repaint), sorting by
  name, folder or type builds each text once, and typing in a filter field refilters when the typing pauses.
- Selecting thousands of rows (Ctrl+A) no longer checks every file on disk before the preview: only the ones shown.
- The *Content created* dates are read faster (only the dated properties, without formatting the others; the files' dates come from the
  scan), *Similar metadata* fills them from what it already read, and the list is re-sorted once at the end of the reading instead of at
  every batch. A failed reading no longer leaves "reading dates" on screen.
- The preview of a big archive (`.tar.gz`, solid 7z, zip with many entries) stops at the first 5000 entries and when another item is
  selected, instead of blocking every later preview until the whole archive was decompressed.
- File icons are shared by all the pages, so each is asked to Windows once.
- `pdfium.dll` is loaded only from the program's folder, and an incompatible one is ignored (PDFs get no preview) instead of closing the
  program at the first PDF.
- The summary line no longer slows down on big lists of checked items (nested items are found with one sort instead of one walk up per item).
- **The last copy is never removed.** In the groups of copies (duplicates, versions, same name, similar metadata, an archive and its folder) a
  checked item is left untouched when the copy kept in its group has been removed or changed since the analysis, for example from another
  page or in Explorer; and what an action removes now leaves the lists of the other pages too.
- A folder changed deep inside since the analysis (a new file in a sub-folder) is left untouched: only changes of its direct content were noticed.
- Delete permanently removes read-only files too, and a folder deleted only in part (a file in use) is reported as such and shown with its
  new size, instead of "could not be removed".
- Move to folder no longer copies an item whose rename failed on the same drive (a file in use): that left a second copy and a half-deleted
  original. A link (junction, symbolic link) is not copied to another drive.
- Cancel stops copies between drives and deletions in the middle of a big item, and the Cache Cleaner while it lists the folders to empty.
- Cancel no longer kills an analysis or a removal after 10 seconds (which could leave a lock held or lose the report): the operation stops at
  the next item. Closing the window while one runs is refused with a message.

## [1.1.0] - 2026-10-03
### Added
- Duplicate files: choice of algorithm, *Size + content* (default) or *Checksum* (SHA-256 of every file).
- Large elements (formerly Large files): option to include folders.
- Default selection: a drop-down at the bottom left of each list chooses what the *default checks* state means
  (none, all, keep first following the sort order, keep longest name, keep shortest name, folders only, files only).
  Criteria now suggest an initial selection and sort instead of fixing the checks.
- Filters on every page: several types at once (with "All types") and a "doesn't contain" name filter.
- Content created: the oldest date found in a file's metadata (photo taken, document created...), as a table column and as
  a choice in Date clusters (*File created* / *File modified* / *Content created*) and in Old files.
- Drop-down to order the groups of grouped criteria (analysis order, name, most items, largest, newest, oldest).
- Selecting several items shows all their previews in a grid, with only the properties that differ between the files.
- Progress window with a Cancel button while moving, trashing or deleting items.
- "?" button that opens the project page on GitHub; tooltips are now shown.
- "Reset settings" button that deletes the saved settings (after confirmation) and closes the application.
- CHANGELOG.md.

### Changed
- The check box in the table header cycles the checks of all rows (default, all, none); the check box of a group header
  cycles that group. The "Check all" button of the footer is gone.
- Same content, different format is now called Similar metadata, with a shorter description.
- Criteria descriptions say what is checked *by default*, since the default selection can be changed.
- The filter bar is more compact ("Name contains ... and not ..."), without the Type and Date labels,
  and is separated from the analysis controls by a line.
- Multiple criteria checks nothing by default.
- Old files: the date to use (created, modified or content created) is chosen from a drop-down.

### Fixed
- A filter saved in an older format could be read as a wrong file type.

## [1.0.0] - 2026-10-02
### Added
- First release: scans a folder with pluggable criteria (duplicate files, file versions, same name with a different extension,
  similar content, archives and their extracted folders, junk folders, large files, date clusters, empty files and folders,
  incomplete downloads, installers and junk files, old files, multiple criteria, manual inspection) and lists the candidates
  for deletion with check boxes, sizes, dates and groups.
- Preview of images, text, PDF, audio, videos, Office files, folders and archives, with file metadata.
- Move to Trash, Move to folder and Delete permanently, with confirmation and a report.
- Cache Cleaner: empties temporary and cache folders.
- Windows x64 build (JUCE 8).

[Unreleased]: https://github.com/Kuig/Redoondant/compare/v1.2.0...HEAD
[1.2.0]: https://github.com/Kuig/Redoondant/compare/v1.1.0...v1.2.0
[1.1.0]: https://github.com/Kuig/Redoondant/compare/v1.0.0...v1.1.0
[1.0.0]: https://github.com/Kuig/Redoondant/releases/tag/v1.0.0
