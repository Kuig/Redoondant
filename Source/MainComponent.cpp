#include "MainComponent.h"
#include "Criteria/AllCriteria.h"

namespace
{
    constexpr int folderBarHeight = 40;
    constexpr int toolEntryHeight = 34;
    const juce::String projectUrl ("https://github.com/Kuig/Redoondant");
    const juce::String cleanupId ("tool.cache");      // Stored as the "criterion" setting when the tool is the last page.

    juce::File defaultFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Downloads");
    }
}

/** Sidebar list showing the criteria names. */
class MainComponent::CriteriaListModel final : public juce::ListBoxModel
{
public:
    explicit CriteriaListModel (MainComponent& o) : owner (o) {}

    int getNumRows() override                       { return (int) owner.criteria.size(); }
    juce::String getNameForRow (int row) override   { return owner.criteria[(size_t) row]->getInfo().name; }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool isSelected) override
    {
        if (! juce::isPositiveAndBelow (row, getNumRows()))
            return;

        if (isSelected)
            g.fillAll (owner.findColour (juce::TextEditor::highlightColourId));

        g.setColour (owner.findColour (juce::ListBox::textColourId));
        g.setFont (juce::FontOptions (15.0f));
        g.drawText (getNameForRow (row), 12, 0, width - 16, height, juce::Justification::centredLeft, true);
    }

    void selectedRowsChanged (int lastRowSelected) override
    {
        owner.showPage (lastRowSelected);
    }

private:
    MainComponent& owner;
};

//==============================================================================
void MainComponent::ToolEntry::setSelected (bool shouldBeSelected)
{
    selected = shouldBeSelected;
    repaint();
}

void MainComponent::ToolEntry::paint (juce::Graphics& g)
{
    if (selected)
        g.fillAll (findColour (juce::TextEditor::highlightColourId));

    g.setColour (findColour (juce::ListBox::textColourId));
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.drawText ("Cache Cleaner", 12, 0, getWidth() - 16, getHeight(), juce::Justification::centredLeft, true);
}

MainComponent::Sidebar::Sidebar (juce::ListBox& criteria, ToolEntry& tool) : list (criteria), toolEntry (tool)
{
    addAndMakeVisible (list);
    addAndMakeVisible (toolEntry);
}

void MainComponent::Sidebar::paint (juce::Graphics& g)
{
    g.setColour (findColour (juce::ListBox::outlineColourId).withAlpha (0.6f));
    g.fillRect (0, getHeight() - toolEntryHeight - 1, getWidth(), 1);
}

void MainComponent::Sidebar::resized()
{
    auto area = getLocalBounds();
    toolEntry.setBounds (area.removeFromBottom (toolEntryHeight));
    area.removeFromBottom (1);
    list.setBounds (area);
}

//==============================================================================
MainComponent::MainComponent (Settings& appSettings)
    : appSettings (appSettings),
      settings (appSettings.root()),
      criteria (Criteria::createAll ([this] { return describePeers(); })),
      preview (settings.child ("preview"))
{
    for (const auto& criterion : criteria)
    {
        auto page = std::make_unique<CriterionPage> (*criterion, settings.child (criterion->getInfo().id));
        page->getRootFolder = [this] { return getRootFolder(); };
        page->onSelectionChanged = [this] (std::vector<FileEntry> entries) { preview.show (std::move (entries)); };
        addChildComponent (*page);
        pages.push_back (std::move (page));
    }

    cleanupPage = std::make_unique<CleanupPage> (settings.child ("cleanup"));
    cleanupPage->onSelectionChanged = [this] (std::vector<FileEntry> entries) { preview.show (std::move (entries)); };
    addChildComponent (*cleanupPage);
    cacheEntry.setTitle ("Cache Cleaner");
    cacheEntry.onClick = [this] { showCleanupPage(); };

    const juce::File savedFolder (settings.get ("folder"));
    setRootFolder (savedFolder.isDirectory() ? savedFolder : defaultFolder());
    folderEditor.onReturnKey = [this] { setRootFolder (juce::File (folderEditor.getText().trim())); };
    folderEditor.onFocusLost = folderEditor.onReturnKey;
    browseButton.onClick = [this] { browseForFolder(); };

    resetSettingsButton.setTooltip ("Delete the saved settings of Redoondant from this computer");
    resetSettingsButton.onClick = [this] { confirmResetSettings(); };

    helpButton.setTooltip (juce::String (ProjectInfo::projectName) + " " + ProjectInfo::versionString + " - open the project page on GitHub");
    helpButton.onClick = [] { juce::URL (projectUrl).launchInDefaultBrowser(); };

    criteriaModel = std::make_unique<CriteriaListModel> (*this);
    criteriaList.setModel (criteriaModel.get());
    criteriaList.setRowHeight (30);

    layout.setItemLayout (0, 150, 400, settings.getDouble ("layout.left", 240));
    layout.setItemLayout (1, 5, 5, 5);
    layout.setItemLayout (2, 400, -1.0, -0.62);
    layout.setItemLayout (3, 5, 5, 5);
    layout.setItemLayout (4, 200, 900, settings.getDouble ("layout.right", -0.4));

    leftBar.onMoved = rightBar.onMoved = [this]
    {
        settings.set ("layout.left", sidebar.getWidth());
        settings.set ("layout.right", preview.getWidth());
    };

    for (auto* c : std::initializer_list<juce::Component*> { &folderLabel, &folderEditor, &browseButton, &resetSettingsButton, &helpButton, &sidebar,
                                                               &leftBar, &rightBar, &preview })
        addAndMakeVisible (c);

    int selected = 0;

    for (size_t i = 0; i < criteria.size(); ++i)
        if (criteria[i]->getInfo().id == settings.get ("criterion"))
            selected = (int) i;

    if (settings.get ("criterion") == cleanupId)
        showCleanupPage();
    else
        criteriaList.selectRow (selected);

    setSize (1400, 820);
}

