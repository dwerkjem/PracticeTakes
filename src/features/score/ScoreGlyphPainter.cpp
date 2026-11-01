#include "ScoreGlyphPainter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <vector>

namespace
{
using engraving::StaffSteps;

// Line weights, in staff spaces: Bravura's published engraving defaults. SMuFL
// draws these things as lines rather than glyphs -- a staff line, a stem or a
// barline is as long as the layout says, which no fixed glyph can be -- and
// publishes the thickness each should be drawn at so that the lines match the
// weight of the glyphs beside them.
constexpr double staffLineThickness = 0.13;
constexpr double ledgerLineThickness = 0.16;
constexpr double thinBarlineThickness = 0.16;
constexpr double thickBarlineThickness = 0.5;

// How far a ledger line reaches past each side of its notehead, and how far on
// the side an accidental stands: shortened there, as Gould prescribes, so the
// sign does not sit on the end of the line.
constexpr double ledgerLineExtension = 0.4;
constexpr double ledgerLineExtensionBesideAccidental = 0.1;

// The gap between the thin and thick lines of a final or repeat barline, and
// between a repeat's dots and its thin line.
constexpr double thinThickBarlineSeparation = 0.4;
constexpr double repeatDotSeparation = 0.16;

// A tie is drawn thick in the middle and thin at the ends, which is what makes
// it read as a tie rather than as a stroked arc.
constexpr double tieEndpointThickness = 0.1;
constexpr double tieMidpointThickness = 0.22;

// Room between an accidental and the notehead it modifies.
constexpr double accidentalGap = 0.2;

// Room between a notehead and its first augmentation dot, and from one dot to
// the next.
constexpr double dotGap = 0.24;
constexpr double dotPitch = 0.56;

// Grace notes are drawn at this fraction of full size: the convention for cue
// and grace notation, small enough to read as not taking time and large
// enough to read at all.
constexpr double graceScale = 0.66;

// Where a clef's sign begins inside the room the layout reserved for it.
constexpr double clefLeadIn = 0.4;

// Where a time signature's numbers begin inside their reserved room. Half a
// space, so the first digit stands clear of a key signature's last accidental
// rather than reading as part of it.
constexpr double timeSignatureLeadIn = 0.5;

// A time signature's numbers each fill half the staff, centred on the second
// line from the top and the second from the bottom.
constexpr StaffSteps upperTimeSignatureNumber = 2;
constexpr StaffSteps lowerTimeSignatureNumber = 6;

// The two dots of a repeat sit in the spaces either side of the middle line.
constexpr std::array<StaffSteps, 2> repeatDotPositions{3, 5};

[[nodiscard]] double yForSteps(StaffSteps steps)
{
    return engraving::spacesBelowTopLine(steps);
}

// The width of a run of glyphs set side by side, each advancing by its own
// right edge. Time signature digits are the only run drawn, and their outlines
// begin at x 0.08, so the right edge is the advance plus nothing worth adding.
[[nodiscard]] double runWidth(const MusicFont& font, const std::vector<smufl::Glyph>& glyphs)
{
    double width = 0.0;

    for (const smufl::Glyph glyph : glyphs)
    {
        width += static_cast<double>(font.bounds(glyph).getRight());
    }

    return width;
}
} // namespace

ScoreGlyphPainter::Palette scoreNotationPaletteFor(Theme theme)
{
    const AppPalette app = appPaletteFor(theme);

    // The staff is drawn lighter than the music on it. A staff at full
    // foreground weight reads as louder than the notes, which is backwards.
    return ScoreGlyphPainter::Palette{
        app.foreground.withMultipliedAlpha(0.45f), app.foreground, app.muted, app.accent};
}

ScoreGlyphPainter::ScoreGlyphPainter(
    juce::Graphics& graphicsToUse,
    const MusicFont& fontToUse,
    double pixelsPerSpace,
    Palette colours)
    : graphics(graphicsToUse), font(fontToUse), spaceInPixels(pixelsPerSpace), palette(colours)
{
}

