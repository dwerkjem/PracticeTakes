#include "ScoreViewComponent.h"

#include "../../platform/evaluation/TakeReview.h"
#include "../../platform/score/PitchNames.h"
#include "engraving/NoteHit.h"
#include "engraving/PartNames.h"
#include "engraving/ScoreLayoutEngine.h"
#include "engraving/StaffGeometry.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
// Pixels of margin inside the scrolling canvas, so the first clef is not
// against the edge and the last system is not against the bottom.
constexpr int canvasMargin = 14;

// Height of the strip naming the score, and of the strip listing what was not
// drawn.
constexpr int titleStripHeight = 24;
constexpr int noticeStripHeight = 46;

// Page view: the room left around the page, and the page's own margin as a
// fraction of its width -- about 12 mm on A4.
constexpr float pageSurround = 12.0f;
constexpr float pageMarginFraction = 0.06f;

// A4, height over width.
constexpr double pageAspect = 1.4142135623730951;

// Horizontal scroll: a width no score reaches, so the packer never starts a
// second system.
constexpr double unboundedWidth = 1.0e7;

// The title strip's controls, right to left.
constexpr int controlButtonWidth = 26;
constexpr int sizeLabelWidth = 48;
constexpr int pageLabelWidth = 96;
constexpr int controlGap = 10;
constexpr int followButtonWidth = 64;

// Following in Horizontal scroll: when the cursor passes this far across the
// panel, the view moves so it is at the second fraction (score-cursor design.md
// decision 5). A screen at a time, not a crawl.
constexpr double horizontalFollowEdge = 0.7;
constexpr double horizontalFollowRestart = 0.2;

// Vertical scroll: a system the cursor enters is brought to this far down.
constexpr double verticalFollowPlace = 0.25;

// The cursor's marks, in staff spaces: an ellipse a little larger than a
// notehead, centred on it, and a band about as wide as one.
constexpr double markWidth = 1.75;
constexpr double markHeight = 1.3;
constexpr double cursorBandWidth = 1.6;

// The review's marks (take-review decision 3): a ring a little larger than the
// cursor's, so both show on a note that has both, and colours that read on
// either theme's paper.
constexpr double reviewMarkWidth = 2.1;
constexpr double reviewMarkHeight = 1.6;
constexpr juce::uint32 reviewCorrect = 0xff2e9e4f;
constexpr juce::uint32 reviewWrong = 0xffd23c3c;
constexpr juce::uint32 reviewTiming = 0xffe08a1e;

// How strongly the marks are drawn over the notation, which shows through.
constexpr float soundingAlpha = 0.45f;
constexpr float upcomingAlpha = 0.85f;
constexpr float bandAlpha = 0.16f;
constexpr float cursorLineAlpha = 0.75f;

[[nodiscard]] bool
marks(const std::vector<engraving::TargetNote>& notes, std::size_t part, const score::NoteRef& note)
{
    return std::any_of(
        notes.begin(), notes.end(), [part, &note](const engraving::TargetNote& target)
        { return target.part == part && target.note == note; });
}
} // namespace

void ScoreViewComponent::WatchedViewport::visibleAreaChanged(const juce::Rectangle<int>& area)
{
    juce::Viewport::visibleAreaChanged(area);
    if (!owner.scrollingProgrammatically)
    {
        owner.suspendFollowing();
    }
}

ScoreViewComponent::Canvas::Canvas(ScoreViewComponent& ownerToUse) : owner(ownerToUse) {}

void ScoreViewComponent::Canvas::mouseDown(const juce::MouseEvent& event)
{
    juce::ignoreUnused(event);
    owner.grabKeyboardFocus();
}

void ScoreViewComponent::Canvas::mouseUp(const juce::MouseEvent& event)
{
    if (event.mouseWasDraggedSinceMouseDown())
    {
        return;
    }
    if (event.mods.isPopupMenu())
    {
        owner.showNoteMenu(event.position);
        return;
    }
    owner.seekToCanvasPoint(event.position);
}

void ScoreViewComponent::Canvas::paint(juce::Graphics& graphics)
{
    const juce::Rectangle<int> clip = graphics.getClipBounds();

    // Each system is drawn only where it meets what is being repainted: the
    // cursor repaints one or two systems sixty times a second while the clock
    // plays, and a long score in Vertical scroll should not cost all of them.
    const auto drawSystems = [this, &graphics, &clip](std::size_t first, std::size_t last)
    {
        for (std::size_t index = first; index <= last && index < owner.layout.systems.size();
             ++index)
        {
            const std::optional<SystemOnCanvas> where = owner.systemOnCanvas(index);
            if (!where.has_value() || !where->bounds.intersects(clip))
            {
                continue;
            }
            const ScoreGlyphPainter painter(
                graphics, owner.musicFont, where->pixelsPerSpace,
                scoreNotationPaletteFor(owner.currentTheme));
            painter.drawSystem(owner.layout.systems.at(index), where->originX, where->originY);
            owner.paintCursorOn(graphics, index, *where);
            owner.paintReviewOn(graphics, index, *where);
        }
    };

    if (owner.settings.view != score_view::ViewMode::page)
    {
        if (!owner.layout.systems.empty())
        {
            drawSystems(0, owner.layout.systems.size() - 1);
        }

        return;
    }

    if (owner.pages.empty())
    {
        return;
    }

    // The page: a sheet with an edge and a faint shadow, on the panel's colour.
    const AppPalette colours = owner.palette();
    const juce::Rectangle<float> page = owner.pageBounds();

    graphics.fillAll(colours.background);
    graphics.setColour(juce::Colours::black.withAlpha(0.12f));
    graphics.fillRect(page.translated(3.0f, 3.0f));
    graphics.setColour(colours.panel);
    graphics.fillRect(page);
    graphics.setColour(colours.outline);
    graphics.drawRect(page, 1.0f);

    const engraving::Page& shown =
        owner.pages.at(std::min(owner.currentPage, owner.pages.size() - 1));
    drawSystems(shown.firstSystem, shown.lastSystem);
}

