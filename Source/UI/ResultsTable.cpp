#include "ResultsTable.h"
#include "../Core/Format.h"

namespace
{
    constexpr int checkColumnWidth = 30;
    constexpr int groupGap = 6;         // Space above each group header, separating groups.

    struct ColumnSpec
    {
        Column column;
        const char* title;
        int width;
    };

    const ColumnSpec columnSpecs[] =
    {
        { Column::name,     "Name",     260 },
        { Column::folder,   "Folder",   140 },
        { Column::size,     "Size",     80 },
        { Column::modified, "Modified", 125 },
        { Column::created,  "Created",  125 },
        { Column::contentCreated, "Content created", 125 },
        { Column::type,     "Type",     60 },
    };
}

/** Forwards column resizes/moves to onLayoutChanged, so they can be saved. */
struct ResultsTable::HeaderListener final : public juce::TableHeaderComponent::Listener
{
    explicit HeaderListener (ResultsTable& t) : owner (t) {}

    void tableColumnsChanged (juce::TableHeaderComponent*) override     { notify(); }
    void tableColumnsResized (juce::TableHeaderComponent*) override     { notify(); }
    void tableSortOrderChanged (juce::TableHeaderComponent*) override   { notify(); }

    void notify()
    {
        if (owner.onLayoutChanged != nullptr)
            owner.onLayoutChanged();
    }

    ResultsTable& owner;
};

/** Table header whose check column shows the overall check state and cycles it when clicked. */
struct ResultsTable::CheckHeader final : public juce::TableHeaderComponent,
                                         public juce::SettableTooltipClient
{
    explicit CheckHeader (ResultsTable& t) : owner (t) {}

    void paint (juce::Graphics& g) override
    {
        TableHeaderComponent::paint (g);

        const int index = getIndexOfColumnId ((int) Column::check, true);

        if (index >= 0)
            drawCheckBox (g, getColumnPosition (index).toFloat().withSizeKeepingCentre (14.0f, 14.0f),
                          owner.model.getOverallState(), findColour (juce::TableHeaderComponent::textColourId));
    }

    void columnClicked (int columnId, const juce::ModifierKeys& mods) override
    {
        if (columnId == (int) Column::check)
        {
            if (owner.onHeaderCheckClicked != nullptr)
                owner.onHeaderCheckClicked();
        }
        else
        {
            TableHeaderComponent::columnClicked (columnId, mods);
        }
    }

    ResultsTable& owner;
};

ResultsTable::ResultsTable (ResultsModel& m) : model (m)
{
    table.setHeader (std::make_unique<CheckHeader> (*this));
    auto& header = table.getHeader();
    header.addColumn ({}, (int) Column::check, checkColumnWidth, checkColumnWidth, checkColumnWidth,
                      juce::TableHeaderComponent::visible);

    for (const auto& spec : columnSpecs)
        header.addColumn (spec.title, (int) spec.column, spec.width, 40);

    header.setStretchToFitActive (true);
    headerListener = std::make_unique<HeaderListener> (*this);
    header.addListener (headerListener.get());

    icons.onIconLoaded = [this] { table.repaint(); };
    table.setRowHeight (22);
    table.setMultipleSelectionEnabled (true);
    table.setWantsKeyboardFocus (true);
    addAndMakeVisible (table);
}

juce::String ResultsTable::Table::getNameForRow (int row)
{
    if (const auto* entry = owner.model.getEntry (row))
        return entry->name();

    const auto* group = owner.model.getGroup (row);
    return group != nullptr ? group->title : juce::String();
}

ResultsTable::~ResultsTable()
{
    table.getHeader().removeListener (headerListener.get());
}

bool ResultsTable::isColumnVisible (Column column) const
{
    return table.getHeader().isColumnVisible ((int) column);
}

void ResultsTable::setSort (Column column, bool forwards)
{
    table.getHeader().setSortColumnId ((int) column, forwards);
}

void ResultsTable::setHeaderTooltip (const juce::String& text)
{
    static_cast<CheckHeader&> (table.getHeader()).setTooltip (text);
}

void ResultsTable::refresh()
{
    table.getHeader().repaint();
    table.updateContent();
    table.repaint();
}

juce::String ResultsTable::getLayoutState() const
{
    return table.getHeader().toString();
}

void ResultsTable::restoreLayoutState (const juce::String& state)
{
    if (state.isNotEmpty())
        table.getHeader().restoreFromString (state);
}

juce::Array<juce::File> ResultsTable::getSelectedFiles() const
{
    juce::Array<juce::File> files;
    const auto selected = table.getSelectedRows();

    for (int i = 0; i < selected.size(); ++i)
        if (const auto* entry = model.getEntry (selected[i]))
            files.add (entry->file);

    return files;
}

void ResultsTable::resized()
{
    table.setBounds (getLocalBounds());
}

bool ResultsTable::keyPressed (const juce::KeyPress& key)
{
    if (key != juce::KeyPress::spaceKey)
        return false;

    const auto selected = table.getSelectedRows();

    for (int i = 0; i < selected.size(); ++i)
        model.toggle (selected[i]);

    refresh();

    if (onCheckedChanged != nullptr)
        onCheckedChanged();

    return true;
}

int ResultsTable::getNumRows()
{
    return model.getNumRows();
}

