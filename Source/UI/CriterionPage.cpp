#include "CriterionPage.h"
#include "../Core/Format.h"
#include "../Core/MoveToFolder.h"
#include "../Core/Trash.h"
#include "../Platform/ComInit.h"
#include "../Platform/ShellContextMenu.h"

namespace
{
    constexpr int margin = 12;
    constexpr int rowHeight = 26;
    constexpr int gap = 8;

    /** Text with paths that message boxes can wrap: invisible break opportunities (zero-width spaces) after each separator. */
    juce::String wrappable (const juce::String& text)
    {
        const juce::String zeroWidthSpace (juce::CharPointer_UTF8 ("\xe2\x80\x8b"));
        return text.replace ("\\", "\\" + zeroWidthSpace).replace ("/", "/" + zeroWidthSpace);
    }

    juce::String wrappable (const juce::File& path)     { return wrappable (path.getFullPathName()); }
}

/** Runs an analysis on a background thread behind a cancellable progress window.
    Deletes itself when done; the callback is only invoked if the page still exists.
*/
class CriterionPage::AnalysisJob final : public juce::ThreadWithProgressWindow
{
public:
    AnalysisJob (CriterionPage& page, const juce::File& root)
        : ThreadWithProgressWindow ("Analyzing " + page.criterion.getInfo().name, true, true, 10000, {}, &page),
          owner (&page), criterion (page.criterion), parameters (page.parameters)
    {
        context.root = root;
        context.recursive = page.recursiveToggle.getToggleState();
        context.shouldStop = [this] { return threadShouldExit(); };
        context.onProgress = [this] (double value, const juce::String& text)
        {
            setProgress (value);
            setStatusMessage (text);
        };
        setProgress (-1.0);
    }

    void run() override
    {
        const ScopedComInit com;    // Some criteria read Windows metadata.
        result = criterion.analyse (context, parameters);
    }

    void threadComplete (bool cancelled) override
    {
        if (owner != nullptr && ! cancelled)
            owner->showResult (std::move (result), context.root);

        delete this;
    }

private:
    juce::Component::SafePointer<CriterionPage> owner;
    const Criterion& criterion;
    const ParameterSet parameters;      // Snapshot: the UI may not change it during the analysis.
    ScanContext context;
    AnalysisResult result;
};

//==============================================================================
CriterionPage::CriterionPage (const Criterion& c, SettingsScope s)
    : criterion (c),
      settings (s),
      parameters (criterion.createParameters()),
      parametersPanel (parameters, settings.child ("param"))
{
    const auto& info = criterion.getInfo();

    title.setText (info.name, juce::dontSendNotification);
    title.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    description.setText (info.description, juce::dontSendNotification);
    description.setJustificationType (juce::Justification::topLeft);
    description.setMinimumHorizontalScale (1.0f);

    recursiveToggle.setToggleState (settings.getBool ("recursive", info.recursiveByDefault), juce::dontSendNotification);
    recursiveToggle.onClick = [this] { settings.set ("recursive", recursiveToggle.getToggleState()); };

    analyseButton.onClick = [this] { analyse(); };
    resetButton.onClick = [this] { resetToDefaults(); };
    trashButton.onClick = [this] { moveCheckedToTrash(); };
    moveButton.onClick = [this] { moveCheckedToFolder(); };
    checksButton.onClick = [this] { cycleChecks(); };

    if (info.filterable)
    {
        filterBar = std::make_unique<FilterBar> (settings.child ("filter"));
        filterBar->onChange = [this]
        {
            model.setFilter (filterBar->createFilter());
            table.refresh();
            updateSummary();
        };
        model.setFilter (filterBar->createFilter());
        addAndMakeVisible (*filterBar);
    }

    table.restoreLayoutState (settings.get ("table"));
    table.onLayoutChanged = [this] { settings.set ("table", table.getLayoutState()); };
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

    for (auto* component : std::initializer_list<juce::Component*> { &title, &description, &parametersPanel, &recursiveToggle,
                                                                       &analyseButton, &resetButton, &status, &table, &summary, &checksButton, &moveButton, &trashButton })
        addAndMakeVisible (component);

    updateSummary();
}

