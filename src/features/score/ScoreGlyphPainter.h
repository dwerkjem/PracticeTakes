#pragma once

#include <JuceHeader.h>

#include "../../application/theme/Theme.h"

#include "engraving/ScoreLayout.h"

// Drawing the glyphs a `ScoreLayout` names.
//
// Everything JUCE-facing about notation is here and nowhere else, which is the
// point of the split: the layout decides *where* every glyph goes and is unit
// tested without a display, and this file decides what each one looks like.
//
// The glyphs are built from `juce::Path` primitives rather than from a SMuFL
// music font. That is a deliberate first pass -- Bravura would look far better
// and brings a licence, a binary asset and a packaging question with it. The
// seam is here: a font would replace this file and change no layout at all,
// because the layout already works in staff spaces, which is the unit a music
// font is designed in.
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

    // `pixelsPerSpace` is the whole of zoom: one staff space, in pixels.
    ScoreGlyphPainter(juce::Graphics& graphicsToUse, double pixelsPerSpace, Palette colours);

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

    void drawAccidental(
        engraving::AccidentalKind kind,
        double centreXInSpaces,
        engraving::StaffSteps position,
        double originX,
        double originY) const;

    void drawTimeSignature(
        const engraving::TimeSignatureGlyph& glyph,
        double originX,
        double originY) const;

    void drawLyric(const engraving::LyricGlyph& glyph, double originX, double originY) const;

    void
    drawText(const engraving::TextGlyph& glyph, double originX, double originY, int lyricVerseCount)
        const;

    // Build the stroked outline of a path given in staff spaces, already
    // translated to its place on screen.
    void strokeInSpaces(
        const juce::Path& pathInSpaces,
        double originX,
        double originY,
        double thicknessInSpaces,
        juce::Colour colour) const;

    void fillInSpaces(
        const juce::Path& pathInSpaces,
        double originX,
        double originY,
        juce::Colour colour) const;

    juce::Graphics& graphics;
    double spaceInPixels;
    Palette palette;
};

// The notation palette for a theme.
[[nodiscard]] ScoreGlyphPainter::Palette scoreNotationPaletteFor(Theme theme);
