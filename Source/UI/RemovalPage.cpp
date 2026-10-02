#include "RemovalPage.h"
#include "../Core/Format.h"
#include "../Core/MoveToFolder.h"
#include "../Metadata/ContentDate.h"
#include "../Platform/ComInit.h"
#include "../Platform/ShellContextMenu.h"

namespace
{
    constexpr int rowHeight = 26;
    constexpr int gap = 8;

    /** Text with paths that message boxes can wrap: invisible break opportunities (zero-width spaces) after each separator. */
    juce::String wrappable (const juce::String& text)
    {
        const juce::String zeroWidthSpace (juce::CharPointer_UTF8 ("\xe2\x80\x8b"));
        return text.replace ("\\", "\\" + zeroWidthSpace).replace ("/", "/" + zeroWidthSpace);
    }
}

juce::String RemovalPage::wrappable (const juce::File& path)
{
    return ::wrappable (path.getFullPathName());
}

RemovalPage::RemovalPage (SettingsScope s)
    : settings (s)
{
    trashButton.onClick = [this] { moveCheckedToTrash(); };
    moveButton.onClick = [this] { moveCheckedToFolder(); };
    deleteButton.onClick = [this] { deleteChecked(); };
    table.onHeaderCheckClicked = [this] { cycleChecks(); };

    table.restoreLayoutState (settings.get ("table"));
    table.onLayoutChanged = [this]
    {
        settings.set ("table", table.getLayoutState());
        loadContentDates (false);       // The column may have just been shown or sorted.
    };
    table.onCheckedChanged = [this] { updateSummary(); };
    table.onContextMenu = [this] (const juce::Array<juce::File>& files, juce::Point<int> position)
    {
        if (ShellContextMenu::show (files, position, *this))
            forgetMissing (files);
    };
    table.onItemSelected = [this] (const FileEntry* entry)
    {
        if (onItemSelected != nullptr)
            onItemSelected (entry);
    };

    for (auto* component : std::initializer_list<juce::Component*> { &table, &summary, &moveButton, &trashButton, &deleteButton })
        addAndMakeVisible (component);

    // Red: unlike the other actions, this one can't be undone.
    deleteButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xffb3261e));
    deleteButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    deleteButton.setColour (juce::TextButton::textColourOnId, juce::Colours::white);

    updateSummary();
}

RemovalPage::~RemovalPage()
{
    ++dateRequest;
    dateLoader.removeAllJobs (true, 10000);
}

void RemovalPage::loadContentDates (bool restart)
{
    if (! table.isColumnVisible (Column::contentCreated) || (datesPending > 0 && ! restart))
        return;

    const int request = ++dateRequest;
    dateLoader.removeAllJobs (false, 0);

    auto files = model.filesNeedingContentDate();
    datesPending = (int) files.size();
    updateSummary();

    if (files.empty())
        return;

    dateLoader.addJob ([owner = this, safeThis = juce::Component::SafePointer<RemovalPage> (this), request, files = std::move (files)]
    {
        const ScopedComInit com;
        std::vector<std::pair<juce::File, juce::Time>> batch;

        const auto flush = [&batch, safeThis, request]
        {
            auto sent = std::move (batch);
            batch.clear();

            juce::MessageManager::callAsync ([safeThis, request, sent]
            {
                if (safeThis == nullptr || safeThis->dateRequest != request)
                    return;

                safeThis->model.setContentDates (sent);
                safeThis->datesPending -= (int) sent.size();
                safeThis->table.refresh();
                safeThis->updateSummary();
            });
        };

        for (const auto& file : files)
        {
            if (owner->dateRequest != request)     // The destructor waits for this job, so the page is alive.
                return;

            batch.emplace_back (file, ContentDate::of (file));

            if (batch.size() >= 25)
                flush();
        }

        flush();
    });
}

void RemovalPage::layoutTableAndFooter (juce::Rectangle<int> area)
{
    auto footer = area.removeFromBottom (rowHeight + 4);

    deleteButton.setBounds (footer.removeFromRight (150));
    footer.removeFromRight (gap);

    trashButton.setBounds (footer.removeFromRight (140));
    footer.removeFromRight (gap);
    moveButton.setBounds (footer.removeFromRight (140));
    summary.setBounds (footer);
    area.removeFromBottom (gap);

    table.setBounds (area);
}

juce::String RemovalPage::describeChecked() const
{
    juce::int64 bytes = 0;
    const auto checked = Trash::withoutNested (model.getCheckedEntries());

    for (const auto& entry : checked)
        bytes += entry.size;

    return Format::count (checked.size(), "item") + " (" + Format::size (bytes) + ")";
}

juce::String RemovalPage::summaryText (size_t checkedCount, juce::int64 checkedBytes) const
{
    return model.hasResult()
             ? Format::count (model.getVisibleCount(), "item") + " listed, "
                 + juce::String ((int) checkedCount) + " checked (" + Format::size (checkedBytes) + ")"
             : juce::String ("Press Analyze to search for candidates.");
}

void RemovalPage::itemsGone (const juce::Array<juce::File>& files)
{
    model.remove (files);
    table.refresh();
    updateSummary();
}

