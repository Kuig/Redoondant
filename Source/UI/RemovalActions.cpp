#include "RemovalActions.h"
#include "../Core/Format.h"
#include "ProgressTask.h"

RemovalActions::RemovalActions (juce::Component& ownerPage, Host& hostPage, SettingsScope s)
    : owner (ownerPage), host (hostPage), settings (s)
{
}

void RemovalActions::confirm (juce::Component& page, juce::MessageBoxIconType icon, const juce::String& boxTitle,
                              const juce::String& question, const juce::String& okText, std::function<void()> action)
{
    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (icon)
                                      .withTitle (boxTitle)
                                      .withMessage (question)
                                      .withButton (okText)
                                      .withButton ("Cancel")
                                      .withAssociatedComponent (&page),
                                  [safePage = juce::Component::SafePointer<juce::Component> (&page), action = std::move (action)] (int button)
                                  {
                                      // With two buttons JUCE reports 1 for the first (OK) and 0 for the second (Cancel).
                                      constexpr int okResult = 1;

                                      if (button == okResult && safePage != nullptr)
                                          action();
                                  });
}

juce::String RemovalActions::groupWarning() const
{
    const auto groups = host.groupsWithoutKeptItem();

    return groups > 0 ? "\n\nCareful: " + Format::count (groups, "group") + " would have every item removed, so nothing of "
                          + (groups == 1 ? "it" : "them") + " would be kept."
                      : juce::String();
}

void RemovalActions::run (std::vector<FileEntry> items, const juce::String& windowTitle, Action action, const juce::String& boxTitle,
                          std::function<juce::String (const RemovalReport&)> done)
{
    auto report = std::make_shared<RemovalReport>();

    ProgressTask::run (owner, windowTitle,
                       [report, items = std::move (items), expand = host.removalExpander(), action = std::move (action)] (const ScanContext& context) mutable
                       {
                           if (expand != nullptr)
                               items = expand (std::move (items));

                           *report = action (std::move (items), [&context] (double fraction, const juce::String& item)
                           {
                               context.progress (fraction, item);
                               return ! context.isCancelled();
                           });
                       },
                       [this, report, boxTitle, done = std::move (done)] (bool, const juce::String& error)
                       {
                           // The action reports each item's failure itself; an error here stopped it altogether.
                           if (error.isNotEmpty())
                               report->failures.add ("Unexpected error: " + error);

                           finish (*report, boxTitle, done (*report));     // The actions live as long as the page.
                       });
}

void RemovalActions::moveToTrash()
{
    // What the question describes is what the action gets, even if the list changes while the question is open
    // (content dates arriving can change the sort, and with it the item each group keeps).
    auto checked = host.checkedEntries();

    // A warning alert: trashing is the more drastic action of the two.
    auto question = "Move " + host.describe (checked) + " to the Recycle Bin?";

    if (const auto without = Removal::countWithoutRecycleBin (checked); without > 0)
        question << "\n\nLeft untouched, because their drive has no usable Recycle Bin (network, removable or turned off): "
                 << juce::String (without) << ".";

    question << groupWarning();

    confirm (owner, juce::MessageBoxIconType::WarningIcon, "Move to Trash", question, "Move to Trash",
             [this, checked = std::move (checked)]
             {
                 run (checked, "Moving to the Recycle Bin", Removal::toTrash, "Move to Trash",
                      [] (const RemovalReport& r) { return "moved to the Recycle Bin.\n" + Format::size (r.bytes) + " freed."; });
             });
}

void RemovalActions::deletePermanently()
{
    auto checked = host.checkedEntries();

    confirm (owner, juce::MessageBoxIconType::WarningIcon, "Delete permanently",
             "Permanently delete " + host.describe (checked) + "?\n\nThey bypass the Recycle Bin and can't be restored." + groupWarning(),
             "Delete permanently",
             [this, checked = std::move (checked)]
             {
                 run (checked, "Deleting", Removal::deletePermanently, "Delete permanently",
                      [] (const RemovalReport& r) { return "deleted permanently.\n" + Format::size (r.bytes) + " freed."; });
             });
}

void RemovalActions::moveToFolder()
{
    const juce::File last (settings.get ("moveTarget"));

    chooser = std::make_unique<juce::FileChooser> ("Move the checked items to...", last.isDirectory() ? last : host.startFolder());

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this, safePage = juce::Component::SafePointer<juce::Component> (&owner)] (const juce::FileChooser& fc)
                          {
                              const auto destination = fc.getResult();

                              if (safePage == nullptr || destination == juce::File())
                                  return;

                              auto checked = host.checkedEntries();

                              // An info box rather than a warning: moving keeps the files, unlike the Recycle Bin.
                              confirm (owner, juce::MessageBoxIconType::InfoIcon, "Move to folder",
                                       "Move " + host.describe (checked) + " to\n" + Format::wrappable (destination.getFullPathName()) + " ?",
                                       "Move",
                                       [this, destination, checked = std::move (checked)]
                                       {
                                           settings.set ("moveTarget", destination.getFullPathName());

                                           run (checked, "Moving to folder",
                                                [destination] (std::vector<FileEntry> items, const Removal::Progress& progress)
                                                { return Removal::toFolder (std::move (items), destination, progress); },
                                                "Move to folder",
                                                [destination] (const RemovalReport& r)
                                                {
                                                    return "moved to " + Format::wrappable (destination.getFullPathName()) + ".\n"
                                                           + Format::size (r.bytes) + " moved.";
                                                });
                                       });
                          });
}

void RemovalActions::finish (const RemovalReport& report, const juce::String& boxTitle, const juce::String& done)
{
    host.removedByAction (report.files);

    auto message = Format::count (report.processed, "item") + " " + done;

    if (report.alreadyGone > 0)
        message << "\n" << Format::count (report.alreadyGone, "item") << " didn't exist any more.";

    if (report.cancelled)
        message << "\n\nStopped before the end: the remaining items were left untouched.";

    if (! report.failures.isEmpty())
    {
        message << "\n\n" << Format::count (report.failures.size(), "item")
                << " could not be removed:\n" << Format::wrappable (report.failures.joinIntoString ("\n"));

        if (const auto advice = host.failureAdvice(); advice.isNotEmpty())
            message << "\n\n" << advice;
    }

    if (! report.leftBehind.isEmpty())
        message << "\n\n" << Format::count (report.leftBehind.size(), "item")
                << " copied to the destination, but the original could not be removed completely (a file may be in use). "
                   "Both copies exist:\n" << Format::wrappable (report.leftBehind.joinIntoString ("\n"));

    juce::AlertWindow::showMessageBoxAsync (report.failures.isEmpty() && report.leftBehind.isEmpty() ? juce::MessageBoxIconType::InfoIcon
                                                                                                     : juce::MessageBoxIconType::WarningIcon,
                                            boxTitle, message);
}