float ScoreGlyphPainter::px(double spaces) const
{
    return static_cast<float>(spaces * spaceInPixels);
}

void ScoreGlyphPainter::fillInSpaces(
    const juce::Path& pathInSpaces,
    double originX,
    double originY,
    juce::Colour colour) const
{
    juce::Path scaled = pathInSpaces;
    scaled.applyTransform(
        juce::AffineTransform::scale(static_cast<float>(spaceInPixels))
            .translated(static_cast<float>(originX), static_cast<float>(originY)));

    graphics.setColour(colour);
    graphics.fillPath(scaled);
}

void ScoreGlyphPainter::drawGlyph(
    smufl::Glyph glyph,
    double xInSpaces,
    double yInSpaces,
    double originX,
    double originY,
    juce::Colour colour,
    double scale) const
{
    const juce::Path& outline = font.outline(glyph);

    if (outline.isEmpty())
    {
        return;
    }

    // Scaled about the glyph's own origin first, so a grace notehead shrinks
    // towards the point it is anchored at rather than drifting off it.
    juce::Path placed = outline;
    placed.applyTransform(
        juce::AffineTransform::scale(static_cast<float>(scale))
            .translated(static_cast<float>(xInSpaces), static_cast<float>(yInSpaces)));

    fillInSpaces(placed, originX, originY, colour);
}

void ScoreGlyphPainter::drawStaffLines(
    const engraving::System& system,
    double originX,
    double originY) const
{
    graphics.setColour(palette.staffLines);

    for (int line = 0; line < engraving::staffLineCount; ++line)
    {
        const float y = static_cast<float>(originY) + px(static_cast<double>(line));

        graphics.fillRect(juce::Rectangle<float>(
            static_cast<float>(originX), y - (px(staffLineThickness) * 0.5f), px(system.width),
            px(staffLineThickness)));
    }
}

void ScoreGlyphPainter::drawNotehead(
    const engraving::NoteheadGlyph& glyph,
    double originX,
    double originY) const
{
    const double scale = glyph.isGrace ? graceScale : 1.0;
    const juce::Colour colour = glyph.isGrace ? palette.muted : palette.notation;

    const smufl::Glyph head = smufl::noteheadFor(glyph.head);
    const juce::Rectangle<float> headBounds = font.bounds(head);

    const double headWidth = static_cast<double>(headBounds.getWidth()) * scale;
    const double centreX = glyph.x + (static_cast<double>(headBounds.getCentreX()) * scale);
    const double y = yForSteps(glyph.position);

    // Ledger lines first, so a notehead sits on top of them rather than being
    // cut by one.
    //
    // At their **own** staff positions rather than at offsets from the note: a
    // ledger line continues the staff's own lines, so they are at -2, -4, ...
    // above and 10, 12, ... below, whatever the note sits on. A note on the
    // first one is *on* it; a note in the space beyond it is a half space past
    // it and still needs only that one. Centred on the notehead actually drawn,
    // because a whole note is wider than a black one and its ledger lines are
    // too.
    graphics.setColour(colour);

    const double leftExtension =
        (glyph.accidental == engraving::AccidentalKind::none
             ? ledgerLineExtension
             : ledgerLineExtensionBesideAccidental) *
        scale;
    const double ledgerLeft = centreX - (headWidth * 0.5) - leftExtension;
    const double ledgerRight = centreX + (headWidth * 0.5) + (ledgerLineExtension * scale);

    const auto ledgerAt = [&](StaffSteps steps)
    {
        graphics.fillRect(juce::Rectangle<float>(
            static_cast<float>(originX) + px(ledgerLeft),
            static_cast<float>(originY) + px(yForSteps(steps)) - (px(ledgerLineThickness) * 0.5f),
            px(ledgerRight - ledgerLeft), px(ledgerLineThickness)));
    };

    for (int above = 1; above <= glyph.ledger.above; ++above)
    {
        ledgerAt(engraving::topLineSteps - (above * 2));
    }

    for (int below = 1; below <= glyph.ledger.below; ++below)
    {
        ledgerAt(engraving::bottomLineSteps + (below * 2));
    }

    // SMuFL puts a notehead's origin at its left edge, on the line or space it
    // sits on -- which is exactly what the layout's x and staff position are.
    drawGlyph(head, glyph.x, y, originX, originY, colour, scale);

    if (const std::optional<smufl::Glyph> sign = smufl::accidentalFor(glyph.accidental))
    {
        // Right-aligned a small gap before the notehead. The signs differ in
        // width -- a double flat is nearly twice a natural -- so it is their
        // right edges that line up, not their origins.
        const double right = glyph.x - (accidentalGap * scale);
        const double left = right - (static_cast<double>(font.bounds(*sign).getRight()) * scale);

        drawGlyph(*sign, left, y, originX, originY, colour, scale);
    }

    // Augmentation dots go in a space, never on a line, so a dot on a
    // line-sitting note moves to the space above it.
    const StaffSteps dotPosition =
        engraving::isOnLine(glyph.position) ? glyph.position - 1 : glyph.position;

    for (int dot = 0; dot < glyph.dots; ++dot)
    {
        const double dotX =
            glyph.x + headWidth + ((dotGap + (static_cast<double>(dot) * dotPitch)) * scale);

        drawGlyph(
            smufl::Glyph::augmentationDot, dotX, yForSteps(dotPosition), originX, originY, colour,
            scale);
    }
}

