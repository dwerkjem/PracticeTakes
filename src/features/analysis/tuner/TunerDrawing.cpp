#include "TunerComponent.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr double inTuneToleranceCents = 5.0;

// The graph's axis labels and the status line's note name must agree. They were
// byte-identical copies of the same function before, free to drift apart; the
// single definition now lives beside the tracker that produces the note.
[[nodiscard]] juce::String noteLabelForMidi(int midiNote)
{
    return juce::String(::noteNameForMidi(midiNote));
}
} // namespace

//==============================================================================
// Drawing

void TunerComponent::drawPitchGraph(juce::Graphics& graphics, juce::Rectangle<int> bounds) const
{
    const auto palette = tunerPaletteFor(currentTheme);

    auto content = bounds.reduced(8);
    auto labelArea = content.removeFromLeft(48);
    const auto plotArea = content;

    std::vector<double> validValues;
    validValues.reserve(graphHistory.size());
    for (const auto value : graphHistory)
    {
        if (std::isfinite(value))
        {
            validValues.push_back(value);
        }
    }

    double minimumValue = hasLockedMidiNote ? static_cast<double>(lockedMidiNote) - 3.0 : 66.0;
    double maximumValue = hasLockedMidiNote ? static_cast<double>(lockedMidiNote) + 3.0 : 72.0;

    if (!validValues.empty())
    {
        minimumValue = *std::min_element(validValues.begin(), validValues.end()) - 0.5;
        maximumValue = *std::max_element(validValues.begin(), validValues.end()) + 0.5;

        if (maximumValue - minimumValue < 6.0)
        {
            const auto centre = (minimumValue + maximumValue) * 0.5;
            minimumValue = centre - 3.0;
            maximumValue = centre + 3.0;
        }

        // Snapped to whole semitones, for the same reason the compact graph
        // does it: the gridlines below are drawn at integer semitones within
        // this range, so ends that move by a fraction of one every frame --
        // which a data-driven min/max does continuously, both as the trace
        // moves and as points scroll off the end -- slide the entire grid
        // under the trace. A held note then appears to drift against lines
        // that are themselves drifting, and neither can be read against the
        // other.
        minimumValue = std::floor(minimumValue);
        maximumValue = std::ceil(maximumValue);
    }

    const auto firstNote = static_cast<int>(std::ceil(minimumValue));
    const auto lastNote = static_cast<int>(std::floor(maximumValue));
    const auto pixelsPerSemitone =
        static_cast<double>(plotArea.getHeight()) / std::max(1.0, maximumValue - minimumValue);

    graphics.setFont(juce::FontOptions(11.0f));

    for (int midiNote = firstNote; midiNote <= lastNote; ++midiNote)
    {
        const auto y = juce::jmap(
            static_cast<float>(midiNote), static_cast<float>(minimumValue),
            static_cast<float>(maximumValue), static_cast<float>(plotArea.getBottom()),
            static_cast<float>(plotArea.getY()));

        const auto isCurrentNote = hasSignal && midiNote == lockedMidiNote;
        const auto isC = ((midiNote % 12) + 12) % 12 == 0;

        graphics.setColour(
            isCurrentNote ? palette.accent.withAlpha(0.48f)
            : isC         ? palette.muted.withAlpha(0.30f)
                          : palette.outline.withAlpha(0.62f));
        graphics.drawHorizontalLine(
            static_cast<int>(std::round(y)), static_cast<float>(plotArea.getX()),
            static_cast<float>(plotArea.getRight()));

        if (pixelsPerSemitone >= 10.0 || isCurrentNote || isC)
        {
            graphics.setColour(isCurrentNote ? palette.accent : palette.muted);
            graphics.drawFittedText(
                noteLabelForMidi(midiNote),
                labelArea.withY(static_cast<int>(std::round(y)) - 8).withHeight(16),
                juce::Justification::centredRight, 1);
        }
    }

    if (graphHistory.size() < 2 || validValues.empty())
    {
        return;
    }

    juce::Path pitchPath;
    bool pathHasStarted = false;

    for (std::size_t index = 0; index < graphHistory.size(); ++index)
    {
        const auto value = graphHistory[index];
        if (!std::isfinite(value))
        {
            pathHasStarted = false;
            continue;
        }

        const auto x = juce::jmap(
            static_cast<float>(index), 0.0f, static_cast<float>(graphHistory.size() - 1),
            static_cast<float>(plotArea.getX()), static_cast<float>(plotArea.getRight()));
        const auto y = juce::jmap(
            static_cast<float>(value), static_cast<float>(minimumValue),
            static_cast<float>(maximumValue), static_cast<float>(plotArea.getBottom()),
            static_cast<float>(plotArea.getY()));

        if (pathHasStarted)
        {
            pitchPath.lineTo(x, y);
        }
        else
        {
            pitchPath.startNewSubPath(x, y);
            pathHasStarted = true;
        }
    }

    graphics.setColour(palette.accent);
    graphics.strokePath(pitchPath, juce::PathStrokeType(2.0f));
}

