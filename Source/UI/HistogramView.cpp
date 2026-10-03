#include "HistogramView.h"
#include "../Core/Format.h"

namespace
{
    constexpr int maxRowHeight = 30;
    constexpr int legendHeight = 22;
    constexpr int valueWidth = 120;
    constexpr int gap = 8;

    // Same hue for "all" and "visible" (they differ in brightness); the checked ones have their own.
    const juce::Colour allColour     = juce::Colour::fromHSV (0.58f, 0.35f, 0.42f, 1.0f);
    const juce::Colour visibleColour = juce::Colour::fromHSV (0.58f, 0.65f, 0.90f, 1.0f);
    const juce::Colour checkedColour = juce::Colour::fromHSV (0.04f, 0.80f, 0.95f, 1.0f);

    juce::String dates (const Histogram::Stats& s)
    {
        return s.oldest.toMilliseconds() == 0 ? juce::String() : Format::date (s.oldest).substring (0, 10) + " - " + Format::date (s.newest).substring (0, 10);
    }
}

void HistogramView::setBins (std::vector<Histogram::Bin> newBins, Metric newMetric)
{
    bins = std::move (newBins);
    metric = newMetric;
    hovered = juce::jmin (hovered, (int) bins.size() - 1);
    repaint();
}

juce::Rectangle<int> HistogramView::barsArea() const
{
    return getLocalBounds().withTrimmedBottom (legendHeight);
}

juce::Rectangle<int> HistogramView::rowBounds (int index) const
{
    const auto area = barsArea();
    const int rowHeight = bins.empty() ? maxRowHeight : juce::jmin (maxRowHeight, area.getHeight() / (int) bins.size());
    return { area.getX(), area.getY() + index * rowHeight, area.getWidth(), rowHeight };
}

int HistogramView::binAt (juce::Point<int> position) const
{
    for (int i = 0; i < (int) bins.size(); ++i)
        if (rowBounds (i).contains (position))
            return i;

    return -1;
}

juce::String HistogramView::valueText (const Histogram::Stats& stats) const
{
    return metric == Metric::count ? juce::String (stats.count) : Format::size (stats.bytes);
}

void HistogramView::paint (juce::Graphics& g)
{
    const auto text = findColour (juce::ListBox::textColourId);
    g.setFont (juce::FontOptions (14.0f));

    juce::int64 most = 0;

    for (const auto& bin : bins)
        most = juce::jmax (most, Histogram::valueOf (bin.total, metric));

    if (most == 0)
    {
        g.setColour (text.withAlpha (0.6f));
        g.drawText ("Nothing to show", getLocalBounds(), juce::Justification::centred);
        return;
    }

    const int labelWidth = juce::jlimit (90, 220, getWidth() * 35 / 100);

    for (int i = 0; i < (int) bins.size(); ++i)
    {
        const auto& bin = bins[(size_t) i];
        const auto row = rowBounds (i);

        if (i == hovered)
        {
            g.setColour (text.withAlpha (0.08f));
            g.fillRect (row);
        }

        auto cells = row.reduced (2, 3);
        g.setColour (text.withAlpha (bin.total.count > 0 ? 1.0f : 0.5f));
        g.drawText (bin.label, cells.removeFromLeft (labelWidth), juce::Justification::centredLeft, true);
        cells.removeFromLeft (gap);

        auto value = cells.removeFromRight (valueWidth);
        const int fullWidth = cells.getWidth() - gap;

        const auto draw = [&] (const Histogram::Stats& stats, juce::Colour colour)
        {
            const int w = (int) std::ceil ((double) fullWidth * (double) Histogram::valueOf (stats, metric) / (double) most);

            if (w > 0)
            {
                g.setColour (colour);
                g.fillRect (cells.withWidth (w));
            }
        };

        draw (bin.total, allColour);
        draw (bin.filtered, visibleColour);
        draw (bin.checked, checkedColour);

        g.setColour (text);
        g.drawText (valueText (bin.filtered) + (bin.filtered.count != bin.total.count ? " / " + valueText (bin.total) : juce::String()),
                    value, juce::Justification::centredRight, true);
    }

    // Legend.
    auto legend = getLocalBounds().removeFromBottom (legendHeight).reduced (2, 4);
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
    tip << Format::count ((size_t) total.count, "item") << " (" << share (total.count, allItems) << "), "
        << Format::size (total.bytes) << " (" << share (total.bytes, allBytes) << ")";

    if (total.count > 0)
        tip << "\nAverage " << Format::size (total.average()) << ", largest " << total.largestName << " (" << Format::size (total.largestSize) << ")";

    if (dates (total).isNotEmpty())
        tip << "\nModified " << dates (total);

    if (bin.filtered.count != total.count)
        tip << "\nShown by the filters: " << bin.filtered.count << " items, " << Format::size (bin.filtered.bytes);

    if (bin.checked.count > 0)
        tip << "\nChecked: " << bin.checked.count << " items, " << Format::size (bin.checked.bytes);

    tip << (bin.filterable ? "\nClick to show only these" : juce::String());
    return tip;
}
