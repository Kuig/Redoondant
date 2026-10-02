#include "CleanupPage.h"
#include "../Core/EmptyFolders.h"
#include "../Core/Format.h"

namespace
{
    constexpr int margin = 12;
    constexpr int rowHeight = 26;
    constexpr int gap = 8;
    const juce::String noneChecked ("-");   // Stored when the list of checked folders is empty (as opposed to never saved).

    /** The entry of a listed folder: measured if it exists, flagged missing otherwise. */
    FileEntry measure (const juce::File& folder)
    {
        if (folder.isDirectory())
            return FileEntry::fromFile (folder);

        FileEntry entry;
        entry.file = folder;
        entry.isDirectory = true;
        entry.missing = true;
        return entry;
    }

    void warn (const juce::String& boxTitle, const juce::String& message)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, boxTitle, message);
    }
}

CleanupPage::CleanupPage (SettingsScope s)
    : RemovalPage (s)
{
    title.setText ("Cache Cleaner", juce::dontSendNotification);
    title.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    description.setText ("Empty temporary and cache folders. Checked folders keep existing, only their content is moved or deleted. "
                         "Close the programs that use a cache first, or their files will be skipped.",
                         juce::dontSendNotification);
    description.setJustificationType (juce::Justification::topLeft);
    description.setMinimumHorizontalScale (1.0f);

    addButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Add a folder to empty", juce::File (EmptyFolders::expand ("%LOCALAPPDATA%")));
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [this] (const juce::FileChooser& fc)
                              {
                                  if (fc.getResult() != juce::File())
                                      addFolder (fc.getResult());
                              });
    };
    removeButton.onClick = [this] { removeSelected(); };
    resetButton.onClick = [this] { resetToDefaults(); };
    refreshButton.onClick = [this] { refresh(); };

    table.onItemSelected = [this] (const FileEntry* entry)
    {
        if (onItemSelected != nullptr)
            onItemSelected (entry);

        updateButtons();
    };

    for (auto* component : std::initializer_list<juce::Component*> { &title, &description, &status, &addButton, &removeButton, &resetButton, &refreshButton })
        addAndMakeVisible (component);

    loadFolders();
    updateButtons();
    refresh();
}

CleanupPage::~CleanupPage()
{
    measurer.removeAllJobs (true, 5000);
}

void CleanupPage::resized()
{
    auto area = getLocalBounds().reduced (margin);

    title.setBounds (area.removeFromTop (28));
    description.setBounds (area.removeFromTop (36));
    area.removeFromTop (gap);

    auto actions = area.removeFromTop (rowHeight);

    for (auto* button : { &addButton, &removeButton, &resetButton, &refreshButton })
    {
        button->setBounds (actions.removeFromLeft (button == &resetButton ? 130 : 100));
        actions.removeFromLeft (gap);
    }

    status.setBounds (actions);
    area.removeFromTop (gap);

    layoutTableAndFooter (area);
}

void CleanupPage::loadFolders()
{
    folders.clear();
    checkedPaths.clear();

    const auto add = [this] (const juce::String& stored)
    {
        Folder folder { stored, juce::File (EmptyFolders::expand (stored)), false };

        for (const auto& d : EmptyFolders::defaults())
            if (d.path == stored)
                folder.checkedByDefault = d.checkedByDefault;

        folders.push_back (std::move (folder));
    };

    if (settings.has ("folders"))
        for (const auto& stored : settings.getList ("folders"))
            add (stored);
    else
        for (const auto& d : EmptyFolders::defaults())
            add (d.path);

    if (settings.has ("checked"))
    {
        for (const auto& stored : settings.getList ("checked"))
            if (stored != noneChecked)
                checkedPaths.insert (juce::File (EmptyFolders::expand (stored)).getFullPathName());
    }
    else
    {
        for (const auto& folder : folders)
            if (folder.checkedByDefault)
                checkedPaths.insert (folder.file.getFullPathName());
    }
}

void CleanupPage::saveFolders()
{
    juce::StringArray stored, checked;

    for (const auto& folder : folders)
    {
        stored.add (folder.stored);

        if (checkedPaths.count (folder.file.getFullPathName()) > 0)
            checked.add (folder.stored);
    }

    settings.setList ("folders", stored);
    settings.setList ("checked", checked.isEmpty() ? juce::StringArray (noneChecked) : checked);
}

void CleanupPage::addFolder (const juce::File& folder)
{
    if (! folder.isDirectory())
        return;

    if (EmptyFolders::isTooBroad (folder))
    {
        warn ("Folder too broad", "Emptying " + wrappable (folder) + " could break Windows or your programs, so it can't be added.");
        return;
    }

    for (const auto& existing : folders)
        if (existing.file == folder)
        {
            warn ("Already listed", wrappable (folder) + " is already in the list.");
            return;
        }

    folders.push_back ({ EmptyFolders::contract (folder.getFullPathName()), folder, false });
    saveFolders();
    refresh();
}