void TunerComponent::drawPitchBar(juce::Graphics& graphics, juce::Rectangle<int> bounds) const
{
    const auto palette = tunerPaletteFor(currentTheme);
    const auto indicatorColour =
        std::abs(displayedCents) <= inTuneToleranceCents ? palette.inTune : palette.accent;

    auto barBounds = bounds.reduced(28, 34);
    const auto centreY = barBounds.getCentreY();

    graphics.setColour(palette.control);
    graphics.fillRoundedRectangle(
        barBounds.toFloat().withHeight(10.0f).withCentre(
            {static_cast<float>(barBounds.getCentreX()), static_cast<float>(centreY)}),
        5.0f);

    graphics.setFont(juce::FontOptions(12.0f));
    for (const auto cents : {-50, -25, 0, 25, 50})
    {
        const auto x = juce::jmap(
            static_cast<float>(cents), -50.0f, 50.0f, static_cast<float>(barBounds.getX()),
            static_cast<float>(barBounds.getRight()));

        graphics.setColour(cents == 0 ? palette.foreground : palette.muted);
        graphics.drawVerticalLine(
            static_cast<int>(std::round(x)), static_cast<float>(centreY) - 18.0f,
            static_cast<float>(centreY) + 18.0f);
        graphics.drawText(
            juce::String(cents > 0 ? "+" : "") + juce::String(cents), static_cast<int>(x) - 24,
            centreY + 22, 48, 18, juce::Justification::centred);
    }

    if (!hasSignal)
    {
        return;
    }

    const auto indicatorX = juce::jmap(
        static_cast<float>(juce::jlimit(-50.0, 50.0, displayedCents)), -50.0f, 50.0f,
        static_cast<float>(barBounds.getX()), static_cast<float>(barBounds.getRight()));

    graphics.setColour(indicatorColour);
    graphics.fillEllipse(indicatorX - 10.0f, static_cast<float>(centreY) - 10.0f, 20.0f, 20.0f);
}

