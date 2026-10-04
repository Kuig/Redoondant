#include "HistogramView.h"
#include "../Core/Format.h"

namespace
{
    constexpr int legendHeight = 22;
    constexpr int labelHeight = 32;         // Two lines of 12 pt.
    constexpr int valueHeight = 16;
    constexpr int barGap = 4;
    constexpr int gap = 8;

    // Same hue for "all" and "visible" (they differ in brightness); the checked ones have their own.
    const juce::Colour allColour     = juce::Colour::fromHSV (0.58f, 0.35f, 0.42f, 1.0f);
    const juce::Colour visibleColour = juce::Colour::fromHSV (0.58f, 0.65f, 0.90f, 1.0f);
    const juce::Colour checkedColour = juce::Colour::fromHSV (0.04f, 0.80f, 0.95f, 1.0f);

    /** A size, with "b" for bytes ("512 b", not "512 bytes"). */
    juce::String sizeText (juce::int64 bytes)
    {
        return Format::size (bytes).replace (" bytes", " b").replace (" byte", " b");
    }

    juce::String dates (const Histogram::Stats& s)
    {
        return s.oldest.toMilliseconds() == 0 ? juce::String() : Format::day (s.oldest) + " - " + Format::day (s.newest);
    }
}

HistogramView::HistogramView()
{
    logButton.setTooltip ("Logarithmic scale for the height of the bars");
    logButton.onClick = [this]
    {
        repaint();

        if (onLogChanged != nullptr)
            onLogChanged (logButton.getToggleState());
    };
    addAndMakeVisible (logButton);
}

void HistogramView::setLogScale (bool shouldBeLog)
{
    logButton.setToggleState (shouldBeLog, juce::dontSendNotification);
    repaint();
}

void HistogramView::resized()
{
    auto legend = getLocalBounds().removeFromBottom (legendHeight);
    logButton.setBounds (legend.removeFromRight (56));
}

double HistogramView::heightFraction (juce::int64 value, juce::int64 most) const
{
    if (value <= 0 || most <= 0)
        return 0.0;

    return logButton.getToggleState() ? std::log10 (1.0 + (double) value) / std::log10 (1.0 + (double) most)
                                      : (double) value / (double) most;
}

void HistogramView::setBins (std::vector<Histogram::Bin> newBins, Metric newMetric)
{
    bins = std::move (newBins);
    metric = newMetric;
    hovered = juce::jmin (hovered, (int) bins.size() - 1);
    repaint();
}

juce::Rectangle<int> HistogramView::plotArea() const
{
    return getLocalBounds().withTrimmedBottom (legendHeight);
}

/** A bar with its label below and its value above. */
juce::Rectangle<int> HistogramView::columnBounds (int index) const
{
    const auto area = plotArea();
    const int width = bins.empty() ? area.getWidth() : area.getWidth() / (int) bins.size();
    return { area.getX() + index * width, area.getY(), width, area.getHeight() };
}

int HistogramView::binAt (juce::Point<int> position) const
{
    for (int i = 0; i < (int) bins.size(); ++i)
        if (columnBounds (i).contains (position))
            return i;

    return -1;
}

juce::String HistogramView::valueText (const Histogram::Stats& stats) const
{
    return metric == Metric::count ? juce::String (stats.count) : sizeText (stats.bytes);
}