std::optional<ScoreViewComponent::SystemOnCanvas>
ScoreViewComponent::systemOnCanvas(std::size_t index) const
{
    if (layout.outcome != engraving::LayoutOutcome::laidOut || index >= layout.systems.size())
    {
        return std::nullopt;
    }

    const engraving::System& system = layout.systems.at(index);
    const double px = pixelsPerSpace();
    SystemOnCanvas where;
    double top = 0.0;
    double bottom = 0.0;

    if (settings.view == score_view::ViewMode::page)
    {
        if (pages.empty())
        {
            return std::nullopt;
        }
        const engraving::Page& shown = pages.at(std::min(currentPage, pages.size() - 1));
        if (index < shown.firstSystem || index > shown.lastSystem)
        {
            return std::nullopt;
        }

        // A page whose one system is larger than the page, in either
        // direction, is drawn just small enough to show all of it: nothing on
        // a page is ever cut off. Every other page is drawn at the chosen size.
        const juce::Rectangle<float> content = pageContent();
        const double scale = engraving::fitScale(
            shown, layout.systems, static_cast<double>(content.getHeight()) / px,
            static_cast<double>(content.getWidth()) / px);
        where.pixelsPerSpace = px * scale;
        where.originX = static_cast<double>(content.getX());
        const double pageTop = static_cast<double>(content.getY());
        where.originY = pageTop + ((system.topOfStaff - shown.top) * where.pixelsPerSpace);
        top = pageTop + ((system.bandTop - shown.top) * where.pixelsPerSpace);
        bottom = pageTop + ((system.bandBottom - shown.top) * where.pixelsPerSpace);
    }
    else
    {
        where.pixelsPerSpace = px;
        where.originX = static_cast<double>(canvasMargin);
        where.originY = static_cast<double>(canvasMargin) + (system.topOfStaff * px);
        top = static_cast<double>(canvasMargin) + (system.bandTop * px);
        bottom = static_cast<double>(canvasMargin) + (system.bandBottom * px);
    }

    // The band, or the staves with room for a mark beyond the outer lines if
    // they reach further: the cursor is drawn across the staves either way.
    const double staffSpan = (system.staffOffsets.empty() ? 0.0 : system.staffOffsets.back()) +
                             engraving::staffHeightInSpaces;
    top = std::min(top, where.originY - (2.0 * where.pixelsPerSpace));
    bottom = std::max(bottom, where.originY + ((staffSpan + 2.0) * where.pixelsPerSpace));
    const double width = (system.width + 4.0) * where.pixelsPerSpace;

    where.bounds = juce::Rectangle<double>(0.0, top, where.originX + width, bottom - top)
                       .getSmallestIntegerContainer()
                       .expanded(2);
    return where;
}

void ScoreViewComponent::paintCursorOn(
    juce::Graphics& graphics,
    std::size_t index,
    const SystemOnCanvas& where) const
{
    if (!cursor.has_value())
    {
        return;
    }

    const engraving::System& system = layout.systems.at(index);
    const AppPalette colours = palette();
    const double px = where.pixelsPerSpace;
    const double noteheadMiddle = engraving::LayoutOptions{}.noteheadWidth / 2.0;
    const double staffSpan = (system.staffOffsets.empty() ? 0.0 : system.staffOffsets.back()) +
                             engraving::staffHeightInSpaces;

    if (cursor->system == index)
    {
        // Centred on a notehead's middle, since an anchor is a head's left
        // edge: at an onset the band sits on its notes.
        const double middle = where.originX + ((cursor->x + noteheadMiddle) * px);
        const auto top = static_cast<float>(where.originY - (1.5 * px));
        const auto height = static_cast<float>((staffSpan + 3.0) * px);
        const auto bandWidth = static_cast<float>(cursorBandWidth * px);

        graphics.setColour(colours.accent.withAlpha(bandAlpha));
        graphics.fillRect(juce::Rectangle<float>(
            static_cast<float>(middle) - (bandWidth * 0.5f), top, bandWidth, height));
        graphics.setColour(colours.accent.withAlpha(cursorLineAlpha));
        graphics.fillRect(
            juce::Rectangle<float>(static_cast<float>(middle) - 1.0f, top, 2.0f, height));
    }

    if (targets.sounding.empty() && targets.upcoming.empty())
    {
        return;
    }

    const auto ring = static_cast<float>(std::max(1.5, 0.16 * px));

    for (const engraving::NoteheadGlyph& head : system.noteheads)
    {
        if (head.isGrace)
        {
            continue;
        }
        const auto staff = static_cast<std::size_t>(std::max(0, head.staff));
        if (staff >= system.staffParts.size())
        {
            continue;
        }
        const std::size_t part = system.staffParts.at(staff);
        const bool sounding = marks(targets.sounding, part, head.source);
        const bool upcoming = !sounding && marks(targets.upcoming, part, head.source);
        if (!sounding && !upcoming)
        {
            continue;
        }

        const double offset =
            staff < system.staffOffsets.size() ? system.staffOffsets.at(staff) : 0.0;
        const double centreX = where.originX + ((head.x + noteheadMiddle) * px);
        const double centreY =
            where.originY + ((offset + engraving::spacesBelowTopLine(head.position)) * px);
        const juce::Rectangle<float> mark(
            static_cast<float>(centreX - (markWidth * px * 0.5)),
            static_cast<float>(centreY - (markHeight * px * 0.5)),
            static_cast<float>(markWidth * px), static_cast<float>(markHeight * px));

        if (sounding)
        {
            graphics.setColour(colours.accent.withAlpha(soundingAlpha));
            graphics.fillEllipse(mark);
        }
        else
        {
            graphics.setColour(colours.accent.withAlpha(upcomingAlpha));
            graphics.drawEllipse(mark, ring);
        }
    }
}

