#pragma once

#include <cstddef>
#include <vector>

#include "GlyphMetrics.h"
#include "StaffPosition.h"

#include "platform/score/Diagnostic.h"

// The result of laying a score out: what to draw, and where.
//
// Everything positional lives here and nowhere in `Score` -- invariant 10 says
// the model holds no coordinates, so this is where they are allowed to exist.
//
// **Units.** Horizontal positions are in staff spaces from the left edge of the
// system. Vertical positions are in *staff steps* within their own staff, as
// `StaffPosition.h` defines them, with the staff's own offset carried on the
// staff. Nothing here is in pixels: the painting code multiplies by one scale
// and that is the only place a pixel appears. Laying out twice at two sizes
// must produce the same numbers, which is a thing a test can check and a thing
// that stops being true the moment a font size leaks in here.
//
// No JUCE, no font, no colours.
namespace score::engraving
{
// One glyph to draw. Noteheads, rests, clefs, accidentals, time signature
// digits and the brace are all this -- the painter does not need to know which
// it is holding, and the layout has already decided everything that differs.
struct PlacedGlyph
{
    Glyph glyph = Glyph::noteheadBlack;

    // Staff spaces from the left edge of the system.
    double x = 0.0;

    // Staff steps from this staff's bottom line, positive upward.
    int staffStep = 0;
};

// A stem: the vertical line beside a notehead that, with its flag or beam, is
// the whole of how a reader gets duration off the page.
//
// Positions only, like everything here. Thickness is not carried because it is
// the same for every stem and is already in `engravingDefaults`; a painter that
// needed a per-stem thickness would mean the font had stopped being the source
// of it.
struct PlacedStem
{
    // Centre of the stem, in staff spaces from the left edge of the system.
    double x = 0.0;

    // The notehead end and the free end, in staff steps. Doubles because a stem
    // in a beamed group ends on the beam, which is not generally a whole step.
    double fromStep = 0.0;
    double toStep = 0.0;
};

// A ledger line: a short horizontal rule through or beside a notehead.
struct PlacedLedgerLine
{
    double x = 0.0;
    double width = 0.0;
    int staffStep = 0;
};

// One staff of one part, within one system.
struct StaffLayout
{
    // MusicXML's staff number within the part, from 1. Kept rather than
    // reduced to an index so that a diagnostic or a test can name the staff the
    // way the file does.
    int staffNumber = 1;

    // Staff spaces from the top of the system to this staff's *top line*.
    double topOffset = 0.0;

    std::vector<PlacedGlyph> glyphs;
    std::vector<PlacedLedgerLine> ledgerLines;
    std::vector<PlacedStem> stems;
};

// One part's staves within one system, and whether they are braced together.
struct PartLayout
{
    std::size_t partIndex = 0;

    // True when the part has more than one staff -- a piano, drawn as a grand
    // staff. Stated here so a painter never re-derives the rule, and so a test
    // can assert it without reading the score back.
    bool braced = false;

    // Where the brace goes, when there is one: a vertical span at a fixed x,
    // from the top line of the part's first staff to the bottom line of its
    // last. Zero-height when `braced` is false.
    double braceX = 0.0;
    double braceTop = 0.0;
    double braceBottom = 0.0;

    std::vector<StaffLayout> staves;
};

// One horizontal band of music: every part, for some run of measures.
struct SystemLayout
{
    std::vector<PartLayout> parts;

    // Staff spaces from the top of the laid-out score to the top of this
    // system.
    double topOffset = 0.0;

    // Total height of the system in staff spaces, including the gap between
    // its staves but not the gap to the next system.
    double height = 0.0;

    // Where barlines fall, in staff spaces from the left edge. Drawn through
    // every staff of a part, and through the gap between the staves of a
    // braced part.
    std::vector<double> barlineXs;

    // The measures this system covers, as indices into every part's measure
    // list. Invariant 1 makes one range valid for every part at once.
    std::size_t firstMeasureIndex = 0;
    std::size_t measureCount = 0;
};

// A whole score, laid out for one width.
struct ScoreLayout
{
    std::vector<SystemLayout> systems;

    // Overall extent in staff spaces. `height` is what a scrolling view needs
    // to know.
    double width = 0.0;
    double height = 0.0;

    // What the renderer could not draw, in the shape the importer already uses
    // so that there is one kind of thing to read and one way to show it.
    std::vector<Diagnostic> diagnostics;
};

// How many staves a system holds in total, across every part.
[[nodiscard]] inline std::size_t staffCount(const SystemLayout& system) noexcept
{
    std::size_t total = 0;

    for (const PartLayout& part : system.parts)
    {
        total += part.staves.size();
    }

    return total;
}
} // namespace score::engraving
