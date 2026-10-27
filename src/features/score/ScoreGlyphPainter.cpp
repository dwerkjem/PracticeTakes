#include "ScoreGlyphPainter.h"

#include <cmath>

namespace
{
using engraving::AccidentalKind;
using engraving::StaffSteps;

// Stroke weights, in staff spaces. A staff line is thin, a stem heavier, a
// final barline heavier again -- the proportions a reader expects even when the
// glyph shapes are approximations.
constexpr double staffLineThickness = 0.09;
constexpr double stemThickness = 0.13;
constexpr double barlineThickness = 0.12;
constexpr double heavyBarlineThickness = 0.42;
constexpr double ledgerThickness = 0.11;

constexpr double noteheadWidthInSpaces = 1.28;
constexpr double noteheadHeightInSpaces = 0.92;

// Noteheads are ellipses tilted anticlockwise, which is what makes a chord of
// seconds readable and what every engraved notehead does.
constexpr float noteheadTiltRadians = -0.35f;

constexpr double ledgerHalfWidth = 0.92;

[[nodiscard]] double yForSteps(StaffSteps steps)
{
    return engraving::spacesBelowTopLine(steps);
}

// An ellipse notehead, centred on the origin, tilted.
[[nodiscard]] juce::Path noteheadPath()
{
    juce::Path head;
    head.addEllipse(
        static_cast<float>(-noteheadWidthInSpaces * 0.5),
        static_cast<float>(-noteheadHeightInSpaces * 0.5),
        static_cast<float>(noteheadWidthInSpaces), static_cast<float>(noteheadHeightInSpaces));
    head.applyTransform(juce::AffineTransform::rotation(noteheadTiltRadians));

    return head;
}

// The accidental signs. Each is built around its own origin, which sits where
// the sign centres on the staff -- the middle of a sharp or a natural, and the
// *bowl* of a flat rather than the top of its upright.
[[nodiscard]] juce::Path sharpPath()
{
    juce::Path sign;

    // Two uprights and two thicker crossbars, slanted upwards to the right.
    sign.addRectangle(-0.24f, -0.78f, 0.1f, 1.56f);
    sign.addRectangle(0.16f, -0.92f, 0.1f, 1.56f);

    for (const float offset : {-0.26f, 0.22f})
    {
        juce::Path bar;
        bar.addRectangle(-0.46f, offset, 0.92f, 0.2f);
        bar.applyTransform(juce::AffineTransform::shear(0.0f, -0.22f));
        sign.addPath(bar);
    }

    return sign;
}

[[nodiscard]] juce::Path flatPath()
{
    juce::Path sign;
    sign.addRectangle(-0.26f, -1.25f, 0.11f, 1.9f);

    juce::Path bowl;
    bowl.startNewSubPath(-0.15f, -0.2f);
    bowl.cubicTo(0.62f, -0.44f, 0.68f, 0.5f, -0.15f, 0.65f);
    sign.addPath(bowl);

    return sign;
}

[[nodiscard]] juce::Path naturalPath()
{
    juce::Path sign;
    sign.addRectangle(-0.24f, -1.0f, 0.1f, 1.6f);
    sign.addRectangle(0.14f, -0.6f, 0.1f, 1.6f);

    for (const float offset : {-0.3f, 0.18f})
    {
        juce::Path bar;
        bar.addRectangle(-0.24f, offset, 0.48f, 0.19f);
        bar.applyTransform(juce::AffineTransform::shear(0.0f, -0.26f));
        sign.addPath(bar);
    }

    return sign;
}

[[nodiscard]] juce::Path doubleSharpPath()
{
    // The conventional glyph is a saltire -- two crossed strokes -- not two
    // sharps side by side.
    juce::Path sign;

    for (const float rotation : {0.78f, -0.78f})
    {
        juce::Path stroke;
        stroke.addRectangle(-0.5f, -0.11f, 1.0f, 0.22f);
        stroke.applyTransform(juce::AffineTransform::rotation(rotation));
        sign.addPath(stroke);
    }

    return sign;
}

[[nodiscard]] juce::Path accidentalPath(AccidentalKind kind)
{
    switch (kind)
    {
    case AccidentalKind::sharp:
        return sharpPath();
    case AccidentalKind::flat:
        return flatPath();
    case AccidentalKind::natural:
        return naturalPath();
    case AccidentalKind::doubleSharp:
        return doubleSharpPath();
    case AccidentalKind::doubleFlat:
    {
        juce::Path sign = flatPath();
        juce::Path second = flatPath();
        second.applyTransform(juce::AffineTransform::translation(0.62f, 0.0f));
        sign.addPath(second);

        return sign;
    }
    case AccidentalKind::none:
        break;
    }

    return {};
}

// A treble clef, as a stroked outline centred on the line the sign names.
[[nodiscard]] juce::Path gClefPath()
{
    juce::Path clef;

    clef.startNewSubPath(0.72f, -2.75f);
    clef.cubicTo(-0.55f, -2.15f, -0.78f, -0.6f, 0.42f, 0.0f);
    clef.cubicTo(1.6f, 0.6f, 1.3f, 2.05f, 0.0f, 2.05f);
    clef.cubicTo(-0.98f, 2.05f, -1.08f, 1.0f, -0.32f, 0.76f);

    clef.startNewSubPath(0.6f, -2.6f);
    clef.lineTo(0.16f, 3.3f);
    clef.cubicTo(0.12f, 4.1f, -0.6f, 4.2f, -0.92f, 3.72f);

    return clef;
}

[[nodiscard]] juce::Path fClefStrokePath()
{
    juce::Path clef;

    clef.startNewSubPath(-0.85f, -0.85f);
    clef.cubicTo(0.25f, -1.65f, 1.35f, -0.95f, 1.28f, 0.15f);
    clef.cubicTo(1.2f, 1.6f, 0.0f, 2.55f, -1.1f, 2.95f);

    return clef;
}

[[nodiscard]] juce::Path cClefPath()
{
    juce::Path clef;

    // Upper and lower bowls meeting at the line the sign names.
    clef.startNewSubPath(-0.35f, -2.0f);
    clef.cubicTo(1.35f, -1.95f, 1.3f, -0.2f, 0.0f, -0.05f);

    clef.startNewSubPath(-0.35f, 2.0f);
    clef.cubicTo(1.35f, 1.95f, 1.3f, 0.2f, 0.0f, 0.05f);

    return clef;
}

[[nodiscard]] juce::Path quarterRestPath()
{
    juce::Path rest;

    rest.startNewSubPath(-0.28f, -1.5f);
    rest.lineTo(0.34f, -0.52f);
    rest.lineTo(-0.22f, 0.12f);
    rest.lineTo(0.4f, 0.98f);
    rest.cubicTo(-0.08f, 0.62f, -0.46f, 1.02f, 0.06f, 1.5f);

    return rest;
}

[[nodiscard]] juce::String timeSignatureDigits(int value)
{
    return juce::String(value);
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
    double pixelsPerSpace,
    Palette colours)
    : graphics(graphicsToUse), spaceInPixels(pixelsPerSpace), palette(colours)
{
}

float ScoreGlyphPainter::px(double spaces) const
{
    return static_cast<float>(spaces * spaceInPixels);
}

void ScoreGlyphPainter::strokeInSpaces(
    const juce::Path& pathInSpaces,
    double originX,
    double originY,
    double thicknessInSpaces,
    juce::Colour colour) const
{
    juce::Path scaled = pathInSpaces;
    scaled.applyTransform(
        juce::AffineTransform::scale(static_cast<float>(spaceInPixels))
            .translated(static_cast<float>(originX), static_cast<float>(originY)));

    graphics.setColour(colour);
    graphics.strokePath(
        scaled,
        juce::PathStrokeType(
            px(thicknessInSpaces), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
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
    const double scale = glyph.isGrace ? 0.66 : 1.0;

    const double centreX = originX + px(glyph.x) + (px(noteheadWidthInSpaces * scale) * 0.5);
    const double centreY = originY + px(yForSteps(glyph.position));

    // Ledger lines first, so a notehead sits on top of them rather than being
    // cut by one.
    //
    // At their **own** staff positions rather than at offsets from the note: a
    // ledger line continues the staff's own lines, so they are at -2, -4, ...
    // above and 10, 12, ... below, whatever the note sits on. A note on the
    // first one is *on* it; a note in the space beyond it is a half space past
    // it and still needs only that one.
    graphics.setColour(palette.notation);

    const auto ledgerAt = [&](engraving::StaffSteps steps)
    {
        const double y = originY + px(yForSteps(steps));

        graphics.fillRect(juce::Rectangle<float>(
            static_cast<float>(centreX) - px(ledgerHalfWidth * scale),
            static_cast<float>(y) - (px(ledgerThickness) * 0.5f), px(ledgerHalfWidth * 2.0 * scale),
            px(ledgerThickness)));
    };

    for (int above = 1; above <= glyph.ledger.above; ++above)
    {
        ledgerAt(engraving::topLineSteps - (above * 2));
    }

    for (int below = 1; below <= glyph.ledger.below; ++below)
    {
        ledgerAt(engraving::bottomLineSteps + (below * 2));
    }

    juce::Path head = noteheadPath();
    head.applyTransform(juce::AffineTransform::scale(static_cast<float>(scale)));

    juce::Path placed = head;
    placed.applyTransform(
        juce::AffineTransform::scale(static_cast<float>(spaceInPixels))
            .translated(static_cast<float>(centreX), static_cast<float>(centreY)));

    const juce::Colour colour = glyph.isGrace ? palette.muted : palette.notation;

    graphics.setColour(colour);

    switch (glyph.head)
    {
    case engraving::NoteheadKind::filled:
        graphics.fillPath(placed);
        break;

    case engraving::NoteheadKind::half:
    case engraving::NoteheadKind::whole:
        graphics.strokePath(placed, juce::PathStrokeType(px(0.17 * scale)));
        break;

    case engraving::NoteheadKind::breve:
        graphics.strokePath(placed, juce::PathStrokeType(px(0.17 * scale)));

        // The two vertical strokes that distinguish a breve from a whole note.
        for (const double side : {-1.0, 1.0})
        {
            graphics.fillRect(juce::Rectangle<float>(
                static_cast<float>(centreX + (side * px(noteheadWidthInSpaces * 0.62))) -
                    (px(0.12) * 0.5f),
                static_cast<float>(centreY) - px(0.62), px(0.12), px(1.24)));
        }

        break;
    }

    if (glyph.accidental != engraving::AccidentalKind::none)
    {
        drawAccidental(glyph.accidental, glyph.x - 0.75, glyph.position, originX, originY);
    }

    // Augmentation dots go in a space, never on a line, so a dot on a
    // line-sitting note moves to the space above it.
    const engraving::StaffSteps dotPosition =
        engraving::isOnLine(glyph.position) ? glyph.position - 1 : glyph.position;

    for (int dot = 0; dot < glyph.dots; ++dot)
    {
        const double dotX = centreX + px(0.85 + (static_cast<double>(dot) * 0.48));

        graphics.fillEllipse(
            static_cast<float>(dotX) - px(0.15),
            static_cast<float>(originY + px(yForSteps(dotPosition))) - px(0.15), px(0.3), px(0.3));
    }
}

void ScoreGlyphPainter::drawStem(const engraving::StemGlyph& glyph, double originX, double originY)
    const
{
    const double scale = glyph.isGrace ? 0.66 : 1.0;

    const double x = originX + px(glyph.x);
    const double attachY = originY + px(yForSteps(glyph.attachPosition));
    const double tipY = originY + px(yForSteps(glyph.tipPosition));

    graphics.setColour(glyph.isGrace ? palette.muted : palette.notation);
    graphics.fillRect(juce::Rectangle<float>(
        static_cast<float>(x) - (px(stemThickness * scale) * 0.5f),
        static_cast<float>(std::min(attachY, tipY)), px(stemThickness * scale),
        static_cast<float>(std::abs(attachY - tipY))));

    // Flags, not beams. One hook per flag, stacked back along the stem from the
    // tip, curling away from the notehead.
    for (int flag = 0; flag < glyph.flags; ++flag)
    {
        const double along = static_cast<double>(flag) * 0.78 * scale;

        juce::Path hook;
        const float startY = static_cast<float>(glyph.up ? along : -along);
        const float sweep = static_cast<float>(glyph.up ? 1.15 : -1.15);

        hook.startNewSubPath(0.0f, startY);
        hook.cubicTo(
            static_cast<float>(0.72 * scale), startY + (sweep * 0.25f),
            static_cast<float>(0.78 * scale), startY + (sweep * 0.62f),
            static_cast<float>(0.2 * scale), startY + sweep);

        juce::Path placed = hook;
        placed.applyTransform(juce::AffineTransform::scale(static_cast<float>(spaceInPixels))
                                  .translated(static_cast<float>(x), static_cast<float>(tipY)));

        graphics.strokePath(
            placed,
            juce::PathStrokeType(
                px(0.17 * scale), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

void ScoreGlyphPainter::drawRest(const engraving::RestGlyph& glyph, double originX, double originY)
    const
{
    const double x = originX + px(glyph.x);
    const double y = originY + px(yForSteps(glyph.position));

    graphics.setColour(palette.notation);

    switch (glyph.value)
    {
    case engraving::WrittenValue::breve:
    case engraving::WrittenValue::whole:
        // Hangs below the fourth line.
        graphics.fillRect(juce::Rectangle<float>(
            static_cast<float>(x) - px(0.62), static_cast<float>(y), px(1.24), px(0.5)));
        break;

    case engraving::WrittenValue::half:
        // Sits on the middle line.
        graphics.fillRect(juce::Rectangle<float>(
            static_cast<float>(x) - px(0.62), static_cast<float>(y) - px(0.5), px(1.24), px(0.5)));
        break;

    case engraving::WrittenValue::quarter:
        strokeInSpaces(quarterRestPath(), x, y, 0.21, palette.notation);
        break;

    default:
    {
        // An eighth rest and shorter: a slanting stem with one hook per flag.
        const int flags = engraving::flagCountFor(glyph.value);
        const double top = -0.55 - (static_cast<double>(flags - 1) * 0.72);

        juce::Path rest;
        rest.startNewSubPath(0.3f, static_cast<float>(top));
        rest.lineTo(-0.12f, 1.1f);

        strokeInSpaces(rest, x, y, 0.16, palette.notation);

        for (int flag = 0; flag < flags; ++flag)
        {
            const double hookY = top + (static_cast<double>(flag) * 0.72);

            graphics.fillEllipse(
                static_cast<float>(x) + px(0.14), static_cast<float>(y + px(hookY)) - px(0.17),
                px(0.36), px(0.36));

            juce::Path tail;
            tail.startNewSubPath(0.3f, static_cast<float>(hookY));
            tail.cubicTo(
                0.05f, static_cast<float>(hookY - 0.3), -0.32f, static_cast<float>(hookY - 0.28),
                -0.52f, static_cast<float>(hookY - 0.08));

            strokeInSpaces(tail, x, y, 0.13, palette.notation);
        }

        break;
    }
    }

    for (int dot = 0; dot < glyph.dots; ++dot)
    {
        graphics.setColour(palette.notation);
        graphics.fillEllipse(
            static_cast<float>(x) + px(0.78 + (static_cast<double>(dot) * 0.48)),
            static_cast<float>(originY) + px(yForSteps(engraving::middleLineSteps - 1)) - px(0.15),
            px(0.3), px(0.3));
    }
}

void ScoreGlyphPainter::drawAccidental(
    engraving::AccidentalKind kind,
    double centreXInSpaces,
    engraving::StaffSteps position,
    double originX,
    double originY) const
{
    const juce::Path sign = accidentalPath(kind);

    if (sign.isEmpty())
    {
        return;
    }

    fillInSpaces(
        sign, originX + px(centreXInSpaces), originY + px(yForSteps(position)), palette.notation);
}

void ScoreGlyphPainter::drawBarline(
    const engraving::BarlineGlyph& glyph,
    double originX,
    double originY) const
{
    const double x = originX + px(glyph.x);
    const double top = originY;
    const float height = px(engraving::staffHeightInSpaces);

    graphics.setColour(palette.notation);

    const auto verticalAt = [&](double offsetInSpaces, double thickness)
    {
        graphics.fillRect(juce::Rectangle<float>(
            static_cast<float>(x + px(offsetInSpaces)) - (px(thickness) * 0.5f),
            static_cast<float>(top), px(thickness), height));
    };

    const auto dotsAt = [&](double offsetInSpaces)
    {
        for (const engraving::StaffSteps steps : {3, 5})
        {
            graphics.fillEllipse(
                static_cast<float>(x + px(offsetInSpaces)) - px(0.17),
                static_cast<float>(originY + px(yForSteps(steps))) - px(0.17), px(0.34), px(0.34));
        }
    };

    switch (glyph.kind)
    {
    case engraving::BarlineKind::single:
        verticalAt(0.0, barlineThickness);
        break;

    case engraving::BarlineKind::final:
        verticalAt(-0.42, barlineThickness);
        verticalAt(0.0, heavyBarlineThickness);
        break;

    case engraving::BarlineKind::repeatEnd:
        verticalAt(-0.42, barlineThickness);
        verticalAt(0.0, heavyBarlineThickness);
        dotsAt(-0.85);
        break;

    case engraving::BarlineKind::repeatStart:
        verticalAt(0.0, heavyBarlineThickness);
        verticalAt(0.42, barlineThickness);
        dotsAt(0.85);
        break;
    }
}

void ScoreGlyphPainter::drawClef(const engraving::ClefGlyph& glyph, double originX, double originY)
    const
{
    const double x = originX + px(glyph.x) + px(1.2);
    const double y = originY + px(yForSteps(glyph.position));

    if (glyph.clef.sign == "G")
    {
        strokeInSpaces(gClefPath(), x, y, 0.19, palette.notation);
    }
    else if (glyph.clef.sign == "F")
    {
        strokeInSpaces(fClefStrokePath(), x, y, 0.3, palette.notation);

        graphics.setColour(palette.notation);
        graphics.fillEllipse(
            static_cast<float>(x) - px(1.06), static_cast<float>(y) - px(1.06), px(0.56), px(0.56));

        for (const double offset : {-0.5, 0.5})
        {
            graphics.fillEllipse(
                static_cast<float>(x) + px(1.58), static_cast<float>(y + px(offset)) - px(0.17),
                px(0.34), px(0.34));
        }
    }
    else if (glyph.clef.sign == "C")
    {
        strokeInSpaces(cClefPath(), x, y, 0.26, palette.notation);

        graphics.setColour(palette.notation);
        graphics.fillRect(juce::Rectangle<float>(
            static_cast<float>(x) - px(1.15), static_cast<float>(y) - px(2.0), px(0.3), px(4.0)));
        graphics.fillRect(juce::Rectangle<float>(
            static_cast<float>(x) - px(0.72), static_cast<float>(y) - px(2.0), px(0.14), px(4.0)));
    }

    // The octave number a transposing clef carries. Drawn because a vocal tenor
    // clef and a treble clef are otherwise the same picture of two different
    // sounding registers.
    if (glyph.clef.octaveChange != 0)
    {
        graphics.setColour(palette.muted);
        graphics.setFont(juce::FontOptions(px(1.1)));
        graphics.drawText(
            "8",
            juce::Rectangle<float>(
                static_cast<float>(x) - px(0.8),
                static_cast<float>(y) + px(glyph.clef.octaveChange < 0 ? 2.1 : -3.4), px(1.6),
                px(1.3)),
            juce::Justification::centred);
    }
}

void ScoreGlyphPainter::drawTie(const engraving::TieGlyph& glyph, double originX, double originY)
    const
{
    const double startX = originX + px(glyph.startX) + px(noteheadWidthInSpaces);
    const double endX = originX + px(glyph.endX);

    if (endX <= startX)
    {
        return;
    }

    const double y = originY + px(yForSteps(glyph.position)) + (glyph.above ? -px(0.62) : px(0.62));

    const double bow = glyph.above ? -px(0.85) : px(0.85);

    juce::Path curve;
    curve.startNewSubPath(static_cast<float>(startX), static_cast<float>(y));
    curve.quadraticTo(
        static_cast<float>((startX + endX) * 0.5), static_cast<float>(y + bow),
        static_cast<float>(endX), static_cast<float>(y));

    graphics.setColour(palette.notation);
    graphics.strokePath(
        curve, juce::PathStrokeType(
                   px(0.11), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void ScoreGlyphPainter::drawTimeSignature(
    const engraving::TimeSignatureGlyph& glyph,
    double originX,
    double originY) const
{
    const double x = originX + px(glyph.x);

    graphics.setColour(palette.notation);
    graphics.setFont(juce::FontOptions(px(2.15), juce::Font::bold));

    graphics.drawText(
        timeSignatureDigits(glyph.beats),
        juce::Rectangle<float>(
            static_cast<float>(x), static_cast<float>(originY) - px(0.25), px(2.4), px(2.3)),
        juce::Justification::centred);

    graphics.drawText(
        timeSignatureDigits(glyph.beatType),
        juce::Rectangle<float>(
            static_cast<float>(x), static_cast<float>(originY) + px(1.95), px(2.4), px(2.3)),
        juce::Justification::centred);
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
        drawAccidental(glyph.kind, glyph.x + 0.4, glyph.position, originX, originY);
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