void ScoreViewComponent::paintReviewOn(
    juce::Graphics& graphics,
    std::size_t index,
    const SystemOnCanvas& where) const
{
    if (reviewMarks.notes.empty() && reviewMarks.extras.empty())
    {
        return;
    }

    const engraving::System& system = layout.systems.at(index);
    const double px = where.pixelsPerSpace;
    const double noteheadMiddle = engraving::LayoutOptions{}.noteheadWidth / 2.0;
    const auto ring = static_cast<float>(std::max(1.5, 0.18 * px));
    const juce::Colour muted = palette().muted;

    for (const engraving::NoteMark& mark : reviewMarks.notes)
    {
        if (mark.system != index || mark.notehead >= system.noteheads.size())
        {
            continue;
        }
        const engraving::NoteheadGlyph& head = system.noteheads.at(mark.notehead);
        const auto staff = static_cast<std::size_t>(std::max(0, head.staff));
        const double offset =
            staff < system.staffOffsets.size() ? system.staffOffsets.at(staff) : 0.0;
        const double centreX = where.originX + ((head.x + noteheadMiddle) * px);
        const double centreY =
            where.originY + ((offset + engraving::spacesBelowTopLine(head.position)) * px);
        const juce::Rectangle<float> around(
            static_cast<float>(centreX - (reviewMarkWidth * px * 0.5)),
            static_cast<float>(centreY - (reviewMarkHeight * px * 0.5)),
            static_cast<float>(reviewMarkWidth * px), static_cast<float>(reviewMarkHeight * px));

        switch (mark.pitch)
        {
        case engraving::PitchMark::correct:
            graphics.setColour(juce::Colour(reviewCorrect));
            graphics.drawEllipse(around, ring);
            break;
        case engraving::PitchMark::acceptable:
            // In tune, within the singer's range but not near perfect.
            graphics.setColour(intonationZoneColour(pitch::IntonationZone::acceptable));
            graphics.drawEllipse(around, ring);
            break;
        case engraving::PitchMark::wrong:
            graphics.setColour(juce::Colour(reviewWrong).withAlpha(0.5f));
            graphics.fillEllipse(around);
            graphics.setColour(juce::Colour(reviewWrong));
            graphics.drawEllipse(around, ring);
            if (mark.playedMidi.has_value())
            {
                // What was played, on a tab above the head and to its right,
                // clear of the chord it is in.
                const auto height = static_cast<float>(std::max(11.0, 1.7 * px));
                const juce::Font font(juce::FontOptions(height, juce::Font::bold));
                const juce::String name(evaluation::keyName(*mark.playedMidi));
                const float width =
                    juce::GlyphArrangement::getStringWidth(font, name) + (height * 0.5f);
                const juce::Rectangle<float> tab(
                    around.getRight() - (height * 0.2f), around.getY() - (height * 0.9f), width,
                    height * 1.1f);
                graphics.setColour(palette().panel.withAlpha(0.92f));
                graphics.fillRoundedRectangle(tab, height * 0.25f);
                graphics.setColour(juce::Colour(reviewWrong));
                graphics.drawRoundedRectangle(tab, height * 0.25f, 1.0f);
                graphics.setFont(font);
                graphics.drawText(name, tab, juce::Justification::centred, false);
            }
            break;
        case engraving::PitchMark::missed:
            graphics.setColour(muted);
            graphics.drawEllipse(around, ring);
            break;
        case engraving::PitchMark::sharp:
        case engraving::PitchMark::flat:
        {
            // The right note, off: an amber ring, and an upright arrow above
            // a sharp head or below a flat one, with how far off beside it.
            const bool sharp = mark.pitch == engraving::PitchMark::sharp;
            const juce::Colour amber(reviewTiming);
            graphics.setColour(amber);
            graphics.drawEllipse(around, ring);

            const double side = sharp ? -1.0 : 1.0;
            const double base = centreY + (side * ((reviewMarkHeight * 0.5) + 0.3) * px);
            const double tip = base + (side * 1.0 * px);
            const double half = 0.45 * px;
            juce::Path arrow;
            arrow.addTriangle(
                static_cast<float>(centreX - half), static_cast<float>(base),
                static_cast<float>(centreX + half), static_cast<float>(base),
                static_cast<float>(centreX), static_cast<float>(tip));
            graphics.fillPath(arrow);

            if (mark.cents.has_value())
            {
                const auto height = static_cast<float>(std::max(10.0, 1.4 * px));
                const juce::String text = juce::String::fromUTF8(sharp ? "+" : "\xE2\x88\x92") +
                                          juce::String(std::lround(std::abs(*mark.cents)));
                graphics.setFont(juce::Font(juce::FontOptions(height, juce::Font::bold)));
                const auto top = static_cast<float>(std::min(base, tip));
                const juce::Rectangle<float> label(
                    static_cast<float>(centreX + half + (0.1 * px)), top - (height * 0.15f),
                    height * 2.6f, height);
                graphics.drawText(text, label, juce::Justification::centredLeft, false);
            }
            break;
        }
        case engraving::PitchMark::uncertain:
            graphics.setColour(muted.withAlpha(0.45f));
            graphics.drawEllipse(around, ring * 0.6f);
            break;
        case engraving::PitchMark::none:
            break;
        }

        if (mark.timing != engraving::TimingMark::none)
        {
            // A small arrow beside the head, pointing the way it was off.
            const bool early = mark.timing == engraving::TimingMark::early;
            const double side = early ? -1.0 : 1.0;
            const double base = centreX + (side * ((reviewMarkWidth * 0.5) + 0.35) * px);
            const double tip = base + (side * 0.9 * px);
            const double half = 0.45 * px;
            juce::Path arrow;
            arrow.addTriangle(
                static_cast<float>(base), static_cast<float>(centreY - half),
                static_cast<float>(base), static_cast<float>(centreY + half),
                static_cast<float>(tip), static_cast<float>(centreY));
            graphics.setColour(juce::Colour(reviewTiming));
            graphics.fillPath(arrow);
        }
    }

    // An extra note: a cross above the top staff, at its time.
    const auto stroke = static_cast<float>(std::max(1.5, 0.22 * px));
    for (const engraving::ExtraMark& extra : reviewMarks.extras)
    {
        if (extra.system != index)
        {
            continue;
        }
        const auto x = static_cast<float>(where.originX + ((extra.x + noteheadMiddle) * px));
        const auto y = static_cast<float>(where.originY - (2.6 * px));
        const auto arm = static_cast<float>(0.55 * px);
        graphics.setColour(juce::Colour(reviewWrong));
        graphics.drawLine(x - arm, y - arm, x + arm, y + arm, stroke);
        graphics.drawLine(x - arm, y + arm, x + arm, y - arm, stroke);
    }
}

ScoreViewComponent::ScoreViewComponent(
    const score::CurrentScore& sharedCurrentScore,
    transport::PracticeClock& practiceClock,
    take::TakeRecorder& takeRecorder,
    pitch::CurrentTarget& applicationTarget)
    : currentScore(sharedCurrentScore), clock(practiceClock), takes(takeRecorder),
      target(applicationTarget)
{
    setComponentID("score-view-tool");
    setWantsKeyboardFocus(true);

    viewport.setViewedComponent(&canvas, false);
    viewport.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport);

    const auto setUpButton = [this](
                                 juce::TextButton& button, const char* identifier,
                                 const char* tooltip, std::function<void()> action)
    {
        button.setComponentID(identifier);
        button.setTooltip(tooltip);
        button.setTitle(tooltip);
        button.setWantsKeyboardFocus(false);
        button.onClick = std::move(action);
        addAndMakeVisible(button);
    };

    setUpButton(
        shrinkButton, "score-view-shrink", "Shrink the music",
        [this]
        {
            if (settings.sizeStep > 0)
            {
                setSizeStep(settings.sizeStep - 1);
            }
        });
    setUpButton(
        enlargeButton, "score-view-enlarge", "Enlarge the music",
        [this] { setSizeStep(settings.sizeStep + 1); });
    setUpButton(
        previousPageButton, "score-view-previous-page", "Previous page",
        [this]
        {
            if (currentPage > 0)
            {
                turnToPage(currentPage - 1);
            }
        });
    setUpButton(
        nextPageButton, "score-view-next-page", "Next page",
        [this] { turnToPage(currentPage + 1); });
    setUpButton(
        followButton, "score-view-follow", "Follow the practice clock again",
        [this] { resumeFollowing(); });
    followButton.setVisible(false);

    for (juce::Label* label : {&sizeLabel, &pageLabel})
    {
        label->setJustificationType(juce::Justification::centred);
        label->setFont(juce::FontOptions(12.0f));
        label->setInterceptsMouseClicks(false, false);
        addAndMakeVisible(*label);
    }

    refreshFromCurrentScore();
    startTimerHz(refreshRateHz);

    // Told where the clock is straight away, so a Score View opened mid-take
    // shows the cursor without waiting for the clock to move.
    clockPosition = clock.position();
    clock.addListener(this);
    updateCursor();

    takes.addListener(this);
    updateReview();
}

ScoreViewComponent::~ScoreViewComponent()
{
    takes.removeListener(this);
    clock.removeListener(this);
    stopTimer();
}

double ScoreViewComponent::pixelsPerSpace() const
{
    return score_view::sizeLadder.at(
        std::min(settings.sizeStep, score_view::sizeLadder.size() - 1));
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

    // A different score starts at its beginning.
    currentPage = 0;
    relayout(std::nullopt, true);
    scrollProgrammatically(0, 0);

    // The clock may already be somewhere in it -- moved the moment it opened,
    // before this noticed -- and following brings that into view.
    if (settings.follow)
    {
        follow();
    }

    repaint();
}

