#include "ScoreViewComponent.h"

#include "MusicFont.h"

#include <algorithm>
#include <cmath>

using namespace score::engraving;

namespace
{
// A five-line staff spans four staff spaces between its outer lines.
constexpr double staffHeightSpaces = 4.0;

// Margin around the page, in staff spaces.
constexpr double pageMargin = 2.5;

// Zoom limits, in pixels per staff space. Below about five the staff lines
// merge; above about eighteen a bar fills the panel.
constexpr float minimumStaffSpacePixels = 4.0f;
constexpr float maximumStaffSpacePixels = 18.0f;
constexpr float zoomStep = 1.5f;

[[nodiscard]] juce::String glyphText(Glyph glyph)
{
    return juce::String::charToString(
        static_cast<juce::juce_wchar>(score::engraving::codepoint(glyph)));
}
} // namespace

ScoreViewComponent::Page::Page(ScoreViewComponent& ownerView) : owner(ownerView) {}

void ScoreViewComponent::Page::paint(juce::Graphics& graphics)
{
    owner.paintPage(graphics);
}

ScoreViewComponent::ScoreViewComponent(const score::CurrentScore& sharedCurrentScore)
    : currentScore(sharedCurrentScore), page(*this)
{
    setComponentID("score-view-tool");

    musicFont = juce::Font(score::notation::musicTypeface());

    viewport.setViewedComponent(&page, false);
    viewport.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport);

    refreshFromCurrentScore();
    startTimerHz(refreshRateHz);
}

ScoreViewComponent::~ScoreViewComponent()
{
    stopTimer();
}

void ScoreViewComponent::timerCallback()
{
    refreshFromCurrentScore();
}

void ScoreViewComponent::refreshFromCurrentScore()
{
    std::shared_ptr<const score::Score> latest = currentScore.score();

    // Pointer identity is the whole change check, for the reason
    // score::CurrentScore gives: while this holds the old score it cannot be
    // freed, so its address cannot be reused by a different one.
    if (latest == laidOutScore)
    {
        return;
    }

    laidOutScore = std::move(latest);

    // Force a re-layout even if the width has not changed -- it is a different
    // score.
    laidOutWidth = 0;

    relayout();
    repaint();
}

void ScoreViewComponent::relayout()
{
    const int width = viewport.getMaximumVisibleWidth();

    if (width <= 0)
    {
        return;
    }

    if (laidOutScore == nullptr)
    {
        layout = ScoreLayout{};
        laidOutWidth = width;
        page.setSize(width, 1);

        return;
    }

    LayoutOptions options;
    options.availableWidth =
        std::max(20.0, (static_cast<double>(width) / staffSpacePixels) - (2 * pageMargin));

    layout = layOutScore(*laidOutScore, options);
    laidOutWidth = width;

    const int pageHeight = static_cast<int>(std::ceil(toPixels(layout.height + (2 * pageMargin))));

    page.setSize(width, std::max(pageHeight, 1));
    page.repaint();
}

void ScoreViewComponent::resized()
{
    viewport.setBounds(getLocalBounds());

    // Only redo the work when the width actually changed; a vertical resize
    // cannot change where anything goes.
    if (viewport.getMaximumVisibleWidth() != laidOutWidth)
    {
        relayout();
    }
}

void ScoreViewComponent::setTheme(Theme theme)
{
    currentTheme = theme;
    repaint();
    page.repaint();
}

void ScoreViewComponent::resetToDefaults()
{
    staffSpacePixels = defaultStaffSpacePixels;
    laidOutWidth = 0;

    relayout();
    repaint();
}

std::vector<ToolComponent::MenuEntry> ScoreViewComponent::optionsMenuEntries()
{
    std::vector<MenuEntry> entries;

    entries.push_back(MenuEntry{
        "Zoom in",
        [this]
        {
            staffSpacePixels = std::min(maximumStaffSpacePixels, staffSpacePixels * zoomStep);
            laidOutWidth = 0;
            relayout();
        },
        false});

    entries.push_back(MenuEntry{
        "Zoom out",
        [this]
        {
            staffSpacePixels = std::max(minimumStaffSpacePixels, staffSpacePixels / zoomStep);
            laidOutWidth = 0;
            relayout();
        },
        false});

    // Everything the renderer could not draw, read out rather than left for the
    // user to notice. A renderer that silently omits what it does not
    // understand is the failure this tool is specified to avoid.
    for (const score::Diagnostic& diagnostic : layout.diagnostics)
    {
        juce::String label = "Bar ";

        label += diagnostic.location.measureNumber.has_value()
                     ? juce::String(*diagnostic.location.measureNumber)
                     : juce::String("?");
        label += ": ";
        label += juce::String(diagnostic.message);

        entries.push_back(MenuEntry{label, [] {}, false});
    }

    return entries;
}