void ScoreGlyphPainter::drawStem(const engraving::StemGlyph& glyph, double originX, double originY)
    const
{
    const double scale = glyph.isGrace ? graceScale : 1.0;
    const juce::Colour colour = glyph.isGrace ? palette.muted : palette.notation;

    const double attachY = yForSteps(glyph.attachPosition);
    const double thickness = glyph.thickness * scale;
    const double left = glyph.x - (thickness * 0.5);

    graphics.setColour(colour);
    graphics.fillRect(juce::Rectangle<float>(
        static_cast<float>(originX) + px(left),
        static_cast<float>(originY) + px(std::min(attachY, glyph.tipSpaces)), px(thickness),
        px(std::abs(attachY - glyph.tipSpaces))));

    // One glyph for every flag the stem carries: SMuFL draws a sixteenth's two
    // hooks as a single shape, not one hook twice. Its origin goes at the
    // stem's tip, on the stem's left edge, from where an up flag hangs down the
    // stem and a down flag rises up it. A beamed stem has no flags: the beam
    // stands in for them.
    if (const std::optional<smufl::Glyph> flag = smufl::flagFor(glyph.flags, glyph.up))
    {
        drawGlyph(*flag, left, glyph.tipSpaces, originX, originY, colour, scale);
    }
}

void ScoreGlyphPainter::drawBeam(const engraving::BeamGlyph& glyph, double originX, double originY)
    const
{
    // A beam is a parallelogram, not a rectangle: its two ends are vertical, so
    // that the stems meeting it do so flush. Sloping the ends with the bar --
    // which is what stroking a line would do -- leaves a wedge of white between
    // the beam and the stem at every joint. The layout has already reached the
    // ends out to the stems' outer edges along the slope.
    const double depth = glyph.up ? -glyph.thickness : glyph.thickness;

    juce::Path bar;
    bar.startNewSubPath(static_cast<float>(glyph.startX), static_cast<float>(glyph.startY));
    bar.lineTo(static_cast<float>(glyph.endX), static_cast<float>(glyph.endY));
    bar.lineTo(static_cast<float>(glyph.endX), static_cast<float>(glyph.endY + depth));
    bar.lineTo(static_cast<float>(glyph.startX), static_cast<float>(glyph.startY + depth));
    bar.closeSubPath();

    fillInSpaces(bar, originX, originY, palette.notation);
}

