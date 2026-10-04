#include "RemovalPage.h"
#include "../Core/FileScanner.h"
#include "../Core/Format.h"
#include "../Core/Removal.h"
#include "../Platform/ShellContextMenu.h"

RemovalPage::RemovalPage (SettingsScope s)
    : settings (s)
{
    trashButton.onClick = [this] { removal.moveToTrash(); };
    moveButton.onClick = [this] { removal.moveToFolder(); };
    deleteButton.onClick = [this] { removal.deletePermanently(); };
    table.onHeaderCheckClicked = [this] { cycleChecks(); };

    table.restoreLayoutState (settings.get ("table"));
    table.onLayoutChanged = [this]
    {
        settings.set ("table", table.getLayoutState());
        tableLayoutChanged();
    };
    table.onCheckedChanged = [this] { updateSummary(); };
    table.onContextMenu = [this] (const juce::Array<juce::File>& files, juce::Point<int> position)
    {
        if (ShellContextMenu::show (files, position, *this))
            forgetMissing (files);
    };
    table.onSelectionChanged = [this] (std::vector<FileEntry> entries) { notifySelection (std::move (entries)); };

    title.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    description.setJustificationType (juce::Justification::topLeft);
    description.setMinimumHorizontalScale (1.0f);

    for (auto* component : std::initializer_list<juce::Component*> { &title, &description, &table, &summary, &moveButton, &trashButton, &deleteButton })
        addAndMakeVisible (component);

    // Red: unlike the other actions, this one can't be undone.
    deleteButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xffb3261e));
    deleteButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    deleteButton.setColour (juce::TextButton::textColourOnId, juce::Colours::white);

    updateSummary();
}

RemovalPage::~RemovalPage() = default;

void RemovalPage::notifySelection (std::vector<FileEntry> entries)
{
    if (onSelectionChanged != nullptr)
        onSelectionChanged (std::move (entries));
}

void RemovalPage::setupDefaultSelection (std::vector<DefaultSelection> available, DefaultSelection initial)
{
    initialSelection = initial;
    selectionBox.clear (juce::dontSendNotification);

    for (const auto s : available)
        selectionBox.addItem (DefaultSelections::nameOf (s), (int) s + 1);

    auto selection = initial;

    if (settings.has ("defaultSelection") && selectionBox.indexOfItemId (settings.getInt ("defaultSelection") + 1) >= 0)
        selection = (DefaultSelection) settings.getInt ("defaultSelection");

    selectionBox.setTooltip ("Which items are checked by default (the first state of the check cycle in the table header)");
    selectionBox.onChange = [this] { selectDefault ((DefaultSelection) (selectionBox.getSelectedId() - 1)); };
    selectionBox.setSelectedId ((int) selection + 1, juce::dontSendNotification);
    model.setDefaultSelection (selection);
    addAndMakeVisible (selectionBox);
}

void RemovalPage::resetDefaultSelection()
{
    selectionBox.setSelectedId ((int) initialSelection + 1, juce::dontSendNotification);
    selectDefault (initialSelection);
}

void RemovalPage::selectDefault (DefaultSelection selection)
{
    settings.set ("defaultSelection", (int) selection);
    model.setDefaultSelection (selection);
    table.refresh();
    updateSummary();
}

void RemovalPage::discardResults()
{
    model.clear();
    checks = BulkCheck::defaults;
    table.refresh();
    updateSummary();
    notifySelection ({});
}

void RemovalPage::setHeading (const juce::String& titleText, const juce::String& descriptionText)
{
    title.setText (titleText, juce::dontSendNotification);
    description.setText (descriptionText, juce::dontSendNotification);
}

void RemovalPage::layoutHeading (juce::Rectangle<int>& area)
{
    title.setBounds (area.removeFromTop (28));
    description.setBounds (area.removeFromTop (36));
    area.removeFromTop (gap);
}

void RemovalPage::layoutTableAndFooter (juce::Rectangle<int> area)
{
    auto footer = area.removeFromBottom (rowHeight + 4);

    deleteButton.setBounds (footer.removeFromRight (150));
    footer.removeFromRight (gap);

    trashButton.setBounds (footer.removeFromRight (140));
    footer.removeFromRight (gap);
    moveButton.setBounds (footer.removeFromRight (140));

    if (selectionBox.isVisible())
    {
        selectionBox.setBounds (footer.removeFromLeft (250));
        footer.removeFromLeft (gap);
    }

    summary.setBounds (footer);
    area.removeFromBottom (gap);

    table.setBounds (area);
}

juce::String RemovalPage::describe (const std::vector<FileEntry>& checked) const
{
    const auto items = Removal::withoutNested (checked);       // As the action counts them: an item inside a checked folder goes with it.
    return Format::count (items.size(), itemNoun()) + " (" + Format::size (totalSize (items)) + ")";
}

void RemovalPage::itemsGone (const juce::Array<juce::File>& files)
{
    model.remove (files);
    table.refresh();
    updateSummary();
}

void RemovalPage::removedByAction (const juce::Array<juce::File>& files)
{
    itemsGone (files);
    notifySelection ({});
}

void RemovalPage::forgetMissing (const juce::Array<juce::File>& files)
{
    juce::Array<juce::File> missing;

    for (const auto& file : files)
        if (! file.exists())
            missing.add (file);

    if (! missing.isEmpty())
        removedByAction (missing);
}

void RemovalPage::cycleChecks()
{
    checks = nextBulkCheck (checks);

    model.applyChecks (checks);
    table.refresh();
    updateSummary();
}

Histogram::Input RemovalPage::getHistogramInput() const
{
    return { model.getAllEntries(), model.getVisibleEntries(), model.getCheckedEntries(), model.getRoot() };
}

void RemovalPage::updateSummary()
{
    checksChanged();

    const auto checked = Removal::withoutNested (model.getCheckedEntries());
    auto text = model.hasResult() ? Format::count (model.getVisibleCount(), itemNoun()) + " listed, " + juce::String ((int) checked.size())
                                      + " checked (" + Format::size (totalSize (checked)) + ")"
                                  : noListText();

    if (const auto note = progressNote(); note.isNotEmpty())
        text << "   (" << note << ")";

    summary.setText (text, juce::dontSendNotification);

    for (auto* button : { &trashButton, &moveButton, &deleteButton })
        button->setEnabled (! checked.empty());

    // The check column header shows what the next click does.
    const auto next = nextBulkCheck (checks);
    table.setHeaderTooltip (juce::String ("Click to cycle the check boxes. Next: ")
                              + (next == BulkCheck::defaults ? "default checks" : (next == BulkCheck::all ? "check all" : "uncheck all")));

    if (onListChanged != nullptr)
        onListChanged();
}