float ScoreViewComponent::toPixels(double staffSpaces) const noexcept
{
    return static_cast<float>(staffSpaces) * staffSpacePixels;
}

float ScoreViewComponent::yFor(
    const SystemLayout& system,
    const StaffLayout& staff,
    double staffStep) const noexcept
{
    // A staff step is half a staff space, counted upward from the bottom line,
    // and `topOffset` is measured to the staff's *top* line -- so the bottom
    // line is four spaces below it and the step counts back up from there.
    const double spacesFromTop =
        system.topOffset + staff.topOffset + staffHeightSpaces - (staffStep * 0.5);

    return toPixels(spacesFromTop + pageMargin);
}

AppPalette ScoreViewComponent::palette() const
{
    return appPaletteFor(currentTheme);
}

void ScoreViewComponent::paint(juce::Graphics& graphics)
{
    const AppPalette colours = palette();

    graphics.fillAll(colours.background);

    if (laidOutScore == nullptr)
    {
        paintEmptyState(graphics, getLocalBounds().reduced(16));
    }
}

void ScoreViewComponent::paintEmptyState(juce::Graphics& graphics, juce::Rectangle<int> bounds)
    const
{
    const AppPalette colours = palette();

    graphics.setColour(colours.foreground);
    graphics.setFont(juce::Font(juce::FontOptions(20.0f).withStyle("Bold")));
    graphics.drawText("No score open", bounds.removeFromTop(32), juce::Justification::centredTop);

    graphics.setColour(colours.muted);
    graphics.setFont(juce::Font(juce::FontOptions(14.0f)));
    graphics.drawFittedText(
        "Open a MusicXML file with File > Open Score. A piano part is drawn as a braced "
        "grand staff.",
        bounds.removeFromTop(60), juce::Justification::centredTop, 3);
}

void ScoreViewComponent::paintStaffLines(
    juce::Graphics& graphics,
    const SystemLayout& system,
    const StaffLayout& staff) const
{
    const AppPalette colours = palette();

    graphics.setColour(colours.foreground.withAlpha(0.7f));

    const float thickness =
        std::max(1.0f, toPixels(score::engraving::engravingDefaults.staffLineThickness));

    const float left = toPixels(pageMargin);
    const float right = toPixels(layout.width + pageMargin);

    // Five lines, at steps 0, 2, 4, 6 and 8.
    for (int step = bottomLineStep; step <= topLineStep; step += 2)
    {
        const float y = yFor(system, staff, step);

        graphics.fillRect(left, y - (thickness * 0.5f), right - left, thickness);
    }
}

