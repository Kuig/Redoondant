#include "CriterionPage.h"
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
    : RemovalPage (s),
      criterion (c),
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

    groupOrderBox.addItemList (groupOrderNames(), 1);
    groupOrderBox.setSelectedItemIndex (juce::jlimit (0, groupOrderNames().size() - 1, settings.get ("groupOrder").getIntValue()), juce::dontSendNotification);
    groupOrderBox.setTooltip ("How the groups are ordered (the column headers sort the items inside each group)");
    groupOrderBox.onChange = [this]
    {
        settings.set ("groupOrder", groupOrderBox.getSelectedItemIndex());
        model.setGroupOrder ((GroupOrder) groupOrderBox.getSelectedItemIndex());
        table.refresh();
    };
    model.setGroupOrder ((GroupOrder) groupOrderBox.getSelectedItemIndex());
    addChildComponent (groupOrderBox);
    groupOrderBox.setVisible (info.grouped);

    analyseButton.onClick = [this] { analyse(); };
    resetButton.onClick = [this] { resetToDefaults(); };

    filterBar = std::make_unique<FilterBar> (settings.child ("filter"));
    filterBar->onChange = [this]
    {
        model.setFilter (filterBar->createFilter());
        table.refresh();
        updateSummary();
    };
    model.setFilter (filterBar->createFilter());
    addAndMakeVisible (*filterBar);

    for (auto* component : std::initializer_list<juce::Component*> { &title, &description, &parametersPanel, &recursiveToggle,
                                                                       &analyseButton, &resetButton, &status })
        addAndMakeVisible (component);
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
    if (groupOrderBox.isVisible())
        groupOrderBox.setBounds (actions.removeFromRight (190));

    status.setBounds (actions);
    area.removeFromTop (gap);

    filterBar->setBounds (area.removeFromTop (filterBar->getHeightForWidth (area.getWidth())));
    area.removeFromTop (gap);

    layoutTableAndFooter (area);
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
    loadContentDates (true);
}

void CriterionPage::resetToDefaults()
{
    parametersPanel.resetToDefaults();
    recursiveToggle.setToggleState (criterion.getInfo().recursiveByDefault, juce::sendNotification);
    resized();
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