juce::Rectangle<float> ScoreViewComponent::pageBounds() const
{
    if (settings.pageFit == score_view::PageFit::page)
    {
        // The whole page in the panel: as tall as the panel allows, A4-shaped,
        // centred, and narrower than the panel only when the panel is tall
        // and thin (score-cursor design.md decision 7).
        const auto available =
            static_cast<float>(std::max(0, viewport.getWidth() - viewport.getScrollBarThickness()));
        const float roomWidth = std::max(0.0f, available - (pageSurround * 2.0f));
        float pageHeight =
            std::max(0.0f, static_cast<float>(viewport.getHeight()) - (pageSurround * 2.0f));
        float pageWidth = pageHeight / static_cast<float>(pageAspect);
        if (pageWidth > roomWidth)
        {
            pageWidth = roomWidth;
            pageHeight = pageWidth * static_cast<float>(pageAspect);
        }
        return {
            pageSurround + ((roomWidth - pageWidth) * 0.5f), pageSurround, pageWidth, pageHeight};
    }

    // As wide as the panel allows, less room for a vertical scrollbar so the
    // page does not change size when one appears; A4-shaped, so as tall as
    // that makes it. A page taller than the panel scrolls within itself --
    // fitting the whole page into a short docked panel instead made it a few
    // hundred pixels wide, with one bar to a line.
    const auto width =
        static_cast<float>(std::max(0, viewport.getWidth() - viewport.getScrollBarThickness()));
    const float pageWidth = std::max(0.0f, width - (pageSurround * 2.0f));
    const float pageHeight = pageWidth * static_cast<float>(pageAspect);

    return {pageSurround, pageSurround, pageWidth, pageHeight};
}

juce::Rectangle<float> ScoreViewComponent::pageContent() const
{
    const juce::Rectangle<float> page = pageBounds();

    return page.reduced(page.getWidth() * pageMarginFraction);
}

void ScoreViewComponent::relayout(std::optional<std::size_t> keep, bool force)
{
    const int width = viewport.getWidth();
    const int height = viewport.getHeight();

    if (width <= 0 || height <= 0)
    {
        return;
    }

    const LayoutKey key{
        width, height, settings.view, settings.sizeStep, settings.hiddenParts, settings.pageFit};

    if (!force && key == laidOutFor)
    {
        return;
    }

    laidOutFor = key;

    // Everything below moves the view as a consequence of laying out, which
    // is not the reader scrolling.
    const juce::ScopedValueSetter<bool> quiet(scrollingProgrammatically, true);

    const double px = pixelsPerSpace();
    engraving::LayoutOptions options;
    options.hiddenParts = settings.hiddenParts;

    switch (settings.view)
    {
    case score_view::ViewMode::vertical:
        options.availableWidth =
            std::max(24.0, static_cast<double>(width - (canvasMargin * 2)) / px);
        break;

    case score_view::ViewMode::horizontal:
        options.availableWidth = unboundedWidth;
        break;

    case score_view::ViewMode::page:
        options.availableWidth = std::max(24.0, static_cast<double>(pageContent().getWidth()) / px);
        break;
    }

    layout = engraving::layOutScore(renderedScore, options);
    pages.clear();

    if (settings.view == score_view::ViewMode::page)
    {
        // A system too tall for the page, or holding a bar too wide for it,
        // goes on the next page by itself and that page is drawn smaller: no
        // bar is cut off at the page's foot or its right edge.
        pages = engraving::paginate(
            layout.systems, static_cast<double>(pageContent().getHeight()) / px,
            static_cast<double>(pageContent().getWidth()) / px);
        currentPage = pages.empty() ? 0 : std::min(currentPage, pages.size() - 1);

        // One page on the canvas, scrolled vertically when it is taller than
        // the panel; pages are turned, never scrolled between.
        const juce::Rectangle<float> page = pageBounds();
        viewport.setScrollBarsShown(true, false);
        canvas.setSize(
            std::max(0, width - viewport.getScrollBarThickness()),
            std::max(height, static_cast<int>(std::ceil(page.getBottom() + pageSurround))));
    }
    else
    {
        double widest = 0.0;

        for (const engraving::System& system : layout.systems)
        {
            widest = std::max(widest, system.width);
        }

        const int contentWidth = static_cast<int>(std::ceil(widest * px)) + (canvasMargin * 2);
        const int contentHeight =
            static_cast<int>(std::ceil(layout.totalHeight * px)) + (canvasMargin * 2);

        if (settings.view == score_view::ViewMode::horizontal)
        {
            // One long system, scrolled sideways. A vertical scrollbar appears
            // only if the system is taller than the panel, which is why the
            // canvas is the content's height rather than at least the panel's:
            // a canvas exactly as tall as the panel would grow a vertical bar
            // the moment the horizontal one took its room.
            viewport.setScrollBarsShown(true, true);
            canvas.setSize(std::max(width, contentWidth), contentHeight);
        }
        else
        {
            // Wide enough for the viewport, or wider if a single bar overflowed
            // -- a measure that does not fit anywhere still has to go somewhere,
            // and the layout says so by returning a system wider than it was
            // asked for. A horizontal scrollbar then appears, so that bar can be
            // scrolled to rather than cut off at the panel's edge.
            viewport.setScrollBarsShown(true, true);
            canvas.setSize(std::max(width, contentWidth), std::max(height, contentHeight));
        }
    }

    // Hidden rather than left empty when there is nothing to draw: the tool
    // paints a sentence over the whole panel in that case, and an empty
    // scrolling area on top of it would show a scrollbar beside a message.
    viewport.setVisible(layout.outcome == engraving::LayoutOutcome::laidOut);

    if (keep.has_value())
    {
        revealMeasure(*keep);
    }

    updateControls();
    canvas.repaint();
    repaint();

    // The cursor's place, and the review's marks, were on the old layout.
    cursor.reset();
    updateCursor();
    updateReview();
}

std::optional<std::size_t> ScoreViewComponent::measureInView() const
{
    if (layout.outcome != engraving::LayoutOutcome::laidOut || layout.systems.empty())
    {
        return std::nullopt;
    }

    // What is on screen was laid out for `laidOutFor`, which a change of view
    // or size has not reached yet when this is asked.
    const double px =
        score_view::sizeLadder.at(std::min(laidOutFor.sizeStep, score_view::sizeLadder.size() - 1));

    switch (laidOutFor.view)
    {
    case score_view::ViewMode::page:
        if (pages.empty())
        {
            return std::nullopt;
        }

        return layout.systems.at(pages.at(std::min(currentPage, pages.size() - 1)).firstSystem)
            .firstMeasureIndex;

    case score_view::ViewMode::vertical:
    {
        const double top = static_cast<double>(viewport.getViewPositionY());

        for (const engraving::System& system : layout.systems)
        {
            if (static_cast<double>(canvasMargin) + (system.bandBottom * px) > top)
            {
                return system.firstMeasureIndex;
            }
        }

        return layout.systems.back().firstMeasureIndex;
    }

    case score_view::ViewMode::horizontal:
    {
        // The bar whose first note is the first at or right of the left edge.
        const double left = static_cast<double>(viewport.getViewPositionX());
        const engraving::System& system = layout.systems.front();
        double nearest = std::numeric_limits<double>::max();
        std::size_t measure = system.firstMeasureIndex;

        for (const engraving::NoteheadGlyph& head : system.noteheads)
        {
            const double x = static_cast<double>(canvasMargin) + (head.x * px);

            if (x >= left && x < nearest)
            {
                nearest = x;
                measure = head.source.measureIndex;
            }
        }

        return measure;
    }
    }

    return std::nullopt;
}