void TunerComponent::drawPitchMeter(juce::Graphics& graphics, juce::Rectangle<int> bounds) const
{
    const auto palette = tunerPaletteFor(currentTheme);
    const auto indicatorColour =
        std::abs(displayedCents) <= inTuneToleranceCents ? palette.inTune : palette.accent;

    const auto centre = juce::Point<float>(
        static_cast<float>(bounds.getCentreX()), static_cast<float>(bounds.getBottom() - 24));
    const auto radius = static_cast<float>(
        std::max(30, std::min(bounds.getWidth() / 2 - 28, bounds.getHeight() - 54)));

    constexpr double startAngle = -2.45;
    constexpr double endAngle = -0.69;

    juce::Path arc;
    for (int step = 0; step <= 64; ++step)
    {
        const auto proportion = static_cast<double>(step) / 64.0;
        const auto angle = startAngle + proportion * (endAngle - startAngle);
        const auto point = centre + juce::Point<float>(
                                        static_cast<float>(std::cos(angle) * radius),
                                        static_cast<float>(std::sin(angle) * radius));

        if (step == 0)
        {
            arc.startNewSubPath(point);
        }
        else
        {
            arc.lineTo(point);
        }
    }

    graphics.setColour(palette.outline);
    graphics.strokePath(arc, juce::PathStrokeType(5.0f));

    graphics.setFont(juce::FontOptions(12.0f));
    for (const auto cents : {-50, -25, 0, 25, 50})
    {
        const auto angle =
            juce::jmap(static_cast<double>(cents), -50.0, 50.0, startAngle, endAngle);
        const auto outerPoint = centre + juce::Point<float>(
                                             static_cast<float>(std::cos(angle) * radius),
                                             static_cast<float>(std::sin(angle) * radius));
        const auto innerPoint =
            centre + juce::Point<float>(
                         static_cast<float>(std::cos(angle) * (radius - 15.0f)),
                         static_cast<float>(std::sin(angle) * (radius - 15.0f)));

        graphics.setColour(cents == 0 ? palette.foreground : palette.muted);
        graphics.drawLine({innerPoint, outerPoint}, cents == 0 ? 2.0f : 1.0f);
    }

    const auto needleCents = hasSignal ? juce::jlimit(-50.0, 50.0, displayedCents) : 0.0;
    const auto needleAngle = juce::jmap(needleCents, -50.0, 50.0, startAngle, endAngle);
    const auto needleEnd =
        centre + juce::Point<float>(
                     static_cast<float>(std::cos(needleAngle) * (radius - 20.0f)),
                     static_cast<float>(std::sin(needleAngle) * (radius - 20.0f)));

    graphics.setColour(hasSignal ? indicatorColour : palette.muted);
    graphics.drawLine({centre, needleEnd}, 3.0f);
    graphics.fillEllipse(centre.x - 7.0f, centre.y - 7.0f, 14.0f, 14.0f);

    graphics.setColour(palette.muted);
    graphics.drawText(
        "FLAT", bounds.removeFromLeft(74).removeFromBottom(24), juce::Justification::centred);
    graphics.drawText(
        "SHARP", bounds.removeFromRight(74).removeFromBottom(24), juce::Justification::centred);
}

void TunerComponent::drawNoteWatermark(juce::Graphics& graphics, juce::Rectangle<int> area) const
{
    // The note being played is drawn *behind* the display rather than above it,
    // at low opacity. It reads at a glance from across a room without spending
    // any vertical space, which is what lets the display have the rest of the
    // panel -- the note used to take 96px off the top before the graph got
    // anything at all.
    //
    // Nothing is drawn without a signal: displayedNote is "--" then, and a
    // giant translucent "--" reads as a rendering fault rather than as an idle
    // tuner. The status line above already says what to do.
    if (!hasSignal)
    {
        return;
    }

    const auto palette = tunerPaletteFor(currentTheme);

    // Sized from the area it is given rather than fixed, so it fills a tall
    // docked pane and still fits the half-height band the bar leaves it. The
    // cap only stops it becoming absurd on a very large display.
    const auto fontHeight =
        juce::jlimit(24.0f, 360.0f, static_cast<float>(area.getHeight()) * 0.28f);

    graphics.setColour(palette.foreground.withAlpha(0.14f));
    graphics.setFont(juce::FontOptions(fontHeight, juce::Font::bold));
    graphics.drawFittedText(displayedNote, area, juce::Justification::centred, 1);
}

