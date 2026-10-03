#pragma once

#include "../Core/Removal.h"
#include "../Core/Settings.h"

/** The actions of a RemovalPage on its checked items (Move to Trash, Move to folder, Delete permanently):
    the confirmation, the run on a worker thread behind a progress window with Cancel, and the report.
*/
class RemovalActions
{
public:
    /** What the actions need to know about the page that owns them. */
    class Host
    {
    public:
        virtual ~Host() = default;

        virtual std::vector<FileEntry> checkedEntries() const = 0;

        /** Turns the checked entries into the items to act on, e.g. a folder into its content. Runs on the
            worker thread, so it must be self-contained (not capture the page); empty = use the entries as they are.
        */
        virtual std::function<std::vector<FileEntry> (std::vector<FileEntry>)> expander() const = 0;

        /** "12 items (340 MB)": what the check boxes currently select. */
        virtual juce::String describeChecked() const = 0;

        /** Groups in which every item is checked, so nothing of them would be kept (0 for pages without groups). */
        virtual int groupsWithoutKeptItem() const = 0;

        /** Added to the report when some items could not be moved. */
        virtual juce::String failureAdvice() const = 0;

        /** The files are no longer where they were (moved, deleted): update the list and the preview. */
        virtual void itemsGone (const juce::Array<juce::File>& files) = 0;

        /** Where the "Move to folder" chooser starts when no folder was used before. */
        virtual juce::File startFolder() const = 0;
    };

    /** @param owner   the page: dialogs are attached to it and nothing happens after it is gone. */
    RemovalActions (juce::Component& owner, Host& host, SettingsScope settings);

    void moveToTrash();
    void moveToFolder();
    void deletePermanently();

    /** Asks the user before running an action; `action` runs only on OK, and only if `owner` still exists. */
    static void confirm (juce::Component& owner, juce::MessageBoxIconType icon, const juce::String& boxTitle,
                         const juce::String& question, const juce::String& okText, std::function<void()> action);

private:
    class Job;
    using Action = std::function<RemovalReport (std::vector<FileEntry>, const Removal::Progress&)>;

    juce::Component& owner;
    Host& host;
    SettingsScope settings;
    std::unique_ptr<juce::FileChooser> chooser;

    /** Runs an action on the checked items on a worker thread behind a progress window (with Cancel),
        then reports the outcome. `done` describes what was achieved, e.g. "moved to the Recycle Bin.".
    */
    void run (const juce::String& windowTitle, Action action, const juce::String& boxTitle,
              std::function<juce::String (const RemovalReport&)> done);

    void finish (const RemovalReport& report, const juce::String& boxTitle, const juce::String& done);

    /** "Careful: N groups..." when checking everything would leave no copy of some group, else empty. */
    juce::String groupWarning() const;

    JUCE_DECLARE_NON_COPYABLE (RemovalActions)
};