void ScoreViewComponent::revealMeasure(std::size_t measure)
{
    if (layout.outcome != engraving::LayoutOutcome::laidOut || layout.systems.empty())
    {
        return;
    }

    // The system holding the measure, or the last one starting before it.
    std::size_t holding = 0;

    for (std::size_t index = 0; index < layout.systems.size(); ++index)
    {
        if (layout.systems.at(index).firstMeasureIndex <= measure)
        {
            holding = index;
        }
    }

    const double px = pixelsPerSpace();

    switch (settings.view)
    {
    case score_view::ViewMode::page:
        for (std::size_t page = 0; page < pages.size(); ++page)
        {
            if (pages.at(page).firstSystem <= holding && holding <= pages.at(page).lastSystem)
            {
                currentPage = page;

                // And down the page to the system, which may be below the
                // panel's foot.
                const double scale = engraving::fitScale(
                    pages.at(page), layout.systems,
                    static_cast<double>(pageContent().getHeight()) / px,
                    static_cast<double>(pageContent().getWidth()) / px);
                const double offset =
                    (layout.systems.at(holding).bandTop - pages.at(page).top) * px * scale;
                viewport.setViewPosition(
                    0, static_cast<int>(std::floor(pageContent().getY() + offset)));
            }
        }

        break;

    case score_view::ViewMode::vertical:
        viewport.setViewPosition(
            0, static_cast<int>(std::floor(layout.systems.at(holding).bandTop * px)));
        break;

    case score_view::ViewMode::horizontal:
    {
        const engraving::System& system = layout.systems.at(holding);
        double first = 0.0;
        bool found = false;

        for (const engraving::NoteheadGlyph& head : system.noteheads)
        {
            if (head.source.measureIndex == measure)
            {
                first = found ? std::min(first, head.x) : head.x;
                found = true;
            }
        }

        if (!found)
        {
            break;
        }

        // A little before the bar's first note, so its barline shows too.
        const double x = std::max(0.0, (first - 3.0) * px);
        viewport.setViewPosition(static_cast<int>(std::floor(x)), viewport.getViewPositionY());
        break;
    }
    }
}

void ScoreViewComponent::setView(score_view::ViewMode view)
{
    if (view == settings.view)
    {
        return;
    }

    const std::optional<std::size_t> keep = measureInView();
    settings.view = view;
    relayout(keep);
}

void ScoreViewComponent::setSizeStep(std::size_t step)
{
    const std::size_t clamped = std::min(step, score_view::sizeLadder.size() - 1);

    if (clamped == settings.sizeStep)
    {
        return;
    }

    const std::optional<std::size_t> keep = measureInView();
    settings.sizeStep = clamped;
    relayout(keep);
}

void ScoreViewComponent::togglePart(const engraving::PartKey& part)
{
    const std::optional<std::size_t> keep = measureInView();
    std::vector<engraving::PartKey>& hidden = settings.hiddenParts;

    if (const auto found = std::find(hidden.begin(), hidden.end(), part); found != hidden.end())
    {
        hidden.erase(found);
    }
    else
    {
        hidden.push_back(part);
    }

    relayout(keep);
}

std::optional<ToolComponent::MenuEntry> ScoreViewComponent::partsMenuEntry()
{
    if (renderedScore == nullptr)
    {
        return std::nullopt;
    }

    const std::vector<engraving::PartKey> drawable = engraving::drawablePartsOf(*renderedScore);

    if (drawable.size() < 2)
    {
        return std::nullopt;
    }

    const auto isHidden = [this](const engraving::PartKey& part)
    {
        return std::find(settings.hiddenParts.begin(), settings.hiddenParts.end(), part) !=
               settings.hiddenParts.end();
    };

    // What the layout actually draws: a selection hiding everything is
    // ignored there, so it is shown as nothing hidden here (decision 2).
    const auto hiddenCount =
        static_cast<std::size_t>(std::count_if(drawable.begin(), drawable.end(), isHidden));
    const bool selectionApplies = hiddenCount < drawable.size();
    const std::size_t shownCount =
        selectionApplies ? drawable.size() - hiddenCount : drawable.size();

    MenuEntry heading{"Parts", {}};

    for (const engraving::PartKey& part : drawable)
    {
        const bool shown = !selectionApplies || !isHidden(part);

        MenuEntry entry{
            juce::String(engraving::displayedPartName(part.name)),
            [this, part] { togglePart(part); }, shown};

        // The last part shown cannot be hidden: an empty page is never the
        // answer (decision 5).
        entry.enabled = !(shown && shownCount == 1);
        heading.submenu.push_back(std::move(entry));
    }

    return heading;
}

void ScoreViewComponent::turnToPage(std::size_t page)
{
    if (pages.empty())
    {
        return;
    }

    // The reader turning pages while the clock plays is looking elsewhere on
    // purpose (decision 5).
    suspendFollowing();

    currentPage = std::min(page, pages.size() - 1);

    // A turned page is read from its top.
    scrollProgrammatically(0, 0);
    updateControls();
    canvas.repaint();
}

void ScoreViewComponent::updateControls()
{
    const bool laidOut = layout.outcome == engraving::LayoutOutcome::laidOut;
    const bool paged = laidOut && settings.view == score_view::ViewMode::page && !pages.empty();

    for (juce::Component* control :
         {static_cast<juce::Component*>(&shrinkButton), static_cast<juce::Component*>(&sizeLabel),
          static_cast<juce::Component*>(&enlargeButton)})
    {
        control->setVisible(laidOut);
    }

    for (juce::Component* control :
         {static_cast<juce::Component*>(&previousPageButton),
          static_cast<juce::Component*>(&pageLabel),
          static_cast<juce::Component*>(&nextPageButton)})
    {
        control->setVisible(paged);
    }

    shrinkButton.setEnabled(settings.sizeStep > 0);
    enlargeButton.setEnabled(settings.sizeStep + 1 < score_view::sizeLadder.size());

    const double percent =
        100.0 * pixelsPerSpace() / score_view::sizeLadder.at(score_view::defaultSizeStep);
    sizeLabel.setText(juce::String(juce::roundToInt(percent)) + "%", juce::dontSendNotification);

    // Follow, when following is on but suspended while the clock plays. The
    // title strip makes room for it, so it is laid out again when it shows.
    const bool offerFollow = laidOut && settings.follow && followSuspended &&
                             clockPosition.state == transport::ClockState::playing;
    if (followButton.isVisible() != offerFollow)
    {
        followButton.setVisible(offerFollow);
        resized();
    }

    previousPageButton.setEnabled(currentPage > 0);
    nextPageButton.setEnabled(currentPage + 1 < pages.size());
    pageLabel.setText(
        "Page " + juce::String(static_cast<int>(currentPage) + 1) + " of " +
            juce::String(static_cast<int>(pages.size())),
        juce::dontSendNotification);

    const AppPalette colours = palette();

    for (juce::Label* label : {&sizeLabel, &pageLabel})
    {
        label->setColour(juce::Label::textColourId, colours.foreground);
    }
}