void ResultsTable::paintRowBackground (juce::Graphics& g, int row, int width, int height, bool isSelected)
{
    const auto* r = model.getRow (row);

    if (r == nullptr)
        return;

    const auto background = findColour (juce::ListBox::backgroundColourId);
    const auto text = findColour (juce::ListBox::textColourId);

    if (! r->isHeader())
    {
        if (isSelected)
            g.fillAll (findColour (juce::TextEditor::highlightColourId));
        else if (row % 2 == 1)
            g.fillAll (background.contrasting (0.03f));

        return;
    }

    // Group separator: a band with the group's checkbox and title.
    const auto band = juce::Rectangle<int> (0, groupGap, width, height - groupGap);
    g.setColour (background.contrasting (0.15f));
    g.fillRect (band);

    const auto state = model.getGroupState (r->group);
    drawCheckBox (g, band.withWidth (checkColumnWidth).toFloat().withSizeKeepingCentre (14.0f, 14.0f), state, text);

    g.setColour (text);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText (model.getGroup (row)->title, band.withTrimmedLeft (checkColumnWidth + 4), juce::Justification::centredLeft, true);
}

void ResultsTable::paintCell (juce::Graphics& g, int row, int columnId, int width, int height, bool)
{
    const auto* entry = model.getEntry (row);

    if (entry == nullptr)
        return;

    const auto text = findColour (juce::ListBox::textColourId);
    const auto column = (Column) columnId;

    if (column == Column::check)
    {
        drawCheckBox (g, juce::Rectangle<float> ((float) width, (float) height).withSizeKeepingCentre (14.0f, 14.0f),
                      entry->selected ? CheckState::all : CheckState::none, text);
        return;
    }

    auto cell = model.getCellText (*entry, column, model.getGroup (row));

    if (column == Column::name && entry->isDirectory)
        cell += "/";

    int textX = 4;

    if (column == Column::name)
    {
        // The system icon (loaded in the background; the space is reserved meanwhile).
        const auto icon = icons.get (*entry);
        const int size = IconCache::iconSize;

        if (icon.isValid())
            g.drawImage (icon, juce::Rectangle<float> (4.0f, ((float) height - (float) size) * 0.5f, (float) size, (float) size),
                         juce::RectanglePlacement::centred, false);

        textX = 4 + size + 4;
    }

    g.setColour (entry->missing ? text.withAlpha (0.45f) : text);
    g.setFont (juce::FontOptions (14.0f, entry->isDirectory && column == Column::name ? juce::Font::bold : juce::Font::plain));
    g.drawText (cell, textX, 0, width - textX - 4, height,
                column == Column::size ? juce::Justification::centredRight : juce::Justification::centredLeft, true);
}

void ResultsTable::cellClicked (int row, int columnId, const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        showContextMenu (row, e.getScreenPosition());
        return;
    }

    const auto* r = model.getRow (row);

    if (r != nullptr && (columnId == (int) Column::check || r->isHeader()))
        toggle (row);
}

void ResultsTable::cellDoubleClicked (int row, int, const juce::MouseEvent&)
{
    if (const auto* entry = model.getEntry (row))
        entry->file.revealToUser();
}

void ResultsTable::sortOrderChanged (int columnId, bool isForwards)
{
    model.sort ((Column) columnId, isForwards);
    refresh();
}

void ResultsTable::selectedRowsChanged (int lastRowSelected)
{
    juce::ignoreUnused (lastRowSelected);

    if (onSelectionChanged == nullptr)
        return;

    std::vector<FileEntry> selected;
    const auto rows = table.getSelectedRows();

    for (int i = 0; i < rows.size(); ++i)
        if (const auto* entry = model.getEntry (rows[i]))
            selected.push_back (*entry);

    onSelectionChanged (std::move (selected));
}

juce::String ResultsTable::getCellTooltip (int row, int)
{
    if (const auto* entry = model.getEntry (row))
        return entry->file.getFullPathName();

    return {};
}

void ResultsTable::showContextMenu (int row, juce::Point<int> screenPosition)
{
    const auto* clicked = model.getEntry (row);

    if (clicked == nullptr || onContextMenu == nullptr)
        return;

    // The selected items of the clicked item's folder (a shell menu covers one folder), or just the clicked one.
    juce::Array<juce::File> files;
    const auto selected = table.getSelectedRows();

    if (table.isRowSelected (row))
        for (int i = 0; i < selected.size(); ++i)
            if (const auto* entry = model.getEntry (selected[i]))
                if (entry->file.getParentDirectory() == clicked->file.getParentDirectory())
                    files.add (entry->file);

    if (files.isEmpty())
        files.add (clicked->file);

    onContextMenu (files, screenPosition);
}

void ResultsTable::toggle (int row)
{
    model.toggle (row);
    refresh();

    if (onCheckedChanged != nullptr)
        onCheckedChanged();
}

void ResultsTable::drawCheckBox (juce::Graphics& g, juce::Rectangle<float> area, CheckState state, juce::Colour colour)
{
    g.setColour (colour.withAlpha (0.7f));
    g.drawRoundedRectangle (area, 2.0f, 1.2f);

    if (state == CheckState::none)
        return;

    g.setColour (colour);

    if (state == CheckState::some)
    {
        g.fillRect (area.reduced (3.5f, 6.0f));
        return;
    }

    juce::Path tick;
    const auto r = area.reduced (3.0f);
    tick.startNewSubPath (r.getX(), r.getCentreY());
    tick.lineTo (r.getX() + r.getWidth() * 0.4f, r.getBottom());
    tick.lineTo (r.getRight(), r.getY());
    g.strokePath (tick, juce::PathStrokeType (2.0f));
}
