#include "MainComponent.h"
#include "Criteria/AllCriteria.h"

namespace
{
    constexpr int folderBarHeight = 40;

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
MainComponent::MainComponent (Settings& appSettings)
    : settings (appSettings.root()),
      criteria (Criteria::createAll()),
      preview (settings.child ("preview"))
{
    for (const auto& criterion : criteria)
    {
        auto page = std::make_unique<CriterionPage> (*criterion, settings.child (criterion->getInfo().id));
        page->getRootFolder = [this] { return getRootFolder(); };
        page->onItemSelected = [this] (const FileEntry* entry) { preview.show (entry); };
        addChildComponent (*page);
        pages.push_back (std::move (page));
    }

    const juce::File savedFolder (settings.get ("folder"));
    setRootFolder (savedFolder.isDirectory() ? savedFolder : defaultFolder());
    folderEditor.onReturnKey = [this] { setRootFolder (juce::File (folderEditor.getText().trim())); };
    folderEditor.onFocusLost = folderEditor.onReturnKey;
    browseButton.onClick = [this] { browseForFolder(); };

    criteriaModel = std::make_unique<CriteriaListModel> (*this);
    criteriaList.setModel (criteriaModel.get());
    criteriaList.setRowHeight (30);

    layout.setItemLayout (0, 150, 400, settings.getDouble ("layout.left", 250));
    layout.setItemLayout (1, 5, 5, 5);
    layout.setItemLayout (2, 400, -1.0, -0.62);
    layout.setItemLayout (3, 5, 5, 5);
    layout.setItemLayout (4, 200, 900, settings.getDouble ("layout.right", 360));

    leftBar.onMoved = rightBar.onMoved = [this]
    {
        settings.set ("layout.left", criteriaList.getWidth());
        settings.set ("layout.right", preview.getWidth());
    };

    for (auto* c : std::initializer_list<juce::Component*> { &folderLabel, &folderEditor, &browseButton, &criteriaList,
                                                               &leftBar, &rightBar, &preview })
        addAndMakeVisible (c);

    int selected = 0;

    for (size_t i = 0; i < criteria.size(); ++i)
        if (criteria[i]->getInfo().id == settings.get ("criterion"))
            selected = (int) i;

    criteriaList.selectRow (selected);
    setSize (1400, 820);
}

MainComponent::~MainComponent()
{
    criteriaList.setModel (nullptr);
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

    for (size_t i = 0; i < pages.size(); ++i)
        pages[i]->setVisible ((int) i == index);

    currentPage = pages[(size_t) index].get();
    settings.set ("criterion", criteria[(size_t) index]->getInfo().id);
    preview.show (nullptr);
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
    browseButton.setBounds (folderBar.removeFromRight (100));
    folderBar.removeFromRight (8);
    folderEditor.setBounds (folderBar);

    juce::Component* columns[] = { &criteriaList, &leftBar, currentPage, &rightBar, &preview };
    layout.layOutComponents (columns, 5, area.getX(), area.getY(), area.getWidth(), area.getHeight(), false, true);
}
