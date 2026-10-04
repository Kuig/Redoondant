#include "CriterionPage.h"
#include "ProgressTask.h"

CriterionPage::CriterionPage (const Criterion& c, SettingsScope s)
    : RemovalPage (s),
      criterion (c),
      parameters (criterion.createParameters()),
      parametersPanel (parameters, settings.child ("param"))
{
    const auto& info = criterion.getInfo();

    setHeading (info.name, info.description);

    recursiveToggle.setToggleState (settings.getBool ("recursive", info.recursiveByDefault), juce::dontSendNotification);
    recursiveToggle.onClick = [this]
    {
        settings.set ("recursive", recursiveToggle.getToggleState());
        updatePathFilter();
    };

    groupOrderBox.addItemList (groupOrderNames(), 1);
    groupOrderBox.setSelectedItemIndex (juce::jlimit (0, groupOrderNames().size() - 1, settings.getInt ("groupOrder")), juce::dontSendNotification);
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

    setupDefaultSelection (DefaultSelections::available (info.grouped, info.showsFolders), criterion.getDefaults().selection);

    if (settings.get ("table").isEmpty())       // Nothing remembered: start with the criterion's sort.
        applyDefaultSort();

    analyseButton.onClick = [this] { analyse(); };
    resetButton.onClick = [this] { resetToDefaults(); };

    filterBar = std::make_unique<FilterBar> (settings.child ("filter"));
    filterBar->onChange = [this] { applyFilter(); };
    addAndMakeVisible (*filterBar);
    updatePathFilter();

    for (auto* component : std::initializer_list<juce::Component*> { &parametersPanel, &recursiveToggle,
                                                                       &analyseButton, &resetButton, &status })
        addAndMakeVisible (component);
}

CriterionPage::~CriterionPage() = default;

void CriterionPage::resized()
{
    auto area = getLocalBounds().reduced (margin);
    layoutHeading (area);

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
    separatorY = area.getY();
    area.removeFromTop (gap);

    filterBar->setBounds (area.removeFromTop (filterBar->getHeightForWidth (area.getWidth())));
    area.removeFromTop (gap);

    layoutTableAndFooter (area);
}

void CriterionPage::paint (juce::Graphics& g)
{
    g.setColour (findColour (juce::ResizableWindow::backgroundColourId).contrasting (0.15f));
    g.fillRect (margin, separatorY, getWidth() - 2 * margin, 1);
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

    ScanContext settingsOfRun;
    settingsOfRun.root = root;
    settingsOfRun.recursive = recursiveToggle.getToggleState();

    // Snapshots: the UI may change the settings and the other pages during the analysis.
    auto peers = criterion.getInfo().usesPeers && getPeers != nullptr ? getPeers() : std::vector<PeerCriterion>();
    auto result = std::make_shared<AnalysisResult>();

    ProgressTask::run (*this, "Analyzing " + criterion.getInfo().name,
                       [&criterion = criterion, parameters = parameters, peers = std::move (peers), settingsOfRun, result] (const ScanContext& progress)
                       {
                           auto context = progress;
                           context.root = settingsOfRun.root;
                           context.recursive = settingsOfRun.recursive;
                           *result = criterion.analyse (context, parameters, peers);
                       },
                       [this, root, result] (bool cancelled, const juce::String& error)
                       {
                           if (error.isNotEmpty())
                               juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Analysis failed",
                                                                       "The analysis stopped because of an error: " + error);
                           else if (! cancelled)
                               showResult (std::move (*result), root);
                       });
}

void CriterionPage::showResult (AnalysisResult result, const juce::File& root)
{
    filterBar->setRoot (root);
    model.setFilter (filterBar->createFilter(), filterBar->usesContentDate());
    model.setResult (std::move (result), root, criterion.getInfo().grouped);
    checks = BulkCheck::defaults;
    table.refresh();
    status.setText ("Analyzed \"" + root.getFileName() + "\"" + (recursiveToggle.getToggleState() ? " recursively" : "")
                      + " at " + juce::Time::getCurrentTime().formatted ("%H:%M"),
                    juce::dontSendNotification);
    status.setTooltip (root.getFullPathName());
    updateSummary();
    dates.load (true);
}

void CriterionPage::applyFilter()
{
    model.setFilter (filterBar->createFilter(), filterBar->usesContentDate());
    table.refresh();
    updateSummary();
}

juce::String CriterionPage::progressNote() const
{
    return dates.pending() > 0 ? "reading dates: " + juce::String (dates.pending()) + " left" : juce::String();
}

void CriterionPage::updatePathFilter()
{
    if (filterBar == nullptr)
        return;

    filterBar->setRecursive (recursiveToggle.getToggleState());
    applyFilter();
    resized();
}

void CriterionPage::applyDefaultSort()
{
    const auto& defaults = criterion.getDefaults();

    if (defaults.sortColumn != Column::check)
        table.setSort (defaults.sortColumn, defaults.sortForwards);
}

void CriterionPage::resetToDefaults()
{
    parametersPanel.resetToDefaults();
    resetDefaultSelection();
    applyDefaultSort();
    recursiveToggle.setToggleState (criterion.getInfo().recursiveByDefault, juce::sendNotification);
    resized();
}

void CriterionPage::forgetResults()
{
    dates.cancel();
    discardResults();
    status.setText ({}, juce::dontSendNotification);
    status.setTooltip ({});
}

PeerCriterion CriterionPage::describeAsPeer() const
{
    PeerCriterion peer { &criterion, parameters, std::nullopt, {}, {}, model.getDefaultSelection() };

    if (model.hasResult())
    {
        peer.results = model.getAllEntries();
        peer.checked = model.getCheckedEntries();
        peer.resultsRoot = model.getRoot();
    }

    return peer;
}
