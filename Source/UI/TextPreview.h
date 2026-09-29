#pragma once

#include <JuceHeader.h>

/** Read-only viewer for plain text that only lays out the visible lines, so even large texts
    appear instantly (juce::TextEditor lays out the whole text up front, which took seconds
    for a 64 KB preview). Long lines wrap at the panel width, using a monospaced font.
*/
class TextPreview final : public juce::Component,
                          private juce::ListBoxModel
{
public:
    TextPreview();

    /** Cheap: the text is only split into paragraphs; wrapping is computed for the current width. */
    void setText (const juce::String& newText);

    void resized() override;

private:
    struct Row
    {
        int paragraph = 0;
        int start = 0;
        int length = 0;
    };

    juce::ListBox list { {}, this };
    juce::Font font { juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain) };
    float characterWidth = 8.0f;
    int columns = 80;
    juce::StringArray paragraphs;
    std::vector<Row> rows;

    void wrap();

    int getNumRows() override                           { return (int) rows.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool isSelected) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TextPreview)
};