MainComponent::~MainComponent()
{
    criteriaList.setModel (nullptr);
}

std::vector<PeerCriterion> MainComponent::describePeers() const
{
    std::vector<PeerCriterion> peers;

    for (const auto& page : pages)
        peers.push_back (page->describeAsPeer());

    return peers;
}

juce::File MainComponent::getRootFolder() const
{
    return juce::File (folderEditor.getText().trim());
}

void MainComponent::setRootFolder (const juce::File& folder)
{
    folderEditor.setText (folder.getFullPathName(), false);

    if (folder.isDirectory())
        settings.set ("folder", folder.getFullPathName());
}

void MainComponent::confirmResetSettings()
{
    const auto options = juce::MessageBoxOptions()
                             .withIconType (juce::MessageBoxIconType::QuestionIcon)
                             .withTitle ("Reset settings")
                             .withMessage ("Delete the saved settings (folder, filters, column layout, window position, "
                                           "the Cache Cleaner list...)?\n\n" + appSettings.file().getFullPathName()
                                           + "\n\nRedoondant will close and start from the defaults next time. Your files are not touched.")
                             .withButton ("Delete settings and close")
                             .withButton ("Cancel");

    juce::AlertWindow::showAsync (options, [this] (int result)
    {
        if (result == 1)
        {
            appSettings.discardAll();
            juce::JUCEApplication::getInstance()->quit();
        }
    });
}

void MainComponent::browseForFolder()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose the folder to analyze", getRootFolder());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc)
                          {
                              if (fc.getResult() != juce::File())
                                  setRootFolder (fc.getResult());
                          });
}

void MainComponent::showPage (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) pages.size()))
        return;

    settings.set ("criterion", criteria[(size_t) index]->getInfo().id);
    cacheEntry.setSelected (false);
    setCurrentPage (pages[(size_t) index].get());
}

void MainComponent::showCleanupPage()
{
    settings.set ("criterion", cleanupId);
    criteriaList.deselectAllRows();
    cacheEntry.setSelected (true);
    setCurrentPage (cleanupPage.get());
}

void MainComponent::setCurrentPage (juce::Component* page)
{
    for (auto& p : pages)
        p->setVisible (p.get() == page);

    cleanupPage->setVisible (cleanupPage.get() == page);
    currentPage = page;
    preview.show ({});
    resized();
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void MainComponent::resized()
{
    auto area = getLocalBounds();

    auto folderBar = area.removeFromTop (folderBarHeight).reduced (10, 7);
    folderLabel.setBounds (folderBar.removeFromLeft (60));
    helpButton.setBounds (folderBar.removeFromRight (28));
    folderBar.removeFromRight (8);
    resetSettingsButton.setBounds (folderBar.removeFromRight (120));
    folderBar.removeFromRight (8);
    browseButton.setBounds (folderBar.removeFromRight (100));
    folderBar.removeFromRight (8);
    folderEditor.setBounds (folderBar);

    juce::Component* columns[] = { &sidebar, &leftBar, currentPage, &rightBar, &preview };
    layout.layOutComponents (columns, 5, area.getX(), area.getY(), area.getWidth(), area.getHeight(), false, true);
}
