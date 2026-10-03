# Change Log
All notable changes to this project are documented in this file.
The format is based on [Keep a Changelog](https://keepachangelog.com/en/0.3.0/)
and this project adheres to [Semantic Versioning](https://semver.org/).

## [Unreleased]
### Added
- Histogram in the preview area while nothing is selected: folder, size, modified, created, content created or type, by number of items
  or by total size (linear or logarithmic), with three layers (all items, items passing the filters, checked items), statistics on hover and a click
  on a bar that sets the filters to show only its items.
- Filters: *in folder*, *exclude if path contains* (words separated by `;`, matched against the folders and not the name; shown only when Recursive is on), and the kind of date to filter by (modified, created or content created).

### Changed
- The date filter uses the same buckets as the histogram (today, 1-7 days, 7-30 days, 30 days - 1 year, 1-2 years, more than 2 years ago,
  unknown date) instead of cumulative ranges.
- Clicking the only selected item again deselects it, which brings the histogram back.

### Fixed
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

[Unreleased]: https://github.com/Kuig/Redoondant/compare/v1.1.0...HEAD
[1.1.0]: https://github.com/Kuig/Redoondant/compare/v1.0.0...v1.1.0
[1.0.0]: https://github.com/Kuig/Redoondant/releases/tag/v1.0.0