void ScoreGlyphPainter::drawRest(const engraving::RestGlyph& glyph, double originX, double originY)
    const
{
    // A rest's x is its centre -- the layout centres a bar-long rest in its
    // bar -- while a SMuFL rest's origin is its left edge, so the glyph is
    // moved left by half its own width.
    const smufl::Glyph rest = smufl::restFor(glyph.value);
    const double width = static_cast<double>(font.bounds(rest).getWidth());
    const double left = glyph.x - (width * 0.5);

    // SMuFL anchors each rest against a staff line the same way the layout
    // names one: a whole rest hangs from it, a half rest sits on it, and the
    // rest centre on it.
    drawGlyph(rest, left, yForSteps(glyph.position), originX, originY, palette.notation);

    for (int dot = 0; dot < glyph.dots; ++dot)
    {
        drawGlyph(
            smufl::Glyph::augmentationDot,
            left + width + dotGap + (static_cast<double>(dot) * dotPitch),
            yForSteps(engraving::middleLineSteps - 1), originX, originY, palette.notation);
    }
}

void ScoreGlyphPainter::drawKeyAccidental(
    const engraving::KeyAccidentalGlyph& glyph,
    double originX,
    double originY) const
{
    if (const std::optional<smufl::Glyph> sign = smufl::accidentalFor(glyph.kind))
    {
        drawGlyph(*sign, glyph.x, yForSteps(glyph.position), originX, originY, palette.notation);
    }
}

void ScoreGlyphPainter::drawBarline(
    const engraving::BarlineGlyph& glyph,
    double originX,
    double originY) const
{
    graphics.setColour(palette.notation);

    // A barline is drawn by its edges rather than its centre, so that a final
    // barline's thick line ends where the staff does instead of overhanging it
    // by half its own width.
    const auto lineFrom = [&](double left, double thickness)
    {
        graphics.fillRect(juce::Rectangle<float>(
            static_cast<float>(originX) + px(left), static_cast<float>(originY), px(thickness),
            px(engraving::staffHeightInSpaces)));
    };

    const auto dotsFrom = [&](double left)
    {
        for (const StaffSteps steps : repeatDotPositions)
        {
            drawGlyph(
                smufl::Glyph::repeatDot, left, yForSteps(steps), originX, originY,
                palette.notation);
        }
    };

    const double dotWidth = static_cast<double>(font.bounds(smufl::Glyph::repeatDot).getWidth());

    // The thin line of a heavy barline, measured from the thick one's edge.
    const double thinOffset = thickBarlineThickness + thinThickBarlineSeparation;

    switch (glyph.kind)
    {
    case engraving::BarlineKind::single:
        lineFrom(glyph.x - (thinBarlineThickness * 0.5), thinBarlineThickness);
        break;

    case engraving::BarlineKind::final:
    case engraving::BarlineKind::repeatEnd:
    {
        // Thick line flush against the barline's x, thin line before it.
        const double thickLeft = glyph.x - thickBarlineThickness;
        const double thinLeft = glyph.x - thinOffset - thinBarlineThickness;

        lineFrom(thickLeft, thickBarlineThickness);
        lineFrom(thinLeft, thinBarlineThickness);

        if (glyph.kind == engraving::BarlineKind::repeatEnd)
        {
            dotsFrom(thinLeft - repeatDotSeparation - dotWidth);
        }

        break;
    }

    case engraving::BarlineKind::repeatStart:
    {
        // The mirror image, facing into the music that follows.
        const double thinLeft = glyph.x + thinOffset;

        lineFrom(glyph.x, thickBarlineThickness);
        lineFrom(thinLeft, thinBarlineThickness);
        dotsFrom(thinLeft + thinBarlineThickness + repeatDotSeparation);
        break;
    }
    }
}

