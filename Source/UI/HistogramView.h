#pragma once

#include "../Core/Histogram.h"

/** Vertical bars (the classes along the x axis, the frequency up) for a list of Histogram::Bin, in three layers: all the entries (dim), the visible ones (brighter)
    and the checked ones (another hue). Hovering a bar shows its statistics, clicking it reports its index.
*/
class HistogramView final : public juce::Component,
                            public juce::TooltipClient
{
public:
    HistogramView();

    void setBins (std::vector<Histogram::Bin> newBins, Metric newMetric);

    /** Logarithmic height of the bars (the "log" check box, bottom right). */
    void setLogScale (bool shouldBeLog);
    std::function<void (bool)> onLogChanged;

    /** Called with the index of a clickable bar. */
    std::function<void (int)> onBinClicked;

    juce::String getTooltip() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    juce::MouseCursor getMouseCursor() override;

private:
    std::vector<Histogram::Bin> bins;
    Metric metric = Metric::count;
    int hovered = -1;
    juce::ToggleButton logButton { "log" };

    int binAt (juce::Point<int> position) const;
    juce::Rectangle<int> columnBounds (int index) const;
    juce::Rectangle<int> plotArea() const;
    juce::String valueText (const Histogram::Stats& stats) const;
    double heightFraction (juce::int64 value, juce::int64 most) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HistogramView)
};