CriterionPage::~CriterionPage() = default;

void CriterionPage::resized()
{
    auto area = getLocalBounds().reduced (margin);

    title.setBounds (area.removeFromTop (28));
    description.setBounds (area.removeFromTop (36));
    area.removeFromTop (gap);

    if (const int h = parametersPanel.getHeightForWidth (area.getWidth()); h > 0)
    {
        parametersPanel.setBounds (area.removeFromTop (h));
        area.removeFromTop (gap);
    }

    auto actions = area.removeFromTop (rowHeight);
    recursiveToggle.setBounds (actions.removeFromLeft (230));
    analyseButton.setBounds (actions.removeFromLeft (100));
    actions.removeFromLeft (gap);
    resetButton.setBounds (actions.removeFromLeft (130));
    actions.removeFromLeft (gap);
    status.setBounds (actions);
    area.removeFromTop (gap);

    if (filterBar != nullptr)
    {
        filterBar->setBounds (area.removeFromTop (filterBar->getHeightForWidth (area.getWidth())));
        area.removeFromTop (gap);
    }

    auto footer = area.removeFromBottom (rowHeight + 4);
    trashButton.setBounds (footer.removeFromRight (140));
    footer.removeFromRight (gap);
    moveButton.setBounds (footer.removeFromRight (140));
    checksButton.setBounds (footer.removeFromLeft (130));
    footer.removeFromLeft (gap);
    summary.setBounds (footer);
    area.removeFromBottom (gap);

    table.setBounds (area);
}

void CriterionPage::analyse()
{
    const auto root = getRootFolder != nullptr ? getRootFolder() : juce::File();

    if (! root.isDirectory())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Folder not found",
                                                "Please choose an existing folder to analyze.");
        return;
    }

    (new AnalysisJob (*this, root))->launchThread();
}

void CriterionPage::showResult (AnalysisResult result, const juce::File& root)
{
    model.setResult (std::move (result), root, criterion.getInfo().grouped);
    checks = ResultsModel::Checks::defaults;
    table.refresh();
    status.setText ("Analyzed \"" + root.getFileName() + "\"" + (recursiveToggle.getToggleState() ? " recursively" : "")
                      + " at " + juce::Time::getCurrentTime().formatted ("%H:%M"),
                    juce::dontSendNotification);
    status.setTooltip (root.getFullPathName());
    updateSummary();
}

juce::String CriterionPage::describeChecked() const
{
    juce::int64 bytes = 0;
    const auto checked = Trash::withoutNested (model.getCheckedEntries());

    for (const auto& entry : checked)
        bytes += entry.size;

    return Format::count (checked.size(), "item") + " (" + Format::size (bytes) + ")";
}

void CriterionPage::confirm (juce::MessageBoxIconType icon, const juce::String& boxTitle, const juce::String& question,
                             const juce::String& okText, std::function<void()> action)
{
    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (icon)
                                      .withTitle (boxTitle)
                                      .withMessage (question)
                                      .withButton (okText)
                                      .withButton ("Cancel")
                                      .withAssociatedComponent (this),
                                  [safeThis = juce::Component::SafePointer<CriterionPage> (this), action = std::move (action)] (int button)
                                  {
                                      // With two buttons JUCE reports 1 for the first (OK) and 0 for the second (Cancel).
                                      constexpr int okResult = 1;

                                      if (button == okResult && safeThis != nullptr)
                                          action();
                                  });
}