void TunerComponent::drawCompactGraph(
    juce::Graphics& graphics,
    juce::Rectangle<int> bounds,
    compact::Shape shape) const
{
    // The pitch trace along whichever axis has room, and a scale coarse enough
    // to still be a scale. The full graph draws a gridline per semitone over
    // six semitones; at 90px that is a line every 15px with a label on each,
    // which reads as texture rather than as pitch. So the range narrows and the
    // labels thin out as the space does.
    const auto palette = tunerPaletteFor(currentTheme);
    const auto vertical = shape == compact::Shape::vertical;
    const auto alongAxis = vertical ? bounds.getHeight() : bounds.getWidth();
    const auto detail = compact::detailFor(alongAxis);

    // Two semitones when there is almost no room, up to six when there is.
    const auto span = 2.0 + 4.0 * static_cast<double>(detail);

    // The trace decides the range whenever there is a trace. The locked note
    // is only a fallback, for a graph with nothing in it yet.
    //
    // These used to be unioned -- a window around the locked note, widened by
    // the data -- which tied the axis to a value that disappears. Losing the
    // lock during a pause dropped `centreNote` to its A4 default while the
    // history still held, say, an E3 an octave and a half below, so the range
    // stretched to cover both and the graph went from eight semitones to
    // twenty at the moment the singing stopped. Nothing about the trace had
    // changed; only its anchor had gone.
    auto hasData = false;
    auto lowest = 0.0;
    auto highest = 0.0;

    for (const auto value : graphHistory)
    {
        if (!std::isfinite(value))
        {
            continue;
        }

        lowest = hasData ? std::min(lowest, value) : value;
        highest = hasData ? std::max(highest, value) : value;
        hasData = true;
    }

    if (!hasData)
    {
        const auto centreNote = hasLockedMidiNote ? static_cast<double>(lockedMidiNote) : 69.0;
        lowest = centreNote - span * 0.5;
        highest = centreNote + span * 0.5;
    }
    else if (highest - lowest < span)
    {
        // `span` as a floor, so a dead-steady note does not get an axis zoomed
        // to its own jitter -- centred on the trace, not on the locked note,
        // so this cannot move the window either.
        const auto centre = (lowest + highest) * 0.5;
        lowest = centre - span * 0.5;
        highest = centre + span * 0.5;
    }

    // Air in whole semitones, and the ends snapped to them.
    //
    // The gridlines are drawn at integer semitones *inside* this range, so
    // where the ends fall decides where every line lands. A proportional
    // margin over a min/max that changes every frame -- as the trace moves and
    // as old points scroll off the end -- moves those ends by a fraction of a
    // semitone continuously, which slides the whole grid underneath a trace
    // that ought to be sitting still against it. The pitch stops being
    // readable against a line that will not hold still, and a note held dead
    // steady still appears to drift.
    //
    // Quantised, the range is exactly the same from frame to frame for as long
    // as the trace stays inside it, and steps by one whole line when it does
    // not -- so a line means one fixed pitch for as long as it is on screen.
    lowest = std::floor(lowest) - 1.0;
    highest = std::ceil(highest) + 1.0;

    const auto plot = bounds.reduced(6);

    // The current note's line, always. It is the one the trace is read against.
    for (int midiNote = static_cast<int>(std::ceil(lowest));
         midiNote <= static_cast<int>(std::floor(highest)); ++midiNote)
    {
        const auto isCurrent = hasSignal && midiNote == lockedMidiNote;

        // Every gridline when there is room; only the current one when there
        // is not.
        if (!isCurrent && detail < 0.35f)
        {
            continue;
        }

        graphics.setColour(
            isCurrent ? palette.accent.withAlpha(0.48f) : palette.outline.withAlpha(0.62f));

        if (vertical)
        {
            const auto x = juce::jmap(
                static_cast<float>(midiNote), static_cast<float>(lowest),
                static_cast<float>(highest), static_cast<float>(plot.getX()),
                static_cast<float>(plot.getRight()));
            graphics.drawVerticalLine(
                static_cast<int>(std::round(x)), static_cast<float>(plot.getY()),
                static_cast<float>(plot.getBottom()));
        }
        else
        {
            const auto y = juce::jmap(
                static_cast<float>(midiNote), static_cast<float>(lowest),
                static_cast<float>(highest), static_cast<float>(plot.getBottom()),
                static_cast<float>(plot.getY()));
            graphics.drawHorizontalLine(
                static_cast<int>(std::round(y)), static_cast<float>(plot.getX()),
                static_cast<float>(plot.getRight()));
        }
    }

    if (graphHistory.size() >= 2)
    {
        juce::Path trace;
        bool started = false;

        for (std::size_t index = 0; index < graphHistory.size(); ++index)
        {
            const auto value = graphHistory[index];

            if (!std::isfinite(value))
            {
                started = false;
                continue;
            }

            // Time runs down a vertical pane and across a horizontal one; pitch
            // takes the other axis in both.
            const auto alongTime = juce::jmap(
                static_cast<float>(index), 0.0f, static_cast<float>(graphHistory.size() - 1),
                static_cast<float>(vertical ? plot.getY() : plot.getX()),
                static_cast<float>(vertical ? plot.getBottom() : plot.getRight()));
            // Clamped as well as ranged. The range above covers every finite
            // value, so this only catches the degenerate case where they are
            // all equal -- but a trace outside the panel is exactly the bug
            // being fixed, and it should not depend on arithmetic elsewhere
            // staying correct.
            const auto alongPitch = juce::jmap(
                static_cast<float>(juce::jlimit(lowest, highest, value)),
                static_cast<float>(lowest), static_cast<float>(highest),
                static_cast<float>(vertical ? plot.getX() : plot.getBottom()),
                static_cast<float>(vertical ? plot.getRight() : plot.getY()));

            const auto x = vertical ? alongPitch : alongTime;
            const auto y = vertical ? alongTime : alongPitch;

            started ? trace.lineTo(x, y) : trace.startNewSubPath(x, y);
            started = true;
        }

        graphics.setColour(palette.accent);
        graphics.strokePath(trace, juce::PathStrokeType(2.0f));
    }

    drawCompactReading(
        graphics, juce::Rectangle<int>(0, 0, juce::jmin(bounds.getWidth() - 12, 160), 44)
                      .withCentre(bounds.getCentre()));
}