void CleanupPage::removeSelected()
{
    const auto selected = table.getSelectedFiles();

    folders.erase (std::remove_if (folders.begin(), folders.end(), [&] (const Folder& f)
                                   {
                                       if (! selected.contains (f.file))
                                           return false;

                                       checkedPaths.erase (f.file.getFullPathName());
                                       return true;
                                   }),
                   folders.end());

    saveFolders();
    refresh();
}

void CleanupPage::resetToDefaults()
{
    confirm (juce::MessageBoxIconType::QuestionIcon, "Reset to defaults",
             "Replace the list (and its checks) with the default folders?", "Reset",
             [this]
             {
                 settings.remove ("folders");
                 settings.remove ("checked");
                 loadFolders();
                 refresh();
             });
}

void CleanupPage::refresh()
{
    const int request = ++generation;
    measurer.removeAllJobs (false, 0);      // A running measurement finishes and is ignored.

    status.setText ("Measuring sizes...", juce::dontSendNotification);

    std::vector<juce::File> files;

    for (const auto& folder : folders)
        files.push_back (folder.file);

    measurer.addJob ([safeThis = juce::Component::SafePointer<CleanupPage> (this), request, files = std::move (files)]
    {
        std::vector<FileEntry> entries;

        for (const auto& file : files)
            entries.push_back (measure (file));

        juce::MessageManager::callAsync ([safeThis, request, entries = std::move (entries)]() mutable
        {
            if (safeThis != nullptr && safeThis->generation == request)
                safeThis->showMeasured (std::move (entries));
        });
    });
}

void CleanupPage::showMeasured (std::vector<FileEntry> entries)
{
    std::vector<juce::File> files;

    for (const auto& entry : entries)
        files.push_back (entry.file);

    AnalysisResult result;
    juce::int64 total = 0;

    for (const auto& group : EmptyFolders::groupByCommonParent (files))
    {
        ResultGroup g { group.parent.getFullPathName(), {} };

        for (const auto index : group.members)
        {
            auto entry = entries[index];
            entry.selected = ! entry.missing && folders[index].checkedByDefault;
            total += entry.size;
            g.items.push_back (std::move (entry));
        }

        result.groups.push_back (std::move (g));
    }

    model.setResult (std::move (result), {}, true);
    model.checkWhere ([this] (const FileEntry& e) { return checkedPaths.count (e.file.getFullPathName()) > 0; });
    table.refresh();

    status.setText (Format::size (total) + " in these folders, measured at " + juce::Time::getCurrentTime().formatted ("%H:%M"),
                    juce::dontSendNotification);
    updateSummary();
    updateButtons();
}

void CleanupPage::checksChanged()
{
    bool changed = false;

    for (const auto& entry : model.getAllEntries())
    {
        if (entry.missing)
            continue;       // Keeps its saved state in case the folder appears later.

        const auto path = entry.file.getFullPathName();
        changed |= entry.selected ? checkedPaths.insert (path).second : checkedPaths.erase (path) > 0;
    }

    if (changed)
        saveFolders();
}

void CleanupPage::updateButtons()
{
    removeButton.setEnabled (! table.getSelectedFiles().isEmpty());
}

std::vector<FileEntry> CleanupPage::itemsToRemove() const
{
    std::vector<FileEntry> contents;

    for (const auto& folder : model.getCheckedEntries())
    {
        if (EmptyFolders::isTooBroad (folder.file))
            continue;

        auto children = EmptyFolders::contentsOf (folder.file);
        contents.insert (contents.end(), children.begin(), children.end());
    }

    return contents;
}

juce::String CleanupPage::describeChecked() const
{
    juce::int64 bytes = 0;
    const auto checked = model.getCheckedEntries();

    for (const auto& folder : checked)
        bytes += folder.size;

    return "the contents of " + Format::count (checked.size(), "folder") + " (" + Format::size (bytes) + ")";
}

juce::String CleanupPage::failureAdvice() const
{
    return "Items in use by a running program can't be removed: close it (Teams, Premiere, the browser...) and try again. "
           "Folders under C:\\Windows may need administrator rights.";
}

juce::String CleanupPage::summaryText (size_t checkedCount, juce::int64 checkedBytes) const
{
    return model.hasResult()
             ? Format::count (model.getVisibleCount(), "folder") + " listed, "
                 + juce::String ((int) checkedCount) + " checked (" + Format::size (checkedBytes) + ")"
             : juce::String ("Measuring...");
}
