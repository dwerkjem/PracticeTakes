#include "ScoreViewComponent.h"

#include "engraving/ScoreLayoutEngine.h"

#include <algorithm>
#include <cmath>

namespace
{
// Pixels of margin inside the scrolling canvas, so the first clef is not
// against the edge and the last system is not against the bottom.
constexpr int canvasMargin = 14;

// Height of the strip naming the score, and of the strip listing what was not
// drawn.
constexpr int titleStripHeight = 24;
constexpr int noticeStripHeight = 46;

[[nodiscard]] double spaceSizeFor(int zoomIndex)
{
    switch (zoomIndex)
    {
    case 0:
        return 5.2;
    case 2:
        return 9.4;
    default:
        return 7.0;
    }
}
} // namespace

ScoreViewComponent::Canvas::Canvas(ScoreViewComponent& ownerToUse) : owner(ownerToUse) {}

void ScoreViewComponent::Canvas::paint(juce::Graphics& graphics)
{
    const ScoreGlyphPainter painter(
        graphics, owner.pixelsPerSpace(), scoreNotationPaletteFor(owner.currentTheme));

    for (const engraving::System& system : owner.layout.systems)
    {
        painter.drawSystem(
            system, static_cast<double>(canvasMargin),
            static_cast<double>(canvasMargin) + (system.topOfStaff * owner.pixelsPerSpace()),
            owner.layout.lyricVerseCount);
    }
}

ScoreViewComponent::ScoreViewComponent(const score::CurrentScore& sharedCurrentScore)
    : currentScore(sharedCurrentScore)
{
    setComponentID("score-view-tool");

    viewport.setViewedComponent(&canvas, false);
    viewport.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport);

    refreshFromCurrentScore();
    startTimerHz(refreshRateHz);
}

ScoreViewComponent::~ScoreViewComponent()
{
    stopTimer();
}

double ScoreViewComponent::pixelsPerSpace() const
{
    return spaceSizeFor(static_cast<int>(currentZoom));
}

AppPalette ScoreViewComponent::palette() const
{
    return appPaletteFor(currentTheme);
}

void ScoreViewComponent::timerCallback()
{
    refreshFromCurrentScore();
}

void ScoreViewComponent::refreshFromCurrentScore()
{
    std::shared_ptr<const score::Score> latest = currentScore.score();

    // Pointer identity is the whole change check: two live scores are never the
    // same pointer, and holding `renderedScore` is what guarantees it.
    if (latest == renderedScore)
    {
        return;
    }

    renderedScore = std::move(latest);

    // Force the layout to be recomputed even though the width has not changed.
    layoutWidth = 0;
    relayout();

    repaint();
}

void ScoreViewComponent::relayout()
{
    const int available = viewport.getWidth();

    if (available <= 0)
    {
        return;
    }

    if (available == layoutWidth)
    {
        return;
    }

    layoutWidth = available;

    engraving::LayoutOptions options;
    options.availableWidth =
        std::max(24.0, (static_cast<double>(available - (canvasMargin * 2)) / pixelsPerSpace()));

    layout = engraving::layOutScore(renderedScore, options);

    const int height =
        static_cast<int>(std::ceil(layout.totalHeight * pixelsPerSpace())) + (canvasMargin * 2);

    // Wide enough for the viewport, or wider if a single bar overflowed -- a
    // measure that does not fit anywhere still has to go somewhere, and the
    // layout says so by returning a system wider than it was asked for.
    double widest = 0.0;

    for (const engraving::System& system : layout.systems)
    {
        widest = std::max(widest, system.width);
    }

    const int width = std::max(
        available, static_cast<int>(std::ceil(widest * pixelsPerSpace())) + (canvasMargin * 2));

    canvas.setSize(width, std::max(height, viewport.getHeight()));
    canvas.repaint();

    // Hidden rather than left empty when there is nothing to draw: the tool
    // paints a sentence over the whole panel in that case, and an empty
    // scrolling area on top of it would show a scrollbar beside a message.
    viewport.setVisible(layout.outcome == engraving::LayoutOutcome::laidOut);
}

void ScoreViewComponent::resized()
{
    juce::Rectangle<int> bounds = getLocalBounds();

    bounds.removeFromTop(titleStripHeight);

    // Reserved whether or not there is anything in it, so that opening a score
    // does not move the staff. A strip that appeared only when a notice did
    // would reflow everything the moment a file finished loading, and which
    // files have notices is a property of the files rather than of the tool.
    bounds.removeFromBottom(noticeStripHeight);

    viewport.setBounds(bounds);

    // The width changed, so the systems break differently.
    layoutWidth = 0;
    relayout();
}