void TunerComponent::drawCompactBar(
    juce::Graphics& graphics,
    juce::Rectangle<int> bounds,
    compact::Shape shape) const
{
    // A bar along the long axis with the reading in the middle of it. The
    // -50..+50 tick labels go: five numbers do not fit across 180px, and the
    // position of the marker already carries what they said.
    const auto palette = tunerPaletteFor(currentTheme);
    const auto vertical = shape == compact::Shape::vertical;
    const auto inTune = std::abs(displayedCents) <= inTuneToleranceCents;
    const auto plot = bounds.reduced(10);

    constexpr float trackThickness = 8.0f;
    const auto centre = plot.getCentre();

    graphics.setColour(palette.control);
    graphics.fillRoundedRectangle(
        vertical ? juce::Rectangle<float>(
                       static_cast<float>(centre.x) - trackThickness * 0.5f,
                       static_cast<float>(plot.getY()), trackThickness,
                       static_cast<float>(plot.getHeight()))
                 : juce::Rectangle<float>(
                       static_cast<float>(plot.getX()),
                       static_cast<float>(centre.y) - trackThickness * 0.5f,
                       static_cast<float>(plot.getWidth()), trackThickness),
        trackThickness * 0.5f);

    // The centre line, so "in tune" is a place rather than only a colour.
    graphics.setColour(palette.foreground.withAlpha(0.5f));

    if (vertical)
    {
        graphics.drawHorizontalLine(
            centre.y, static_cast<float>(centre.x) - 14.0f, static_cast<float>(centre.x) + 14.0f);
    }
    else
    {
        graphics.drawVerticalLine(
            centre.x, static_cast<float>(centre.y) - 14.0f, static_cast<float>(centre.y) + 14.0f);
    }

    if (hasSignal)
    {
        const auto cents = static_cast<float>(juce::jlimit(-50.0, 50.0, displayedCents));

        // Sharp is up on a vertical bar and right on a horizontal one, which is
        // the direction each of those already means elsewhere in the tool.
        const auto position =
            vertical ? juce::jmap(
                           cents, -50.0f, 50.0f, static_cast<float>(plot.getBottom()),
                           static_cast<float>(plot.getY()))
                     : juce::jmap(
                           cents, -50.0f, 50.0f, static_cast<float>(plot.getX()),
                           static_cast<float>(plot.getRight()));

        graphics.setColour(inTune ? palette.inTune : palette.accent);
        graphics.fillEllipse(
            (vertical ? static_cast<float>(centre.x) : position) - 9.0f,
            (vertical ? position : static_cast<float>(centre.y)) - 9.0f, 18.0f, 18.0f);
    }

    // Clear of the track, not centred on it. Centred, the marker sits exactly
    // on the note whenever the reading is in tune -- which is the moment the
    // note most wants reading. Offset perpendicular to the track, the two never
    // meet at any reading.
    constexpr int readingHeight = 34;
    constexpr int clearance = 16;
    const auto readingCentre =
        vertical ? juce::Point<int>(
                       centre.x - static_cast<int>(trackThickness) - clearance - readingHeight / 2,
                       centre.y)
                 : juce::Point<int>(
                       centre.x,
                       centre.y - static_cast<int>(trackThickness) - clearance - readingHeight / 2);

    drawCompactReading(
        graphics, juce::Rectangle<int>(0, 0, juce::jmin(bounds.getWidth() - 12, 120), readingHeight)
                      .withCentre(readingCentre));
}

