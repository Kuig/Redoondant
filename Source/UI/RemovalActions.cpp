#include "RemovalActions.h"
#include "../Core/Format.h"
#include "../Platform/ComInit.h"

/** Runs a removal action on a background thread behind a cancellable progress window.
    Deletes itself when done; the outcome is only reported if the page still exists.
*/
class RemovalActions::Job final : public juce::ThreadWithProgressWindow
{
public:
    Job (RemovalActions& actionsToReport, const juce::String& windowTitle, std::vector<FileEntry> checked,
         std::function<std::vector<FileEntry> (std::vector<FileEntry>)> expand, Action act,
         juce::String reportTitle, std::function<juce::String (const RemovalReport&)> describe)
        : ThreadWithProgressWindow (windowTitle, true, true, 10000, {}, &actionsToReport.owner),
          page (&actionsToReport.owner), actions (&actionsToReport),
          items (std::move (checked)), expander (std::move (expand)), action (std::move (act)),
          boxTitle (std::move (reportTitle)), done (std::move (describe))
    {
        setProgress (-1.0);
    }

    void run() override
    {
        // The action reports each item's failure itself; this only stops an exception from ending the program.
        try
        {
            const ScopedComInit com;

            if (expander != nullptr)
                items = expander (std::move (items));

            report = action (std::move (items), [this] (double fraction, const juce::String& item)
            {
                setProgress (fraction);
                setStatusMessage (item);
                return ! threadShouldExit();
            });
        }
        catch (const std::exception& e)     { report.failures.add (juce::String ("Unexpected error: ") + e.what()); }
        catch (...)                         { report.failures.add ("Unexpected error"); }
    }

    void threadComplete (bool) override
    {
        if (page != nullptr)        // The actions live as long as the page.
            actions->finish (report, boxTitle, done (report));

        delete this;
    }

private:
    juce::Component::SafePointer<juce::Component> page;
    RemovalActions* actions;
    std::vector<FileEntry> items;
    std::function<std::vector<FileEntry> (std::vector<FileEntry>)> expander;
    Action action;
    juce::String boxTitle;
    std::function<juce::String (const RemovalReport&)> done;
    RemovalReport report;
};

//==============================================================================
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

void RemovalActions::run (const juce::String& windowTitle, Action action, const juce::String& boxTitle,
                          std::function<juce::String (const RemovalReport&)> done)
{
    (new Job (*this, windowTitle, host.checkedEntries(), host.expander(), std::move (action), boxTitle, std::move (done)))->launchThread();
}

void RemovalActions::moveToTrash()
{
    // A warning alert: trashing is the more drastic action of the two.
    auto question = "Move " + host.describeChecked() + " to the Recycle Bin?";

    if (const auto without = Removal::countWithoutRecycleBin (host.checkedEntries()); without > 0)
        question << "\n\nLeft untouched, because their drive has no usable Recycle Bin (network, removable or turned off): "
                 << juce::String (without) << ".";

    question << groupWarning();

    confirm (owner, juce::MessageBoxIconType::WarningIcon, "Move to Trash", question, "Move to Trash",
             [this]
             {
                 run ("Moving to the Recycle Bin", [] (std::vector<FileEntry> items, const Removal::Progress& progress)
                      { return Removal::toTrash (std::move (items), progress); },
                      "Move to Trash",
                      [] (const RemovalReport& r) { return "moved to the Recycle Bin.\n" + Format::size (r.bytes) + " freed."; });
             });
}

void RemovalActions::deletePermanently()
{
    confirm (owner, juce::MessageBoxIconType::WarningIcon, "Delete permanently",
             "Permanently delete " + host.describeChecked() + "?\n\nThey bypass the Recycle Bin and can't be restored." + groupWarning(),
             "Delete permanently",
             [this]
             {
                 run ("Deleting", [] (std::vector<FileEntry> items, const Removal::Progress& progress)
                      { return Removal::deletePermanently (std::move (items), progress); },
                      "Delete permanently",
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

                              // An info box rather than a warning: moving keeps the files, unlike the Recycle Bin.
                              confirm (owner, juce::MessageBoxIconType::InfoIcon, "Move to folder",
                                       "Move " + host.describeChecked() + " to\n" + Format::wrappable (destination.getFullPathName()) + " ?",
                                       "Move",
                                       [this, destination]
                                       {
                                           settings.set ("moveTarget", destination.getFullPathName());

                                           run ("Moving to folder",
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
    host.itemsGone (report.files);

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