void ScoreViewComponent::resized()
{
    juce::Rectangle<int> bounds = getLocalBounds();

    juce::Rectangle<int> title = bounds.removeFromTop(titleStripHeight).reduced(canvasMargin, 2);

    // Layout and resizing move the view; neither is the reader scrolling.
    const juce::ScopedValueSetter<bool> quiet(scrollingProgrammatically, true);

    enlargeButton.setBounds(title.removeFromRight(controlButtonWidth));
    sizeLabel.setBounds(title.removeFromRight(sizeLabelWidth));
    shrinkButton.setBounds(title.removeFromRight(controlButtonWidth));
    title.removeFromRight(controlGap);
    nextPageButton.setBounds(title.removeFromRight(controlButtonWidth));
    pageLabel.setBounds(title.removeFromRight(pageLabelWidth));
    previousPageButton.setBounds(title.removeFromRight(controlButtonWidth));
    title.removeFromRight(controlGap);
    if (followButton.isVisible())
    {
        followButton.setBounds(title.removeFromRight(followButtonWidth));
        title.removeFromRight(controlGap);
    }
    titleTextBounds = title;

    // Reserved whether or not there is anything in it, so that opening a score
    // does not move the staff. A strip that appeared only when a notice did
    // would reflow everything the moment a file finished loading, and which
    // files have notices is a property of the files rather than of the tool.
    bounds.removeFromBottom(noticeStripHeight);

    const std::optional<std::size_t> keep = measureInView();
    viewport.setBounds(bounds);

    // The room changed, so the systems break differently.
    relayout(keep);
}

bool ScoreViewComponent::keyPressed(const juce::KeyPress& key)
{
    const int code = key.getKeyCode();
    const juce::juce_wchar character = key.getTextCharacter();

    if (key.getModifiers().isCommandDown())
    {
        if (code == '=' || code == '+' || character == '=' || character == '+')
        {
            setSizeStep(settings.sizeStep + 1);
            return true;
        }

        if (code == '-' || character == '-' || character == '_')
        {
            if (settings.sizeStep > 0)
            {
                setSizeStep(settings.sizeStep - 1);
            }

            return true;
        }

        if (code == '0' || character == '0')
        {
            setSizeStep(score_view::defaultSizeStep);
            return true;
        }

        return false;
    }

    if (settings.view != score_view::ViewMode::page || pages.empty())
    {
        return false;
    }

    // Up and down are left to scroll the page itself.
    if (code == juce::KeyPress::pageDownKey || code == juce::KeyPress::rightKey)
    {
        turnToPage(currentPage + 1);
        return true;
    }

    if (code == juce::KeyPress::pageUpKey || code == juce::KeyPress::leftKey)
    {
        if (currentPage > 0)
        {
            turnToPage(currentPage - 1);
        }

        return true;
    }

    if (code == juce::KeyPress::homeKey)
    {
        turnToPage(0);
        return true;
    }

    if (code == juce::KeyPress::endKey)
    {
        turnToPage(pages.size() - 1);
        return true;
    }

    return false;
}

void ScoreViewComponent::setTheme(Theme theme)
{
    currentTheme = theme;
    updateControls();
    canvas.repaint();
    repaint();
}

void ScoreViewComponent::resetToDefaults()
{
    settings = score_view::Settings{};

    // Which score is shown is the application's business rather than a
    // preference, so the honest thing a reset can do is re-read the holder and
    // lay it out afresh.
    renderedScore = nullptr;
    refreshFromCurrentScore();
    relayout(std::nullopt, true);
    repaint();
}

std::optional<ToolSettingsPayload> ScoreViewComponent::captureSettings() const
{
    return score_view::encode(settings);
}

void ScoreViewComponent::applySettings(const ToolSettingsPayload& payload)
{
    const std::optional<std::size_t> keep = measureInView();
    settings = score_view::decode(payload);
    relayout(keep);

    // The look-ahead and the review are not part of the layout, so a relayout
    // that found nothing changed did not reach the marks.
    updateCursor();
    updateReview();
}

bool ScoreViewComponent::showView(const juce::String& view)
{
    // A bare view name, or -- for a state that needs more than a view, such as
    // a part hidden -- a whole settings payload, told apart by its `=`
    // (score-part-selection design.md decision 8).
    score_view::Settings wanted;

    if (view.containsChar('='))
    {
        wanted = score_view::decode({score_view::settingsVersion, view.toStdString()});
    }
    else if (
        const std::optional<score_view::ViewMode> named = score_view::viewNamed(view.toStdString()))
    {
        wanted.view = *named;
    }
    else
    {
        return false;
    }

    // For verification, so the result must not depend on what was asked
    // before: the shell does not reset a tool it is about to put in a named
    // view, and a size left over from the previous state would make the same
    // state photograph two ways.
    wanted.sizeStep = score_view::defaultSizeStep;

    const std::optional<std::size_t> keep = measureInView();
    settings = wanted;
    relayout(keep);

    return true;
}

void ScoreViewComponent::clockChanged(const transport::ClockUpdate& update)
{
    const bool wasPlaying = clockPosition.state == transport::ClockState::playing;
    clockPosition = update.position;
    const bool playing = clockPosition.state == transport::ClockState::playing;

    // Starting the clock again is asking to be followed again.
    if (playing && !wasPlaying)
    {
        followSuspended = false;
    }

    updateCursor();

    // While it plays; and when it is moved, by a seek from anywhere, so a bar
    // chosen elsewhere is brought into view here too.
    if (settings.follow && !followSuspended && (playing || update.seeks > 0))
    {
        follow();
    }

    updateControls();
}

void ScoreViewComponent::updateCursor()
{
    std::optional<engraving::CursorPlace> place;
    engraving::Targets marked;

    if (renderedScore != nullptr && layout.outcome == engraving::LayoutOutcome::laidOut &&
        clockPosition.hasScore)
    {
        place = engraving::placeOfTick(layout, clockPosition.tick);
        marked = engraving::targetsAt(
            *renderedScore, engraving::drawnPartsOf(layout), clockPosition.tick,
            settings.lookAhead);
    }

    if (place == cursor && marked.sounding == targets.sounding &&
        marked.upcoming == targets.upcoming)
    {
        return;
    }

    // Only the systems something moved on or off: the cursor's old and new
    // systems, and those holding a note marked before or after.
    std::vector<std::size_t> touched;
    const auto touch = [&touched](std::size_t index)
    {
        if (std::find(touched.begin(), touched.end(), index) == touched.end())
        {
            touched.push_back(index);
        }
    };
    const auto touchMeasure = [this, &touch](std::size_t measure)
    {
        for (std::size_t index = 0; index < layout.systems.size(); ++index)
        {
            const engraving::System& system = layout.systems.at(index);
            if (system.firstMeasureIndex <= measure && measure <= system.lastMeasureIndex)
            {
                touch(index);
            }
        }
    };
    for (const engraving::Targets* group : {&targets, &marked})
    {
        for (const auto* notes : {&group->sounding, &group->upcoming})
        {
            for (const engraving::TargetNote& note : *notes)
            {
                touchMeasure(note.note.measureIndex);
            }
        }
    }
    if (cursor.has_value())
    {
        touch(cursor->system);
    }
    if (place.has_value())
    {
        touch(place->system);
    }

    cursor = place;
    targets = std::move(marked);

    for (const std::size_t index : touched)
    {
        if (const std::optional<SystemOnCanvas> where = systemOnCanvas(index))
        {
            canvas.repaint(where->bounds);
        }
    }
}