void TunerComponent::drawCompactMeter(
    juce::Graphics& graphics,
    juce::Rectangle<int> bounds,
    compact::Shape shape) const
{
    // The one display that does not rotate. A dial needs width *and* height and
    // has neither here, so it becomes what it was actually telling you: the
    // note, and how close you are, carried by colour.
    juce::ignoreUnused(shape);

    const auto palette = tunerPaletteFor(currentTheme);
    const auto inTune = std::abs(displayedCents) <= inTuneToleranceCents;
    auto content = bounds.reduced(8);

    if (hasSignal)
    {
        // The whole face takes the colour, so it reads from across a room at a
        // size where no text would.
        // Darker on the light palette: 0.18 over white is barely a tint, and
        // the wash is the whole signal at this size.
        graphics.setColour((inTune ? palette.inTune : palette.accent)
                               .withAlpha(isDarkTheme(currentTheme) ? 0.18f : 0.30f));
        graphics.fillRoundedRectangle(bounds.reduced(2).toFloat(), 6.0f);
    }

    auto directionArea = content.removeFromBottom(std::min(28, content.getHeight() / 4));

    graphics.setColour(hasSignal ? palette.foreground : palette.muted);
    graphics.setFont(juce::FontOptions(
        juce::jlimit(24.0f, 190.0f, static_cast<float>(content.getHeight()) * 0.55f),
        juce::Font::bold));
    graphics.drawFittedText(
        hasSignal ? displayedNote : juce::String("--"), content, juce::Justification::centred, 1);

    if (!hasSignal)
    {
        return;
    }

    graphics.setColour(inTune ? palette.inTune : palette.accent);
    graphics.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    graphics.drawFittedText(
        inTune ? "in tune" : directionLabel(), directionArea, juce::Justification::centred, 1);
}

juce::String TunerComponent::directionLabel() const
{
    return displayedCents > 0 ? juce::String::fromUTF8("\xe2\x96\xb2  sharp")
                              : juce::String::fromUTF8("\xe2\x96\xbc  flat");
}