void CriterionPage::finishRemoval (const RemovalReport& report, const juce::String& boxTitle, const juce::String& done)
{
    model.remove (report.movedFiles);
    table.refresh();
    updateSummary();

    if (onItemSelected != nullptr)
        onItemSelected (nullptr);

    auto message = Format::count ((size_t) report.moved, "item") + " " + done;

    if (! report.failures.isEmpty())
        message << "\n\n" << Format::count ((size_t) report.failures.size(), "item")
                << " could not be moved:\n" << wrappable (report.failures.joinIntoString ("\n"));

    juce::AlertWindow::showMessageBoxAsync (report.failures.isEmpty() ? juce::MessageBoxIconType::InfoIcon
                                                                      : juce::MessageBoxIconType::WarningIcon,
                                            boxTitle, message);
}

void CriterionPage::moveCheckedToTrash()
{
    // A warning alert: trashing is the more drastic action of the two.
    confirm (juce::MessageBoxIconType::WarningIcon, "Move to Trash",
             "Move " + describeChecked() + " to the Recycle Bin?", "Move to Trash",
             [this]
             {
                 const auto report = Trash::moveToTrash (model.getCheckedEntries());
                 finishRemoval (report, "Move to Trash",
                                "moved to the Recycle Bin.\n" + Format::size (report.bytesMoved) + " freed.");
             });
}

void CriterionPage::moveCheckedToFolder()
{
    const juce::File last (settings.get ("moveTarget"));

    chooser = std::make_unique<juce::FileChooser> ("Move the checked items to...",
                                                    last.isDirectory() ? last : (getRootFolder != nullptr ? getRootFolder() : juce::File()));

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [safeThis = juce::Component::SafePointer<CriterionPage> (this)] (const juce::FileChooser& fc)
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

                                                     const auto report = MoveToFolder::run (safeThis->model.getCheckedEntries(), destination);
                                                     safeThis->finishRemoval (report, "Move to folder",
                                                                              "moved to " + wrappable (destination) + ".\n"
                                                                                + Format::size (report.bytesMoved) + " moved.");
                                                 });
                          });
}

void CriterionPage::resetToDefaults()
{
    parametersPanel.resetToDefaults();
    recursiveToggle.setToggleState (criterion.getInfo().recursiveByDefault, juce::sendNotification);
    resized();
}

void CriterionPage::forgetMissing (const juce::Array<juce::File>& files)
{
    juce::Array<juce::File> missing;

    for (const auto& file : files)
        if (! file.exists())
            missing.add (file);

    if (missing.isEmpty())
        return;

    model.remove (missing);
    table.refresh();
    updateSummary();

    if (onItemSelected != nullptr)
        onItemSelected (nullptr);
}

void CriterionPage::cycleChecks()
{
    using Checks = ResultsModel::Checks;
    checks = checks == Checks::defaults ? Checks::all : (checks == Checks::all ? Checks::none : Checks::defaults);

    model.applyChecks (checks);
    table.refresh();
    updateSummary();
}

PeerCriterion CriterionPage::describeAsPeer() const
{
    PeerCriterion peer { &criterion, parameters, std::nullopt, {} };

    if (model.hasResult())
    {
        peer.results = model.getAllEntries();
        peer.resultsRoot = model.getRoot();
    }

    return peer;
}

void CriterionPage::updateSummary()
{
    const auto checked = Trash::withoutNested (model.getCheckedEntries());
    juce::int64 bytes = 0;

    for (const auto& entry : checked)
        bytes += entry.size;

    summary.setText (model.hasResult()
                       ? Format::count (model.getVisibleCount(), "item") + " listed, "
                           + juce::String ((int) checked.size()) + " checked (" + Format::size (bytes) + ")"
                       : juce::String ("Press Analyze to search for candidates."),
                     juce::dontSendNotification);

    trashButton.setEnabled (! checked.empty());
    moveButton.setEnabled (! checked.empty());

    // The button shows what the next click does.
    using Checks = ResultsModel::Checks;
    checksButton.setButtonText (checks == Checks::defaults ? "Check all" : (checks == Checks::all ? "Uncheck all" : "Default checks"));
    checksButton.setEnabled (model.getVisibleCount() > 0);
}