void ScoreViewComponent::setTheme(Theme theme)
{
    currentTheme = theme;
    canvas.repaint();
    repaint();
}

void ScoreViewComponent::resetToDefaults()
{
    currentZoom = Zoom::medium;

    // Which score is shown is the application's business rather than a
    // preference, so the honest thing a reset can do is re-read the holder and
    // lay it out afresh.
    renderedScore = nullptr;
    refreshFromCurrentScore();
    relayout();
    repaint();
}

void ScoreViewComponent::setZoom(Zoom zoom)
{
    if (zoom == currentZoom)
    {
        return;
    }

    currentZoom = zoom;
    layoutWidth = 0;
    relayout();
    repaint();
}

std::vector<ToolComponent::MenuEntry> ScoreViewComponent::optionsMenuEntries()
{
    const auto entry = [this](const juce::String& label, Zoom zoom)
    { return MenuEntry{label, [this, zoom] { setZoom(zoom); }, currentZoom == zoom}; };

    return {
        entry("Small staff", Zoom::small), entry("Medium staff", Zoom::medium),
        entry("Large staff", Zoom::large)};
}

juce::String ScoreViewComponent::noticeText() const
{
    juce::StringArray lines;

    for (const std::string& notice : layout.notices)
    {
        lines.add(juce::String(notice));
    }

    return lines.joinIntoString("  ");
}

void ScoreViewComponent::paintMessage(
    juce::Graphics& graphics,
    juce::Rectangle<int> bounds,
    const juce::String& heading,
    const juce::String& detail) const
{
    const AppPalette colours = palette();

    graphics.setColour(colours.foreground);
    graphics.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    graphics.drawText(heading, bounds.removeFromTop(26), juce::Justification::centredLeft);

    bounds.removeFromTop(6);

    graphics.setColour(colours.muted);
    graphics.setFont(juce::FontOptions(14.0f));
    graphics.drawFittedText(detail, bounds, juce::Justification::topLeft, 4);
}

void ScoreViewComponent::paint(juce::Graphics& graphics)
{
    const AppPalette colours = palette();

    graphics.fillAll(colours.background);

    juce::Rectangle<int> bounds = getLocalBounds();

    if (layout.outcome != engraving::LayoutOutcome::laidOut)
    {
        const juce::Rectangle<int> message = bounds.reduced(16);

        if (layout.outcome == engraving::LayoutOutcome::noScore)
        {
            paintMessage(
                graphics, message, "No score open",
                "Open a MusicXML file with File > Open Score, and it is drawn here.");

            return;
        }

        paintMessage(graphics, message, "Nothing to draw", noticeText());

        return;
    }

    // The title strip: what is being drawn, and which part of it.
    juce::Rectangle<int> title = bounds.removeFromTop(titleStripHeight).reduced(canvasMargin, 2);

    graphics.setColour(colours.foreground);
    graphics.setFont(juce::FontOptions(13.0f, juce::Font::bold));

    const juce::String heading =
        juce::String(layout.scoreTitle.empty() ? "Untitled score" : layout.scoreTitle) + "  -  " +
        juce::String(layout.partName);

    graphics.drawText(heading, title, juce::Justification::centredLeft);

    if (renderedScore != nullptr && !renderedScore->diagnostics.empty())
    {
        // The importer had something to say about this file, so what is drawn
        // is not a complete rendering of it. The summary window holds the
        // detail; this is the pointer to it.
        graphics.setColour(colours.warning);
        graphics.setFont(juce::FontOptions(12.0f));
        graphics.drawText(
            juce::String(static_cast<int>(renderedScore->diagnostics.size())) +
                " import diagnostic" + (renderedScore->diagnostics.size() == 1 ? "" : "s"),
            title, juce::Justification::centredRight);
    }

    if (layout.notices.empty())
    {
        return;
    }

    const juce::Rectangle<int> notices =
        bounds.removeFromBottom(noticeStripHeight).reduced(canvasMargin, 4);

    graphics.setColour(colours.panel);
    graphics.fillRect(notices.expanded(canvasMargin, 2));

    graphics.setColour(colours.muted);
    graphics.setFont(juce::FontOptions(11.5f));
    graphics.drawFittedText(noticeText(), notices, juce::Justification::topLeft, 3);
}
