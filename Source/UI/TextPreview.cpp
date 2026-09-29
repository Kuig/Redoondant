#include "TextPreview.h"

namespace
{
    constexpr int rowHeight = 17;
    constexpr int padding = 6;
    constexpr int tabWidth = 4;
}

TextPreview::TextPreview()
{
    characterWidth = juce::jmax (1.0f, juce::GlyphArrangement::getStringWidth (font, "0"));

    list.setRowHeight (rowHeight);
    list.setOutlineThickness (1);
    list.setWantsKeyboardFocus (false);
    list.setRowSelectedOnMouseDown (false);
    addAndMakeVisible (list);
}

void TextPreview::setText (const juce::String& newText)
{
    paragraphs = juce::StringArray::fromLines (newText.replace ("\t", juce::String::repeatedString (" ", tabWidth)));
    wrap();

    // Always start from the top. (Not scrollToEnsureRowIsOnscreen: while the panel is hidden the
    // list has no height, and that would scroll one row down.)
    list.getViewport()->setViewPosition (0, 0);
}

void TextPreview::resized()
{
    list.setBounds (getLocalBounds());
    list.setColour (juce::ListBox::backgroundColourId, findColour (juce::TextEditor::backgroundColourId));
    list.setColour (juce::ListBox::outlineColourId, findColour (juce::TextEditor::outlineColourId));

    const int available = getWidth() - 2 * padding - list.getVerticalScrollBar().getWidth();
    const int newColumns = juce::jmax (8, (int) ((float) available / characterWidth));

    if (newColumns != columns)
    {
        columns = newColumns;
        wrap();
    }
}

void TextPreview::wrap()
{
    rows.clear();

    for (int p = 0; p < paragraphs.size(); ++p)
    {
        const int length = paragraphs[p].length();

        if (length == 0)
        {
            rows.push_back ({ p, 0, 0 });
            continue;
        }

        for (int start = 0; start < length; start += columns)
            rows.push_back ({ p, start, juce::jmin (columns, length - start) });
    }

    list.updateContent();
    list.repaint();
}

void TextPreview::paintListBoxItem (int rowNumber, juce::Graphics& g, int width, int height, bool)
{
    if (! juce::isPositiveAndBelow (rowNumber, (int) rows.size()))
        return;

    const auto& row = rows[(size_t) rowNumber];

    g.setColour (findColour (juce::TextEditor::textColourId));
    g.setFont (font);
    g.drawText (paragraphs[row.paragraph].substring (row.start, row.start + row.length),
                padding, 0, width - padding, height, juce::Justification::centredLeft, false);
}