void HistogramView::paint (juce::Graphics& g)
{
    const auto text = findColour (juce::ListBox::textColourId);

    juce::int64 most = 0;

    for (const auto& bin : bins)
        most = juce::jmax (most, Histogram::valueOf (bin.total, metric));

    if (most == 0)
    {
        g.setColour (text.withAlpha (0.6f));
        g.setFont (juce::FontOptions (14.0f));
        g.drawText ("Nothing to show", getLocalBounds(), juce::Justification::centred);
        return;
    }

    const auto plot = plotArea();
    const int baseline = plot.getBottom() - labelHeight;
    const int fullHeight = baseline - (plot.getY() + valueHeight);
    const auto yOf = [&] (juce::int64 value) { return baseline - (int) std::ceil ((double) fullHeight * heightFraction (value, most)); };

    // Light horizontal grid: even steps on a linear scale; on a logarithmic one a line at each decade
    // (1, 10, 100...) and lighter ones at 2, 3... 9 times it, which bunch up the way the scale does.
    const auto gridLine = [&] (double value, float alpha)
    {
        g.setColour (text.withAlpha (alpha));
        g.drawHorizontalLine (yOf ((juce::int64) value), (float) plot.getX(), (float) plot.getRight());
    };

    if (logButton.getToggleState())
    {
        for (double decade = 1.0; decade <= (double) most; decade *= 10.0)
        {
            gridLine (decade, 0.16f);

            for (int multiple = 2; multiple <= 9 && decade * multiple <= (double) most; ++multiple)
                gridLine (decade * multiple, 0.06f);
        }
    }
    else
    {
        for (int k = 1; k <= 4; ++k)
            gridLine ((double) most * k / 4.0, 0.12f);
    }

    g.setColour (text.withAlpha (0.35f));
    g.drawHorizontalLine (baseline, (float) plot.getX(), (float) plot.getRight());

    for (int i = 0; i < (int) bins.size(); ++i)
    {
        const auto& bin = bins[(size_t) i];
        auto column = columnBounds (i);

        if (i == hovered)
        {
            g.setColour (text.withAlpha (0.08f));
            g.fillRect (column);
        }

        // Long labels wrap onto two lines and are cut after that; the hint has the whole label.
        g.setColour (text.withAlpha (bin.total.count > 0 ? 1.0f : 0.5f));
        g.setFont (juce::FontOptions (12.0f));
        g.drawFittedText (bin.label, column.removeFromBottom (labelHeight).reduced (2, 0), juce::Justification::centredTop, 2, 0.9f);

        const auto bars = column.reduced (barGap, 0);

        const auto draw = [&] (const Histogram::Stats& stats, juce::Colour colour)
        {
            const int top = yOf (Histogram::valueOf (stats, metric));

            if (top < baseline)
            {
                g.setColour (colour);
                g.fillRect (juce::Rectangle<int> (bars.getX(), top, bars.getWidth(), baseline - top));
            }
        };

        draw (bin.total, allColour);
        draw (bin.filtered, visibleColour);
        draw (bin.checked, checkedColour);

        // The value above the bar: what is shown, and the total when the filters hide some. Nothing for an empty bar.
        if (bin.total.count > 0)
        {
            const auto shown = valueText (bin.filtered), all = valueText (bin.total);
            g.setColour (text);
            g.setFont (juce::FontOptions (11.0f));
            g.drawFittedText (shown == all ? all : shown + "/" + all,
                              juce::Rectangle<int> (column.getX(), yOf (Histogram::valueOf (bin.total, metric)) - valueHeight, column.getWidth(), valueHeight),
                              juce::Justification::centredBottom, 1, 0.7f);
        }
    }

    // Legend (the log check box is at its right end).
    auto legend = getLocalBounds().removeFromBottom (legendHeight).withTrimmedRight (56).reduced (2, 4);
    g.setFont (juce::FontOptions (12.0f));

    for (const auto& [colour, name] : { std::pair<juce::Colour, const char*> { allColour, "All" }, { visibleColour, "Shown (filters)" }, { checkedColour, "Checked" } })
    {
        g.setColour (colour);
        g.fillRect (legend.removeFromLeft (legend.getHeight()).reduced (2));
        g.setColour (text.withAlpha (0.8f));
        g.drawText (name, legend.removeFromLeft (juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), name) + 6),
                    juce::Justification::centredLeft);
        legend.removeFromLeft (gap);
    }
}

void HistogramView::mouseMove (const juce::MouseEvent& e)
{
    const int index = binAt (e.getPosition());

    if (index != hovered)
    {
        hovered = index;
        repaint();
    }
}

void HistogramView::mouseExit (const juce::MouseEvent&)
{
    hovered = -1;
    repaint();
}

void HistogramView::mouseUp (const juce::MouseEvent& e)
{
    const int index = binAt (e.getPosition());

    if (index >= 0 && bins[(size_t) index].filterable && e.mouseWasClicked() && onBinClicked != nullptr)
        onBinClicked (index);
}

juce::MouseCursor HistogramView::getMouseCursor()
{
    return hovered >= 0 && bins[(size_t) hovered].filterable ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor;
}

juce::String HistogramView::getTooltip()
{
    if (! juce::isPositiveAndBelow (hovered, (int) bins.size()))
        return {};

    const auto& bin = bins[(size_t) hovered];
    const auto& total = bin.total;
    juce::int64 allItems = 0, allBytes = 0;

    for (const auto& b : bins)
    {
        allItems += b.total.count;
        allBytes += b.total.bytes;
    }

    const auto share = [] (juce::int64 part, juce::int64 whole) { return whole > 0 ? juce::String (100.0 * (double) part / (double) whole, 1) + "%" : juce::String ("0%"); };

    juce::String tip;
    tip << bin.label << "\n";
    tip << Format::count (total.count, "item") << " (" << share (total.count, allItems) << "), "
        << sizeText (total.bytes) << " (" << share (total.bytes, allBytes) << ")";

    if (total.count > 0)
        tip << "\nAverage " << sizeText (total.average()) << ", largest " << total.largestName << " (" << sizeText (total.largestSize) << ")";

    if (dates (total).isNotEmpty())
        tip << "\nModified " << dates (total);

    if (bin.filtered.count != total.count)
        tip << "\nShown by the filters: " << bin.filtered.count << " items, " << sizeText (bin.filtered.bytes);

    if (bin.checked.count > 0)
        tip << "\nChecked: " << bin.checked.count << " items, " << sizeText (bin.checked.bytes);

    tip << (bin.filterable ? "\nClick to show only these" : juce::String());
    return tip;
}