void ScoreViewComponent::scrollProgrammatically(int x, int y)
{
    const juce::ScopedValueSetter<bool> quiet(scrollingProgrammatically, true);
    viewport.setViewPosition(x, y);
}

void ScoreViewComponent::follow()
{
    if (!cursor.has_value() || layout.outcome != engraving::LayoutOutcome::laidOut)
    {
        return;
    }

    const std::size_t index = cursor->system;

    if (settings.view == score_view::ViewMode::page)
    {
        // Turn to the page holding the cursor, the moment it gets there.
        for (std::size_t page = 0; page < pages.size(); ++page)
        {
            if (pages.at(page).firstSystem <= index && index <= pages.at(page).lastSystem &&
                page != currentPage)
            {
                currentPage = page;
                scrollProgrammatically(0, 0);
                updateControls();
                canvas.repaint();
                break;
            }
        }
    }

    const std::optional<SystemOnCanvas> where = systemOnCanvas(index);
    if (!where.has_value())
    {
        return;
    }

    const juce::Rectangle<int> view = viewport.getViewArea();

    switch (settings.view)
    {
    case score_view::ViewMode::page:
    case score_view::ViewMode::vertical:
    {
        // A step when the system is not wholly in view: brought to a quarter
        // of the way down in Vertical scroll, to the top of the panel on a tall
        // page -- the page is read top to bottom, so there is nothing above it
        // worth keeping.
        if (where->bounds.getY() >= view.getY() && where->bounds.getBottom() <= view.getBottom())
        {
            break;
        }
        const double place = settings.view == score_view::ViewMode::vertical
                                 ? static_cast<double>(view.getHeight()) * verticalFollowPlace
                                 : 8.0;
        scrollProgrammatically(
            view.getX(), std::max(0, where->bounds.getY() - static_cast<int>(place)));
        break;
    }

    case score_view::ViewMode::horizontal:
    {
        const double x = where->originX + (cursor->x * where->pixelsPerSpace);
        const double left = static_cast<double>(view.getX());
        const double width = static_cast<double>(view.getWidth());
        if (x >= left && x <= left + (width * horizontalFollowEdge))
        {
            break;
        }
        scrollProgrammatically(
            std::max(0, static_cast<int>(x - (width * horizontalFollowRestart))), view.getY());
        break;
    }
    }
}

void ScoreViewComponent::suspendFollowing()
{
    if (!settings.follow || followSuspended ||
        clockPosition.state != transport::ClockState::playing)
    {
        return;
    }
    followSuspended = true;
    updateControls();
}

void ScoreViewComponent::resumeFollowing()
{
    followSuspended = false;
    follow();
    updateControls();
}

void ScoreViewComponent::seekToCanvasPoint(juce::Point<float> point)
{
    for (std::size_t index = 0; index < layout.systems.size(); ++index)
    {
        const std::optional<SystemOnCanvas> where = systemOnCanvas(index);
        if (!where.has_value() || !where->bounds.toFloat().contains(point))
        {
            continue;
        }
        const double x = (static_cast<double>(point.x) - where->originX) / where->pixelsPerSpace;
        if (const std::optional<score::Tick> tick =
                engraving::onsetAtOrBefore(layout.systems.at(index), x))
        {
            clock.seek(*tick);
        }
        return;
    }
}

void ScoreViewComponent::showNoteMenu(juce::Point<float> point)
{
    juce::PopupMenu menu;
    for (std::size_t index = 0; index < layout.systems.size() && renderedScore != nullptr; ++index)
    {
        const std::optional<SystemOnCanvas> where = systemOnCanvas(index);
        if (!where.has_value() || !where->bounds.toFloat().contains(point))
        {
            continue;
        }
        const engraving::System& system = layout.systems.at(index);
        const double x = (static_cast<double>(point.x) - where->originX) / where->pixelsPerSpace;
        const double y = (static_cast<double>(point.y) - where->originY) / where->pixelsPerSpace;
        const std::optional<std::size_t> head = engraving::noteheadAt(system, x, y);
        if (!head.has_value())
        {
            break;
        }
        const engraving::NoteheadGlyph& glyph = system.noteheads.at(*head);
        const auto staff = static_cast<std::size_t>(std::max(0, glyph.staff));
        if (staff >= system.staffParts.size() ||
            system.staffParts.at(staff) >= renderedScore->parts.size())
        {
            break;
        }
        const score::Part& part = renderedScore->parts.at(system.staffParts.at(staff));
        const score::NoteRef& ref = glyph.source;
        if (ref.measureIndex >= part.measures.size() ||
            ref.voiceIndex >= part.measures[ref.measureIndex].voices.size() ||
            ref.eventIndex >= part.measures[ref.measureIndex].voices[ref.voiceIndex].events.size())
        {
            break;
        }
        const score::Measure& measure = part.measures[ref.measureIndex];
        const score::ScoreEvent& event = measure.voices[ref.voiceIndex].events[ref.eventIndex];
        const std::string source =
            (part.name.empty() ? part.id : part.name) + ", bar " + measure.printedNumber;

        // Every note of a chord: which one is the player's choice.
        for (const score::Note& note : event.notes)
        {
            juce::String label = "Aim at " + juce::String(score::spelledName(note.written));
            if (!score::soundsAsWritten(note))
            {
                label += " (sounds " + juce::String(score::spelledName(note.sounding)) + ")";
            }
            menu.addItem(
                label, [this, note, source]
                { target.publish(pitch::chosenTarget(note.sounding, note.written, source)); });
        }
        break;
    }

    menu.addItem("Move the clock here", [this, point] { seekToCanvasPoint(point); });
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&canvas).withMousePosition());
}

void ScoreViewComponent::showGoToBar()
{
    auto* window = new juce::AlertWindow(
        "Go to bar", "The bar's number, as printed in the score.", juce::MessageBoxIconType::NoIcon,
        this);
    window->addTextEditor("bar", {}, "Bar");
    window->addButton("Go", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    const auto safeThis = juce::Component::SafePointer<ScoreViewComponent>(this);
    window->enterModalState(
        true,
        juce::ModalCallbackFunction::create(
            [safeThis, window](int result)
            {
                if (result == 1 && safeThis != nullptr)
                {
                    safeThis->goToBar(window->getTextEditorContents("bar"));
                }
            }),
        true);
}

void ScoreViewComponent::goToBar(const juce::String& text)
{
    const juce::String wanted = text.trim();
    if (wanted.isEmpty() || renderedScore == nullptr || renderedScore->parts.empty())
    {
        return;
    }

    // By the number printed in the score first: that is what a reader sees,
    // and it is not the ordinal once a pickup or a split bar is involved. By
    // its position only when no bar is printed so.
    const auto& measures = renderedScore->parts.front().measures;
    std::optional<std::size_t> found;
    for (std::size_t index = 0; index < measures.size(); ++index)
    {
        if (juce::String(measures.at(index).printedNumber) == wanted)
        {
            found = index;
            break;
        }
    }
    if (!found.has_value() && wanted.containsOnly("0123456789"))
    {
        const int ordinal = wanted.getIntValue();
        if (ordinal >= 1 && static_cast<std::size_t>(ordinal) <= measures.size())
        {
            found = static_cast<std::size_t>(ordinal - 1);
        }
    }

    if (!found.has_value())
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::InfoIcon, "No bar " + wanted,
            "This score has no bar numbered " + wanted + ".", {}, this);
        return;
    }

    clock.seekToBar(static_cast<std::int64_t>(*found));
    const juce::ScopedValueSetter<bool> quiet(scrollingProgrammatically, true);
    revealMeasure(*found);
    canvas.repaint();
}

