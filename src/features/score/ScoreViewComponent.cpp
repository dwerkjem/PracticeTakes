#include "ScoreViewComponent.h"

#include "engraving/PartNames.h"
#include "engraving/ScoreLayoutEngine.h"

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
} // namespace

ScoreViewComponent::Canvas::Canvas(ScoreViewComponent& ownerToUse) : owner(ownerToUse) {}

void ScoreViewComponent::Canvas::mouseDown(const juce::MouseEvent& event)
{
    juce::ignoreUnused(event);
    owner.grabKeyboardFocus();
}

void ScoreViewComponent::Canvas::paint(juce::Graphics& graphics)
{
    const double px = owner.pixelsPerSpace();
    const ScoreGlyphPainter painter(
        graphics, owner.musicFont, px, scoreNotationPaletteFor(owner.currentTheme));

    if (owner.settings.view != score_view::ViewMode::page)
    {
        for (const engraving::System& system : owner.layout.systems)
        {
            painter.drawSystem(
                system, static_cast<double>(canvasMargin),
                static_cast<double>(canvasMargin) + (system.topOfStaff * px));
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
    const juce::Rectangle<float> content = owner.pageContent();

    // A page whose one system is larger than the page, in either direction, is
    // drawn just small enough to show all of it: nothing on a page is ever cut
    // off. Every other page is drawn at the chosen size.
    const double scale = engraving::fitScale(
        shown, owner.layout.systems, static_cast<double>(content.getHeight()) / px,
        static_cast<double>(content.getWidth()) / px);
    const double pagePx = px * scale;
    const ScoreGlyphPainter pagePainter(
        graphics, owner.musicFont, pagePx, scoreNotationPaletteFor(owner.currentTheme));

    for (std::size_t index = shown.firstSystem; index <= shown.lastSystem; ++index)
    {
        const engraving::System& system = owner.layout.systems.at(index);

        pagePainter.drawSystem(
            system, static_cast<double>(content.getX()),
            static_cast<double>(content.getY()) + ((system.topOfStaff - shown.top) * pagePx));
    }
}

ScoreViewComponent::ScoreViewComponent(const score::CurrentScore& sharedCurrentScore)
    : currentScore(sharedCurrentScore)
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

    for (juce::Label* label : {&sizeLabel, &pageLabel})
    {
        label->setJustificationType(juce::Justification::centred);
        label->setFont(juce::FontOptions(12.0f));
        label->setInterceptsMouseClicks(false, false);
        addAndMakeVisible(*label);
    }

    refreshFromCurrentScore();
    startTimerHz(refreshRateHz);
}

ScoreViewComponent::~ScoreViewComponent()
{
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
    viewport.setViewPosition(0, 0);

    repaint();
}

juce::Rectangle<float> ScoreViewComponent::pageBounds() const
{
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

    const LayoutKey key{width, height, settings.view, settings.sizeStep, settings.hiddenParts};

    if (!force && key == laidOutFor)
    {
        return;
    }

    laidOutFor = key;

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

    currentPage = std::min(page, pages.size() - 1);

    // A turned page is read from its top.
    viewport.setViewPosition(0, 0);
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

    enlargeButton.setBounds(title.removeFromRight(controlButtonWidth));
    sizeLabel.setBounds(title.removeFromRight(sizeLabelWidth));
    shrinkButton.setBounds(title.removeFromRight(controlButtonWidth));
    title.removeFromRight(controlGap);
    nextPageButton.setBounds(title.removeFromRight(controlButtonWidth));
    pageLabel.setBounds(title.removeFromRight(pageLabelWidth));
    previousPageButton.setBounds(title.removeFromRight(controlButtonWidth));
    title.removeFromRight(controlGap);
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

    if (std::optional<MenuEntry> parts = partsMenuEntry())
    {
        entries.push_back(std::move(*parts));
    }

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