void ScoreGlyphPainter::drawClef(const engraving::ClefGlyph& glyph, double originX, double originY)
    const
{
    const std::optional<smufl::ClefGlyphChoice> choice = smufl::clefFor(glyph.clef);

    if (!choice.has_value())
    {
        return;
    }

    // SMuFL puts a clef's baseline on the line the clef names -- the G line
    // for a treble clef, the F line for a bass clef -- which is the position
    // the layout already computed from `clef.line`.
    const double left = glyph.x + clefLeadIn;
    const double y = yForSteps(glyph.position);

    drawGlyph(choice->glyph, left, y, originX, originY, palette.notation);

    if (choice->octaveDrawn || glyph.clef.octaveChange == 0)
    {
        return;
    }

    // An octave change SMuFL has no glyph for. Marked with its number in text
    // rather than dropped: a clef that claims a register the part does not
    // sound in is worse than an unidiomatic mark.
    const juce::Rectangle<float> bounds = font.bounds(choice->glyph);
    const double centreX = left + static_cast<double>(bounds.getCentreX());
    const bool below = glyph.clef.octaveChange < 0;
    const double markY = below ? y + static_cast<double>(bounds.getBottom())
                               : y + static_cast<double>(bounds.getY()) - 1.3;

    graphics.setColour(palette.muted);
    graphics.setFont(juce::FontOptions(px(1.1)));
    graphics.drawText(
        juce::String(std::abs(glyph.clef.octaveChange) * 7 + 1),
        juce::Rectangle<float>(
            static_cast<float>(originX) + px(centreX - 1.0),
            static_cast<float>(originY) + px(markY), px(2.0), px(1.3)),
        juce::Justification::centred);
}

void ScoreGlyphPainter::drawTie(const engraving::TieGlyph& glyph, double originX, double originY)
    const
{
    const double noteheadWidth =
        static_cast<double>(font.bounds(smufl::Glyph::noteheadBlack).getRight());

    const double startX = glyph.startX + noteheadWidth;
    const double endX = glyph.endX;

    if (endX <= startX)
    {
        return;
    }

    const double y = yForSteps(glyph.position) + (glyph.above ? -0.62 : 0.62);
    const double sign = glyph.above ? -1.0 : 1.0;
    const double midX = (startX + endX) * 0.5;

    // A crescent rather than a stroked arc: two curves with a common end
    // point, the outer bowing further than the inner, so the tie is thickest
    // in the middle and tapers to its ends.
    const double outerBow = sign * (0.85 + (tieMidpointThickness * 0.5));
    const double innerBow = sign * (0.85 - (tieMidpointThickness * 0.5));
    const double endHalf = tieEndpointThickness * 0.5;

    juce::Path tie;
    tie.startNewSubPath(static_cast<float>(startX), static_cast<float>(y - endHalf));
    tie.quadraticTo(
        static_cast<float>(midX), static_cast<float>(y + (outerBow * 2.0) - endHalf),
        static_cast<float>(endX), static_cast<float>(y - endHalf));
    tie.lineTo(static_cast<float>(endX), static_cast<float>(y + endHalf));
    tie.quadraticTo(
        static_cast<float>(midX), static_cast<float>(y + (innerBow * 2.0) + endHalf),
        static_cast<float>(startX), static_cast<float>(y + endHalf));
    tie.closeSubPath();

    fillInSpaces(tie, originX, originY, palette.notation);
}

void ScoreGlyphPainter::drawTimeSignature(
    const engraving::TimeSignatureGlyph& glyph,
    double originX,
    double originY) const
{
    const std::vector<smufl::Glyph> upper = smufl::timeSignatureDigits(glyph.beats);
    const std::vector<smufl::Glyph> lower = smufl::timeSignatureDigits(glyph.beatType);

    const double upperWidth = runWidth(font, upper);
    const double lowerWidth = runWidth(font, lower);
    const double widest = std::max(upperWidth, lowerWidth);

    // The two numbers are centred on each other, and the pair starts at the
    // left of the room the layout reserved. A 12 over an 8 is centred as a
    // column, not left-aligned as two lines of text.
    const double centreX = glyph.x + timeSignatureLeadIn + (widest * 0.5);

    const auto drawNumber =
        [&](const std::vector<smufl::Glyph>& digits, double width, StaffSteps position)
    {
        double x = centreX - (width * 0.5);

        for (const smufl::Glyph digit : digits)
        {
            drawGlyph(digit, x, yForSteps(position), originX, originY, palette.notation);
            x += static_cast<double>(font.bounds(digit).getRight());
        }
    };

    drawNumber(upper, upperWidth, upperTimeSignatureNumber);
    drawNumber(lower, lowerWidth, lowerTimeSignatureNumber);
}

