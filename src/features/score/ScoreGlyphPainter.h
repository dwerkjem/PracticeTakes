#pragma once

#include <JuceHeader.h>

#include "../../application/theme/Theme.h"

#include "MusicFont.h"
#include "SmuflGlyphs.h"
#include "engraving/ScoreLayout.h"

// Drawing the glyphs a `ScoreLayout` names.
//
// Everything JUCE-facing about notation is here and nowhere else, which is the
// point of the split: the layout decides *where* every glyph goes and is unit
// tested without a display, and this file decides what each one looks like.
//
// Every symbol with a designed shape -- noteheads, clefs, accidentals, flags,
// rests, time signature digits, dots -- is a glyph from Bravura, through
// `MusicFont`. What SMuFL says to draw as lines is drawn as lines, at Bravura's
// published thicknesses: staff lines, ledger lines, stems, beams and barlines,
// each as long as the layout says, which no fixed glyph could be.
//
// The layout needed almost nothing to make this work, because it already
// measured in staff spaces and SMuFL fixes a font at four of them to the em.
// The two numbers that did change -- the notehead's width and the stem's
// thickness -- now match the font, and MusicFontTests says so.
class ScoreGlyphPainter
{
  public:
    // The colours notation is drawn in. Separate from `AppPalette` because a
    // staff is not a panel: the lines want to be lighter than the noteheads, or
    // the staff reads as louder than the music on it.
    struct Palette
    {
        juce::Colour staffLines;
        juce::Colour notation;
        juce::Colour muted;
        juce::Colour accent;
    };

    // `pixelsPerSpace` is the whole of zoom: one staff space, in pixels. The
    // font is borrowed, and must outlive the painter -- which, for a painter
    // made and dropped inside one `paint` call, it always does.
    ScoreGlyphPainter(
        juce::Graphics& graphicsToUse,
        const MusicFont& fontToUse,
        double pixelsPerSpace,
        Palette colours);

    // Draw one system, with `originX`/`originY` the pixel position of its left
    // edge and its top staff line.
    void
    drawSystem(const engraving::System& system, double originX, double originY, int lyricVerseCount)
        const;

  private:
    [[nodiscard]] float px(double spaces) const;

    void drawStaffLines(const engraving::System& system, double originX, double originY) const;

    void drawNotehead(const engraving::NoteheadGlyph& glyph, double originX, double originY) const;
    void drawStem(const engraving::StemGlyph& glyph, double originX, double originY) const;
    void drawBeam(const engraving::BeamGlyph& glyph, double originX, double originY) const;
    void drawRest(const engraving::RestGlyph& glyph, double originX, double originY) const;
    void drawBarline(const engraving::BarlineGlyph& glyph, double originX, double originY) const;
    void drawClef(const engraving::ClefGlyph& glyph, double originX, double originY) const;
    void drawTie(const engraving::TieGlyph& glyph, double originX, double originY) const;

    void drawKeyAccidental(
        const engraving::KeyAccidentalGlyph& glyph,
        double originX,
        double originY) const;

    // One music-font glyph with its SMuFL origin at (`xInSpaces`,
    // `yInSpaces`), relative to the system's left edge and top staff line, and
    // scaled about that origin.
    void drawGlyph(
        smufl::Glyph glyph,
        double xInSpaces,
        double yInSpaces,
        double originX,
        double originY,
        juce::Colour colour,
        double scale = 1.0) const;

    void drawTimeSignature(
        const engraving::TimeSignatureGlyph& glyph,
        double originX,
        double originY) const;

    void drawLyric(const engraving::LyricGlyph& glyph, double originX, double originY) const;

    void
    drawText(const engraving::TextGlyph& glyph, double originX, double originY, int lyricVerseCount)
        const;

    void fillInSpaces(
        const juce::Path& pathInSpaces,
        double originX,
        double originY,
        juce::Colour colour) const;

    juce::Graphics& graphics;
    const MusicFont& font;
    double spaceInPixels;
    Palette palette;
};

// The notation palette for a theme.
[[nodiscard]] ScoreGlyphPainter::Palette scoreNotationPaletteFor(Theme theme);