void TunerComponent::drawCompactReading(juce::Graphics& graphics, juce::Rectangle<int> area) const
{
    // The note, as large as the space allows, with a small arrow beside it when
    // the reading is off. The arrow is the whole point of a tuner at this size:
    // the note says what you are playing, and only the arrow says what to do
    // about it.
    //
    // In tune is carried twice over -- the colour changes *and* the arrow goes
    // away -- so it survives a glance, a colourblind reader, and a bad monitor.
    if (!hasSignal)
    {
        return;
    }

    const auto palette = tunerPaletteFor(currentTheme);
    const auto inTune = std::abs(displayedCents) <= inTuneToleranceCents;

    // Lighter over a dark background reads as recessed; over a light one it
    // reads as faded. The light palette therefore gets nearly full opacity --
    // the point of the transparency is to let a trace show through, not to make
    // the reading hard to find.
    const auto alpha = isDarkTheme(currentTheme) ? 0.85f : 0.97f;
    const auto noteHeight = juce::jlimit(24.0f, 96.0f, static_cast<float>(area.getHeight()));
    const auto arrowHeight = noteHeight * 0.5f;

    const juce::Font noteFont(juce::FontOptions(noteHeight, juce::Font::bold));
    const juce::Font arrowFont(juce::FontOptions(arrowHeight, juce::Font::bold));

    const auto arrow = inTune               ? juce::String()
                       : displayedCents > 0 ? juce::String::fromUTF8("\xe2\x96\xb2")
                                            : juce::String::fromUTF8("\xe2\x96\xbc");

    constexpr float gap = 6.0f;
    const auto noteWidth = juce::GlyphArrangement::getStringWidth(noteFont, displayedNote);
    const auto arrowWidth =
        arrow.isEmpty() ? 0.0f : juce::GlyphArrangement::getStringWidth(arrowFont, arrow) + gap;

    // Laid out as one centred group rather than a centred note with something
    // hung off it, so the pair stays centred whichever way the reading goes.
    auto left = static_cast<float>(area.getCentreX()) - (noteWidth + arrowWidth) * 0.5f;
    const auto centreY = static_cast<float>(area.getCentreY());

    graphics.setColour((inTune ? palette.inTune : palette.foreground).withAlpha(alpha));
    graphics.setFont(noteFont);
    graphics.drawText(
        displayedNote,
        juce::Rectangle<float>(left, centreY - noteHeight * 0.5f, noteWidth, noteHeight),
        juce::Justification::centred, false);

    if (arrow.isEmpty())
    {
        return;
    }

    left += noteWidth + gap;

    graphics.setColour(palette.accent.withAlpha(alpha));
    graphics.setFont(arrowFont);
    graphics.drawText(
        arrow, juce::Rectangle<float>(left, centreY - arrowHeight * 0.5f, arrowWidth, arrowHeight),
        juce::Justification::centred, false);
}

void TunerComponent::drawCompactDisplay(
    juce::Graphics& graphics,
    juce::Rectangle<int> bounds,
    compact::Shape shape) const
{
    const auto palette = tunerPaletteFor(currentTheme);

    juce::ignoreUnused(palette);

    // Nothing inside the box without a signal *and* without history -- the
    // graph and the bar both already draw correctly with hasSignal false
    // (drawCompactGraph plots straight from graphHistory; drawCompactBar just
    // skips the marker dot), so this was blanking a pane that had a perfectly
    // good trace to show, every ordinary pause between notes. Blank only
    // belongs to a graph that has nothing in it yet -- the prompt this stood
    // in for before repeating "play or sing" in a 180px pane produced two
    // short wrapped lines that read as a rendering fault. The meter is the
    // exception either way: its whole compact form is a note placeholder,
    // which is legible empty.
    if (!hasSignal && !hasGraphHistory() &&
        static_cast<DisplayMode>(displayModeBox.getSelectedId()) != DisplayMode::meter)
    {
        return;
    }

    switch (static_cast<DisplayMode>(displayModeBox.getSelectedId()))
    {
    case DisplayMode::bar:
        drawCompactBar(graphics, bounds, shape);
        break;

    case DisplayMode::meter:
        drawCompactMeter(graphics, bounds, shape);
        break;

    case DisplayMode::graph:
    default:
        drawCompactGraph(graphics, bounds, shape);
        break;
    }
}

