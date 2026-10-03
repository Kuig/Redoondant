#pragma once

#include "../Core/Histogram.h"

/** Vertical bars (the classes along the x axis, the frequency up) for a list of Histogram::Bin, in three layers: all the entries (dim), the visible ones (brighter)
    and the checked ones (another hue). Hovering a bar shows its statistics, clicking it reports its index.
*/
class HistogramView final : public juce::Component,
                            public juce::TooltipClient
{
public:
    HistogramView() = default;

    void setBins (std::vector<Histogram::Bin> newBins, Metric newMetric);

    /** Called with the index of a clickable bar. */
    std::function<void (int)> onBinClicked;

    juce::String getTooltip() override;

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    juce::MouseCursor getMouseCursor() override;

private:
    std::vector<Histogram::Bin> bins;
    Metric metric = Metric::count;
    int hovered = -1;

    int binAt (juce::Point<int> position) const;
    juce::Rectangle<int> columnBounds (int index) const;
    juce::Rectangle<int> plotArea() const;
    juce::String valueText (const Histogram::Stats& stats) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HistogramView)
};