void ScoreViewComponent::paintPage(juce::Graphics& graphics) const
{
    const AppPalette colours = palette();

    graphics.fillAll(colours.background);

    if (laidOutScore == nullptr)
    {
        return;
    }

    const float barlineThickness =
        std::max(1.0f, toPixels(score::engraving::engravingDefaults.thinBarlineThickness));

    for (const SystemLayout& system : layout.systems)
    {
        for (const PartLayout& part : system.parts)
        {
            for (const StaffLayout& staff : part.staves)
            {
                paintStaffLines(graphics, system, staff);
            }

            // The brace. Drawn as a path rather than as the font's glyph:
            // Bravura's brace is a single fixed-height character, and a grand
            // staff's brace has to stretch to whatever the part's staves span.
            if (part.braced)
            {
                const float top = toPixels(system.topOffset + part.braceTop + pageMargin);
                const float bottom = toPixels(system.topOffset + part.braceBottom + pageMargin);
                const float x = toPixels(pageMargin + part.braceX) - toPixels(1.2);
                const float bulge = toPixels(0.9);

                juce::Path brace;
                brace.startNewSubPath(x, top);
                brace.cubicTo(
                    x - bulge, top + ((bottom - top) * 0.25f), x + bulge,
                    top + ((bottom - top) * 0.35f), x, (top + bottom) * 0.5f);
                brace.cubicTo(
                    x + bulge, bottom - ((bottom - top) * 0.35f), x - bulge,
                    bottom - ((bottom - top) * 0.25f), x, bottom);

                graphics.setColour(colours.foreground);
                graphics.strokePath(brace, juce::PathStrokeType(std::max(1.5f, toPixels(0.22))));

                // And the vertical line joining the staves, which is what makes
                // the two read as one system rather than two.
                graphics.fillRect(
                    toPixels(pageMargin) - (barlineThickness * 0.5f), top, barlineThickness,
                    bottom - top);
            }

            // Barlines, drawn through each staff of the part.
            graphics.setColour(colours.foreground);

            for (const double barlineX : system.barlineXs)
            {
                for (const StaffLayout& staff : part.staves)
                {
                    const float top = yFor(system, staff, topLineStep);
                    const float bottom = yFor(system, staff, bottomLineStep);

                    graphics.fillRect(
                        toPixels(barlineX + pageMargin), top, barlineThickness, bottom - top);
                }
            }

            // The music.
            for (const StaffLayout& staff : part.staves)
            {
                graphics.setColour(colours.foreground);

                for (const PlacedLedgerLine& ledger : staff.ledgerLines)
                {
                    const float y = yFor(system, staff, ledger.staffStep);
                    const float thickness = std::max(
                        1.0f, toPixels(score::engraving::engravingDefaults.legerLineThickness));

                    graphics.fillRect(
                        toPixels(ledger.x + pageMargin), y - (thickness * 0.5f),
                        toPixels(ledger.width), thickness);
                }

                // Stems, before the glyphs so a notehead sits over the end of
                // its own stem rather than under it.
                //
                // Every number here came from the layout: the two ends, and the
                // one x. Working out where a stem should stop is exactly the
                // kind of decision this surface must not make.
                for (const PlacedStem& stem : staff.stems)
                {
                    const float thickness =
                        std::max(1.0f, toPixels(score::engraving::engravingDefaults.stemThickness));

                    const float fromY = yFor(system, staff, stem.fromStep);
                    const float toY = yFor(system, staff, stem.toStep);

                    graphics.fillRect(
                        toPixels(stem.x + pageMargin) - (thickness * 0.5f), std::min(fromY, toY),
                        thickness, std::abs(toY - fromY));
                }

                // Beams. Four corners, in the order the layout listed them,
                // filled as a path.
                //
                // No slope is computed here and no thickness is chosen: a beam
                // arrives as a quadrilateral and leaves as one. That is the
                // whole of why beam geometry is assertable without a display.
                for (const PlacedBeam& beam : staff.beams)
                {
                    const float leftX = toPixels(beam.leftX + pageMargin);
                    const float rightX = toPixels(beam.rightX + pageMargin);

                    juce::Path quadrilateral;
                    quadrilateral.startNewSubPath(leftX, yFor(system, staff, beam.leftOuterStep));
                    quadrilateral.lineTo(rightX, yFor(system, staff, beam.rightOuterStep));
                    quadrilateral.lineTo(rightX, yFor(system, staff, beam.rightInnerStep));
                    quadrilateral.lineTo(leftX, yFor(system, staff, beam.leftInnerStep));
                    quadrilateral.closeSubPath();

                    graphics.fillPath(quadrilateral);
                }

                // One font size for every glyph: SMuFL states its metrics
                // against a staff of four spaces, so the em size *is* the staff
                // height and nothing needs a per-glyph scale.
                //
                // `withPointHeight`, not `withHeight`. JUCE's "height" is
                // ascent plus descent, and a music font's are enormous -- a
                // bracket spans several staves -- so asking for a height of one
                // staff draws glyphs a fraction of their intended size. The
                // first capture of this tool showed exactly that: legible
                // staves with noteheads like dust.
                graphics.setFont(musicFont.withPointHeight(toPixels(staffHeightSpaces)));

                for (const PlacedGlyph& glyph : staff.glyphs)
                {
                    const float x = toPixels(glyph.x + pageMargin);
                    const float y = yFor(system, staff, glyph.staffStep);

                    graphics.drawSingleLineText(
                        glyphText(glyph.glyph), juce::roundToInt(x), juce::roundToInt(y));
                }
            }
        }
    }
}