void ScoreGlyphPainter::drawLyric(
    const engraving::LyricGlyph& glyph,
    double originX,
    double originY) const
{
    const double rowOffset =
        engraving::staffHeightInSpaces + 1.55 + (static_cast<double>(glyph.verse - 1) * 1.8);

    juce::String text{glyph.text};

    if (glyph.hyphenAfter)
    {
        text += "-";
    }
    else if (glyph.extend)
    {
        // A melisma: the syllable is held across the notes that follow. Shown
        // as a trailing rule rather than a drawn extender line, which would
        // need the x of the note the melisma ends on.
        text += " _";
    }

    graphics.setColour(palette.notation);
    graphics.setFont(juce::FontOptions(px(1.45)));
    graphics.drawText(
        text,
        juce::Rectangle<float>(
            static_cast<float>(originX + px(glyph.x)) - px(2.0),
            static_cast<float>(originY + px(rowOffset)), px(4.0), px(1.7)),
        juce::Justification::centredTop);
}

void ScoreGlyphPainter::drawText(
    const engraving::TextGlyph& glyph,
    double originX,
    double originY,
    int lyricVerseCount) const
{
    double offset = 0.0;
    juce::Colour colour = palette.notation;
    int style = juce::Font::plain;
    double size = 1.4;
    juce::Justification justification = juce::Justification::centredLeft;

    switch (glyph.role)
    {
    case engraving::TextRole::tempo:
        offset = -2.1;
        style = juce::Font::bold;
        break;

    case engraving::TextRole::dynamic:
        offset =
            engraving::staffHeightInSpaces + 1.5 + (static_cast<double>(lyricVerseCount) * 1.8);
        style = juce::Font::bold | juce::Font::italic;
        colour = palette.accent;
        break;

    case engraving::TextRole::measureNumber:
        offset = -3.4;
        colour = palette.muted;
        size = 1.1;
        break;
    }

    graphics.setColour(colour);
    graphics.setFont(juce::FontOptions(px(size), style));
    graphics.drawText(
        juce::String(glyph.text),
        juce::Rectangle<float>(
            static_cast<float>(originX + px(glyph.x)), static_cast<float>(originY + px(offset)),
            px(14.0), px(1.7)),
        justification);
}

void ScoreGlyphPainter::drawSystem(
    const engraving::System& system,
    double originX,
    double originY,
    int lyricVerseCount) const
{
    drawStaffLines(system, originX, originY);

    for (const engraving::BarlineGlyph& glyph : system.barlines)
    {
        drawBarline(glyph, originX, originY);
    }

    for (const engraving::ClefGlyph& glyph : system.clefs)
    {
        drawClef(glyph, originX, originY);
    }

    for (const engraving::KeyAccidentalGlyph& glyph : system.keyAccidentals)
    {
        drawKeyAccidental(glyph, originX, originY);
    }

    for (const engraving::TimeSignatureGlyph& glyph : system.timeSignatures)
    {
        drawTimeSignature(glyph, originX, originY);
    }

    for (const engraving::RestGlyph& glyph : system.rests)
    {
        drawRest(glyph, originX, originY);
    }

    // Stems before noteheads, so a notehead covers the end of its own stem
    // rather than the stem cutting across it.
    for (const engraving::StemGlyph& glyph : system.stems)
    {
        drawStem(glyph, originX, originY);
    }

    for (const engraving::BeamGlyph& glyph : system.beams)
    {
        drawBeam(glyph, originX, originY);
    }

    for (const engraving::NoteheadGlyph& glyph : system.noteheads)
    {
        drawNotehead(glyph, originX, originY);
    }

    for (const engraving::TieGlyph& glyph : system.ties)
    {
        drawTie(glyph, originX, originY);
    }

    for (const engraving::LyricGlyph& glyph : system.lyrics)
    {
        drawLyric(glyph, originX, originY);
    }

    for (const engraving::TextGlyph& glyph : system.texts)
    {
        drawText(glyph, originX, originY, lyricVerseCount);
    }
}