void TunerComponent::drawSelectedDisplay(juce::Graphics& graphics, juce::Rectangle<int> bounds)
    const
{
    // The panel, the watermark, and the border are common to all three modes,
    // so they live here rather than being repeated in each -- and the ordering
    // is the point: the watermark goes down after the background and before the
    // mode draws, so the graph line, the bar, and the meter all sit on top of
    // it. The border goes last so no content can bleed over the rounded edge.
    const auto palette = tunerPaletteFor(currentTheme);

    graphics.setColour(palette.panel);
    graphics.fillRoundedRectangle(bounds.toFloat(), 8.0f);

    // Too small for any of the three displays at full size, so each draws its
    // compact form instead -- along whichever axis has room. The border still
    // goes down below, because a compact display is the display, not a message
    // shown in place of one.
    const auto shape = compact::shapeFor(bounds.getWidth(), bounds.getHeight());

    if (shape != compact::Shape::full)
    {
        drawCompactDisplay(graphics, bounds, shape);
        graphics.setColour(palette.outline);
        graphics.drawRoundedRectangle(bounds.toFloat(), 8.0f, 1.0f);

        return;
    }

    const auto mode = static_cast<DisplayMode>(displayModeBox.getSelectedId());

    // Where the note goes depends on what the mode leaves free. The graph and
    // the meter both keep their middle clear, so the note sits behind them,
    // centred, which is where it reads best. The bar lies straight across the
    // middle -- centred there, the track and its "0" tick slice the glyphs in
    // half -- so above the bar it is, in the band the bar does not use.
    const auto watermarkArea =
        mode == DisplayMode::bar ? bounds.withTrimmedBottom(bounds.getHeight() / 2 + 26) : bounds;
    drawNoteWatermark(graphics, watermarkArea);

    switch (mode)
    {
    case DisplayMode::bar:
        drawPitchBar(graphics, bounds);
        break;

    case DisplayMode::meter:
        drawPitchMeter(graphics, bounds);
        break;

    case DisplayMode::graph:
    default:
        drawPitchGraph(graphics, bounds);
        break;
    }

    graphics.setColour(palette.outline);
    graphics.drawRoundedRectangle(bounds.toFloat(), 8.0f, 1.0f);
}

void TunerComponent::paint(juce::Graphics& graphics)
{
    const auto palette = tunerPaletteFor(currentTheme);
    graphics.fillAll(palette.background);

    auto bounds = getLocalBounds().reduced(18);

    // One quiet line of readout, then the display takes everything left. The
    // frequency and cents are a detail you consult; the note is the thing you
    // read at a glance, and it is drawn behind the display instead of above it.
    graphics.setColour(palette.muted);
    graphics.setFont(juce::FontOptions(13.0f));
    graphics.drawText(
        statusText(), bounds.removeFromTop(statusHeight), juce::Justification::centred);
    bounds.removeFromTop(statusGap);

    const auto preferredDisplayHeight = std::max(90, bounds.getHeight() - controlAreaHeight() - 8);
    displayBounds = bounds.removeFromTop(std::min(preferredDisplayHeight, bounds.getHeight()));

    drawSelectedDisplay(graphics, displayBounds);
}

void TunerComponent::resized()
{
    auto bounds = getLocalBounds().reduced(18);

    // Must consume exactly what paint() consumes above the display, or the
    // controls drift away from the box they belong under.
    bounds.removeFromTop(statusHeight);
    bounds.removeFromTop(statusGap);

    const auto preferredDisplayHeight = std::max(90, bounds.getHeight() - controlAreaHeight() - 8);
    bounds.removeFromTop(std::min(preferredDisplayHeight, bounds.getHeight()));

    // Only when nobody adopted it. A docked panel puts the chooser on its
    // header line, and reserving a row here as well would leave a gap where it
    // used to be.
    if (modeChooser != nullptr && !isModeChooserAdopted())
    {
        bounds.removeFromTop(8);
        placeModeChooser(bounds.removeFromTop(modeChooserHeight));
    }

    if (!areAdvancedSettingsExpanded)
    {
        return;
    }

    bounds.removeFromTop(8);
    const auto placeSliderRow = [&bounds](juce::Label& label, juce::Slider& slider)
    {
        auto row = bounds.removeFromTop(30);
        label.setBounds(row.removeFromLeft(120));
        slider.setBounds(row);
    };

    placeSliderRow(easingLabel, easingSlider);
    placeSliderRow(averagingLabel, averagingSlider);
    placeSliderRow(thresholdLabel, thresholdSlider);
    placeSliderRow(dropoutLabel, dropoutSlider);
    placeSliderRow(durationLabel, durationSlider);

    bounds.removeFromTop(8);
    clearGraphButton.setBounds(bounds.removeFromTop(36).removeFromRight(120));
}
