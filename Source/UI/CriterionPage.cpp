#include "CriterionPage.h"
#include "../Core/Format.h"
#include "../Core/Trash.h"
#include "../Platform/ComInit.h"

namespace
{
    constexpr int margin = 12;
    constexpr int rowHeight = 26;
    constexpr int gap = 8;
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

    recursiveToggle.setToggleState (settings.getBool ("recursive", false), juce::dontSendNotification);
    recursiveToggle.onClick = [this] { settings.set ("recursive", recursiveToggle.getToggleState()); };

    analyseButton.onClick = [this] { analyse(); };
    resetButton.onClick = [this] { resetToDefaults(); };
    trashButton.onClick = [this] { moveCheckedToTrash(); };

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
    table.onItemSelected = [this] (const FileEntry* entry)
    {
        if (onItemSelected != nullptr)
            onItemSelected (entry);
    };

    for (auto* component : std::initializer_list<juce::Component*> { &title, &description, &parametersPanel, &recursiveToggle,
                                                                       &analyseButton, &resetButton, &status, &table, &summary, &trashButton })
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
    table.refresh();
    status.setText ("Analyzed \"" + root.getFileName() + "\"" + (recursiveToggle.getToggleState() ? " recursively" : "")
                      + " at " + juce::Time::getCurrentTime().formatted ("%H:%M"),
                    juce::dontSendNotification);
    status.setTooltip (root.getFullPathName());
    updateSummary();
}

void CriterionPage::moveCheckedToTrash()
{
    const auto report = Trash::moveToTrash (model.getCheckedEntries());

    model.remove (report.trashed);
    table.refresh();
    updateSummary();

    if (onItemSelected != nullptr)
        onItemSelected (nullptr);

    auto message = Format::count ((size_t) report.moved, "item") + " moved to the Recycle Bin.\n"
                   + Format::size (report.bytesFreed) + " freed.";

    if (! report.failures.isEmpty())
        message << "\n\n" << Format::count ((size_t) report.failures.size(), "item")
                << " could not be moved:\n" << report.failures.joinIntoString ("\n");

    juce::AlertWindow::showMessageBoxAsync (report.failures.isEmpty() ? juce::MessageBoxIconType::InfoIcon
                                                                      : juce::MessageBoxIconType::WarningIcon,
                                            "Move to Trash", message);
}

void CriterionPage::resetToDefaults()
{
    parametersPanel.resetToDefaults();
    recursiveToggle.setToggleState (false, juce::sendNotification);
    resized();
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
}