void ScoreViewComponent::setPageFit(score_view::PageFit fit)
{
    if (fit == settings.pageFit)
    {
        return;
    }
    const std::optional<std::size_t> keep = measureInView();
    settings.pageFit = fit;
    relayout(keep);
}

void ScoreViewComponent::setLookAhead(int onsets)
{
    settings.lookAhead = std::clamp(onsets, 0, engraving::maximumLookAhead);
    updateCursor();
}

void ScoreViewComponent::setFollow(bool shouldFollow)
{
    settings.follow = shouldFollow;
    followSuspended = false;
    if (shouldFollow)
    {
        follow();
    }
    updateControls();
}

void ScoreViewComponent::takesChanged()
{
    updateReview();
}

void ScoreViewComponent::updateReview()
{
    engraving::ReviewMarks placed;
    const take::TakeReview* review = takes.review();
    if (settings.showReview && review != nullptr && review->take != nullptr &&
        renderedScore != nullptr && review->take->score == renderedScore)
    {
        // An extra note's tick is where the review's rows put it.
        std::vector<double> extraTicks(review->report.extra.size(), 0.0);
        for (const evaluation::ReviewRow& row :
             evaluation::reviewRows(*review->take, review->report))
        {
            if (row.extra.has_value() && *row.extra < extraTicks.size())
            {
                extraTicks[*row.extra] = row.tick;
            }
        }
        placed = engraving::reviewMarksFor(layout, review->report, extraTicks);
    }

    // Anything drawn before or after is repainted; nothing either side is not.
    const bool changed = !placed.notes.empty() || !placed.extras.empty() ||
                         !reviewMarks.notes.empty() || !reviewMarks.extras.empty();
    reviewMarks = std::move(placed);
    if (changed)
    {
        canvas.repaint();
    }
}

void ScoreViewComponent::setShowReview(bool show)
{
    settings.showReview = show;
    updateReview();
}

std::vector<ToolComponent::MenuEntry> ScoreViewComponent::optionsMenuEntries()
{
    const auto viewEntry = [this](const juce::String& label, score_view::ViewMode view)
    { return MenuEntry{label, [this, view] { setView(view); }, settings.view == view}; };

    std::vector<MenuEntry> entries{
        viewEntry("Page", score_view::ViewMode::page),
        viewEntry("Vertical scroll", score_view::ViewMode::vertical),
        viewEntry("Horizontal scroll", score_view::ViewMode::horizontal),
        MenuEntry{"Enlarge", [this] { setSizeStep(settings.sizeStep + 1); }, false},
        MenuEntry{
            "Shrink",
            [this]
            {
                if (settings.sizeStep > 0)
                {
                    setSizeStep(settings.sizeStep - 1);
                }
            },
            false},
        MenuEntry{
            "Default size", [this] { setSizeStep(score_view::defaultSizeStep); },
            settings.sizeStep == score_view::defaultSizeStep}};

    if (settings.view == score_view::ViewMode::page)
    {
        entries.push_back(MenuEntry{
            "Fit width", [this] { setPageFit(score_view::PageFit::width); },
            settings.pageFit == score_view::PageFit::width});
        entries.push_back(MenuEntry{
            "Fit page", [this] { setPageFit(score_view::PageFit::page); },
            settings.pageFit == score_view::PageFit::page});
    }

    if (std::optional<MenuEntry> parts = partsMenuEntry())
    {
        entries.push_back(std::move(*parts));
    }

    // The practice clock (score-cursor).
    MenuEntry goTo{
        juce::String::fromUTF8("Go to bar\xe2\x80\xa6"), [this] { showGoToBar(); }, false};
    goTo.enabled = renderedScore != nullptr && layout.outcome == engraving::LayoutOutcome::laidOut;
    entries.push_back(std::move(goTo));

    MenuEntry lookAhead{"Look ahead", {}};
    for (int onsets = 0; onsets <= engraving::maximumLookAhead; ++onsets)
    {
        lookAhead.submenu.push_back(MenuEntry{
            onsets == 0 ? juce::String("None")
                        : juce::String(onsets) + (onsets == 1 ? " note" : " notes"),
            [this, onsets] { setLookAhead(onsets); }, settings.lookAhead == onsets});
    }
    entries.push_back(std::move(lookAhead));

    entries.push_back(MenuEntry{
        "Follow the practice clock", [this] { setFollow(!settings.follow); }, settings.follow});

    // The take under review (take-review decision 3).
    entries.push_back(MenuEntry{
        "Show take review", [this] { setShowReview(!settings.showReview); }, settings.showReview});

    return entries;
}

juce::String ScoreViewComponent::noticeText() const
{
    juce::StringArray lines;

    // First, because it explains everything else on the page. Without the
    // font the staff, stems and beams still draw and every symbol is missing,
    // which would otherwise look like a score with no notes in it.
    if (!musicFont.isLoaded())
    {
        lines.add("The music font did not load, so noteheads, clefs, accidentals and rests are "
                  "not drawn.");
    }

    const bool anyOversize = std::any_of(
        pages.begin(), pages.end(), [](const engraving::Page& page) { return page.oversize; });

    if (settings.view == score_view::ViewMode::page && anyOversize)
    {
        lines.add("A line of music too large for the page is on a page of its own, drawn "
                  "smaller so all of it shows.");
    }

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
                "Open a MusicXML file with File > Open score, and it is drawn here.");

            return;
        }

        paintMessage(graphics, message, "Nothing to draw", noticeText());

        return;
    }

    // The title strip: what is being drawn, and which part of it. The controls
    // to its right are components of their own.
    bounds.removeFromTop(titleStripHeight);
    const juce::Rectangle<int> title = titleTextBounds;

    graphics.setColour(colours.foreground);
    graphics.setFont(juce::FontOptions(13.0f, juce::Font::bold));

    const juce::String heading =
        juce::String(layout.scoreTitle.empty() ? "Untitled score" : layout.scoreTitle) + "  -  " +
        juce::String(layout.partName);

    graphics.drawText(heading, title, juce::Justification::centredLeft, true);

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

    const juce::String notices = noticeText();

    if (notices.isEmpty())
    {
        return;
    }

    const juce::Rectangle<int> strip =
        bounds.removeFromBottom(noticeStripHeight).reduced(canvasMargin, 4);

    graphics.setColour(colours.panel);
    graphics.fillRect(strip.expanded(canvasMargin, 2));

    graphics.setColour(colours.muted);
    graphics.setFont(juce::FontOptions(11.5f));
    graphics.drawFittedText(notices, strip, juce::Justification::topLeft, 3);
}