void RemovalPage::confirm (juce::MessageBoxIconType icon, const juce::String& boxTitle, const juce::String& question,
                           const juce::String& okText, std::function<void()> action)
{
    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (icon)
                                      .withTitle (boxTitle)
                                      .withMessage (question)
                                      .withButton (okText)
                                      .withButton ("Cancel")
                                      .withAssociatedComponent (this),
                                  [safeThis = juce::Component::SafePointer<RemovalPage> (this), action = std::move (action)] (int button)
                                  {
                                      // With two buttons JUCE reports 1 for the first (OK) and 0 for the second (Cancel).
                                      constexpr int okResult = 1;

                                      if (button == okResult && safeThis != nullptr)
                                          action();
                                  });
}

void RemovalPage::finishRemoval (const RemovalReport& report, const juce::String& boxTitle, const juce::String& done)
{
    itemsGone (report.movedFiles);

    if (onItemSelected != nullptr)
        onItemSelected (nullptr);

    auto message = Format::count ((size_t) report.moved, "item") + " " + done;

    if (! report.failures.isEmpty())
    {
        message << "\n\n" << Format::count ((size_t) report.failures.size(), "item")
                << " could not be removed:\n" << ::wrappable (report.failures.joinIntoString ("\n"));

        if (const auto advice = failureAdvice(); advice.isNotEmpty())
            message << "\n\n" << advice;
    }

    juce::AlertWindow::showMessageBoxAsync (report.failures.isEmpty() ? juce::MessageBoxIconType::InfoIcon
                                                                      : juce::MessageBoxIconType::WarningIcon,
                                            boxTitle, message);
}

void RemovalPage::moveCheckedToTrash()
{
    // A warning alert: trashing is the more drastic action of the two.
    confirm (juce::MessageBoxIconType::WarningIcon, "Move to Trash",
             "Move " + describeChecked() + " to the Recycle Bin?", "Move to Trash",
             [this]
             {
                 const auto report = Trash::moveToTrash (itemsToRemove());
                 finishRemoval (report, "Move to Trash",
                                "moved to the Recycle Bin.\n" + Format::size (report.bytesMoved) + " freed.");
             });
}

void RemovalPage::deleteChecked()
{
    confirm (juce::MessageBoxIconType::WarningIcon, "Delete permanently",
             "Permanently delete " + describeChecked() + "?\n\nThey bypass the Recycle Bin and can't be restored.", "Delete permanently",
             [this]
             {
                 const auto report = Trash::deletePermanently (itemsToRemove());
                 finishRemoval (report, "Delete permanently",
                                "deleted permanently.\n" + Format::size (report.bytesMoved) + " freed.");
             });
}

void RemovalPage::moveCheckedToFolder()
{
    const juce::File last (settings.get ("moveTarget"));

    chooser = std::make_unique<juce::FileChooser> ("Move the checked items to...",
                                                    last.isDirectory() ? last : (getRootFolder != nullptr ? getRootFolder() : juce::File()));

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [safeThis = juce::Component::SafePointer<RemovalPage> (this)] (const juce::FileChooser& fc)
                          {
                              const auto destination = fc.getResult();

                              if (safeThis == nullptr || destination == juce::File())
                                  return;

                              // An info box rather than a warning: moving keeps the files, unlike the Recycle Bin.
                              safeThis->confirm (juce::MessageBoxIconType::InfoIcon, "Move to folder",
                                                 "Move " + safeThis->describeChecked() + " to\n" + wrappable (destination) + " ?",
                                                 "Move",
                                                 [safeThis, destination]
                                                 {
                                                     safeThis->settings.set ("moveTarget", destination.getFullPathName());

                                                     const auto report = MoveToFolder::run (safeThis->itemsToRemove(), destination);
                                                     safeThis->finishRemoval (report, "Move to folder",
                                                                              "moved to " + wrappable (destination) + ".\n"
                                                                                + Format::size (report.bytesMoved) + " moved.");
                                                 });
                          });
}

void RemovalPage::forgetMissing (const juce::Array<juce::File>& files)
{
    juce::Array<juce::File> missing;

    for (const auto& file : files)
        if (! file.exists())
            missing.add (file);

    if (missing.isEmpty())
        return;

    itemsGone (missing);

    if (onItemSelected != nullptr)
        onItemSelected (nullptr);
}

void RemovalPage::cycleChecks()
{
    checks = nextChecks (checks);

    model.applyChecks (checks);
    table.refresh();
    updateSummary();
}

void RemovalPage::updateSummary()
{
    checksChanged();

    const auto checked = Trash::withoutNested (model.getCheckedEntries());
    juce::int64 bytes = 0;

    for (const auto& entry : checked)
        bytes += entry.size;

    auto text = summaryText (checked.size(), bytes);

    if (datesPending > 0)
        text << "   (reading dates: " << datesPending << " left)";

    summary.setText (text, juce::dontSendNotification);

    for (auto* button : { &trashButton, &moveButton, &deleteButton })
        button->setEnabled (! checked.empty());

    // The check column header shows what the next click does.
    const auto next = nextChecks (checks);
    table.setHeaderTooltip (juce::String ("Click to cycle the check boxes. Next: ")
                              + (next == Checks::defaults ? "default checks" : (next == Checks::all ? "check all" : "uncheck all")));
}
