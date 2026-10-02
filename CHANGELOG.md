# Change Log
All notable changes to this project are documented in this file.
The format is based on [Keep a Changelog](https://keepachangelog.com/en/0.3.0/)
and this project adheres to [Semantic Versioning](https://semver.org/).

## [Unreleased]

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
