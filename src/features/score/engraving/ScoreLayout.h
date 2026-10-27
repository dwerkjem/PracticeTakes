#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "Accidental.h"
#include "NoteValue.h"
#include "StaffGeometry.h"
#include "StaffPosition.h"
#include "platform/score/LyricSyllable.h"
#include "platform/score/ScoreAttributes.h"
#include "platform/score/ScoreEvent.h"

// The result of laying a score out: every glyph, with a position, and nothing
// that knows how to draw itself (no JUCE dependency).
//
// This is a value, which is the point. A renderer that computed positions while
// painting would need a `juce::Graphics` to be tested at all, and the thing most
// worth pinning -- which line is this note on, how wide is this bar -- would
// only ever be checked by looking at a picture.
//
// **Everything here is in staff spaces**, the distance between two adjacent
// staff lines, with x measured from the left edge of the system and y, where it
// appears, from the system's top staff line downwards. Staff *positions* are
// integer half spaces; see StaffGeometry.h. Nothing is a pixel: the tool
// multiplies by one scale factor, and that factor is zoom.
namespace engraving
{
enum class BarlineKind
{
    single,

    // The double bar that ends a score.
    final,

    // A repeat facing into the music that follows it, drawn at a measure's
    // start, and one facing back, drawn at its end.
    repeatStart,
    repeatEnd
};

// A notehead, with everything attached to it that is not the stem.
struct NoteheadGlyph
{
    // Which note of the model this is. Carried so that #33's target-note
    // overlay and #41's live display can find a note's glyph without laying the
    // score out again, and so a test can check a position against the score
    // rather than against the engine's own report.
    score::NoteRef source;

    double x = 0.0;

    StaffSteps position = 0;

    NoteheadKind head = NoteheadKind::filled;

    // Drawn to the left of the notehead. `none` for most notes.
    AccidentalKind accidental = AccidentalKind::none;

    // Augmentation dots, drawn to the right.
    int dots = 0;

    LedgerLines ledger;

    // Drawn smaller, and took no horizontal time of its own.
    bool isGrace = false;
};

// A stem, with its flags. One per event that has one -- a chord's notes share
// a stem, which is why this is not a field on the notehead.
struct StemGlyph
{
    // The stem's centre line. Set in from the notehead's edge by half the
    // stem's thickness, so that the stem's outer edge and the notehead's are
    // flush -- which is where an engraver puts it, and what makes a stem read
    // as attached rather than as a line drawn next to a note.
    double x = 0.0;

    double thickness = 0.12;

    // The notehead the stem attaches to: the lowest of a chord for an upward
    // stem, the highest for a downward one. A staff position, because this end
    // of a stem is always on a notehead.
    StaffSteps attachPosition = 0;

    // The far end, in **spaces below the top line** rather than in half-space
    // positions. A beam has a slope, so the stems under it end between
    // positions; this end of a stem is not on a notehead and has no reason to
    // land on one. `spacesBelowTopLine` converts a position into these units.
    double tipSpaces = 0.0;

    // None for a quarter note and longer, and none for a note a beam reached:
    // a beam replaces the flags it stands in for, which is what `beamLevels`
    // records.
    int flags = 0;

    // How many beams meet this stem, zero for an unbeamed one. The stem already
    // reaches the outermost of them -- this is here so that a caller can tell a
    // beamed stem from a flagged one without searching the beam list.
    int beamLevels = 0;

    bool up = true;

    bool isGrace = false;
};

// One beam, as a sloped bar joining the stems under or over it.
//
// Level 1 is the beam nearest the noteheads, which every beamed note carries;
// each further level is one more beam, stacked away from the noteheads. A
// sixteenth in a group of eighths therefore has a level-2 beam that the eighths
// do not.
struct BeamGlyph
{
    // Already reaching the outer edges of the stems at each end of a full
    // beam, so the bar covers those stems rather than stopping at their centre
    // lines. A partial beam reaches its own stem's outer edge on that side.
    double startX = 0.0;
    double endX = 0.0;

    // The y of the beam's near edge -- the side facing the noteheads -- at each
    // end, in spaces below the top line. The bar is `thickness` deep from
    // there, **away** from the noteheads: upwards for `up`, downwards for down.
    double startY = 0.0;
    double endY = 0.0;

    double thickness = 0.5;

    int level = 1;

    // A partial beam: the stub drawn on a stem whose beam level has no
    // neighbour to join -- the second beam of a lone sixteenth among eighths.
    // `startX`/`endX` already bound the stub, so a painter draws every beam the
    // same way.
    bool partial = false;

    bool up = true;
};

struct RestGlyph
{
    double x = 0.0;

    WrittenValue value = WrittenValue::quarter;

    int dots = 0;

    // The staff position the rest is drawn against: a whole rest hangs from the
    // fourth line, a half rest sits on the middle line, and everything shorter
    // centres on the middle line.
    StaffSteps position = middleLineSteps;
};

// One accidental of a key signature. A note's accidental is on its notehead.
struct KeyAccidentalGlyph
{
    double x = 0.0;
    StaffSteps position = 0;
    AccidentalKind kind = AccidentalKind::sharp;
};

struct ClefGlyph
{
    double x = 0.0;

    // Kept whole rather than reduced to a named clef, so the painter can draw
    // the sign on the line the file put it on and show an octave change.
    score::Clef clef;

    // The staff position the sign centres on, from `clef.line`.
    StaffSteps position = 0;
};

struct TimeSignatureGlyph
{
    double x = 0.0;
    int beats = 4;
    int beatType = 4;
};

struct BarlineGlyph
{
    double x = 0.0;
    BarlineKind kind = BarlineKind::single;
};

// A tie, as a curve between two noteheads in the same system.
struct TieGlyph
{
    double startX = 0.0;
    double endX = 0.0;

    // The staff position of the notes being tied -- both ends sound the same
    // pitch (invariant 4), so there is one position rather than two.
    StaffSteps position = 0;

    // Which side of the notehead the curve bows to.
    bool above = true;
};

struct LyricGlyph
{
    double x = 0.0;

    // MusicXML's verse number: the row this syllable belongs on.
    int verse = 1;

    std::string text;

    // Whether to draw a hyphen after the syllable, and whether this syllable is
    // held across the notes that follow. Both come from the model, which keeps
    // them precisely because neither is recoverable from the text.
    bool hyphenAfter = false;
    bool extend = false;
};

// Where a piece of text sits relative to the staff.
enum class TextRole
{
    // A tempo marking, above the staff.
    tempo,

    // A dynamic marking, below the staff and below any lyrics.
    dynamic,

    // A printed measure number, above the staff at a system start.
    measureNumber
};

struct TextGlyph
{
    double x = 0.0;
    std::string text;
    TextRole role = TextRole::dynamic;
};

// One staff's worth of music across the width of the tool.
struct System
{
    // Where this system's top staff line sits, in staff spaces from the top of
    // the whole layout.
    double topOfStaff = 0.0;

    // How far the staff lines are drawn. Not the available width: a last system
    // holding two bars is two bars wide, which is what a reader expects.
    double width = 0.0;

    std::vector<ClefGlyph> clefs;
    std::vector<KeyAccidentalGlyph> keyAccidentals;
    std::vector<TimeSignatureGlyph> timeSignatures;
    std::vector<NoteheadGlyph> noteheads;
    std::vector<StemGlyph> stems;
    std::vector<BeamGlyph> beams;
    std::vector<RestGlyph> rests;
    std::vector<BarlineGlyph> barlines;
    std::vector<TieGlyph> ties;
    std::vector<LyricGlyph> lyrics;
    std::vector<TextGlyph> texts;

    // The measures this system holds, as indices into the part. Inclusive.
    std::size_t firstMeasureIndex = 0;
    std::size_t lastMeasureIndex = 0;
};

enum class LayoutOutcome
{
    // Nothing is open.
    noScore,

    // A score is open and there is nothing in it this pass can draw: no parts,
    // no measures, or a staff whose clef names no pitch.
    nothingToDraw,

    laidOut
};

// The spacing decisions, in staff spaces, gathered so that each is one number a
// reader can find rather than a literal in the middle of the engine.
//
// Spacing is proportional to duration with a floor, which is deliberately
// simpler than real engraving -- see design.md decision 5. It is also why these
// are parameters: replacing the model later changes this struct and one
// function, and nothing else.
struct LayoutOptions
{
    // What the layout has to fit into. Systems break at this width.
    double availableWidth = 120.0;

    // Staff spaces of horizontal room per quarter note of music.
    double durationStretchPerQuarter = 7.0;

    // The least room between two successive onsets, so that a run of
    // sixteenths still has space for a notehead.
    double minimumSlotWidth = 2.6;

    // The music font's black notehead, which every stemmed notehead matches:
    // Bravura's is 1.18 spaces wide. MusicFontTests checks this number against
    // the font, because a stem placed by one width against a notehead drawn at
    // another stands off it or cuts into it.
    double noteheadWidth = 1.18;
    double accidentalWidth = 1.4;
    double graceNoteWidth = 1.7;
    double clefWidth = 3.6;
    double keyAccidentalWidth = 1.1;
    double timeSignatureWidth = 2.8;

    // Room after the last onset of a measure, before its barline.
    double measureTrailingWidth = 1.8;

    // Room a flag needs to the right of an up stem. The floor between two
    // onsets has to clear a notehead and a flag together, or a flagged eighth
    // touches whatever follows it; `minimumSlotWidth` is raised to this sum
    // when it falls short, rather than the two numbers being kept in step by
    // hand.
    double flagWidth = 1.0;

    // A stem's thickness, from the music font's engraving defaults. A layout
    // number rather than a painter one, because it decides where the stem's
    // centre line goes and how far a beam reaches past it.
    double stemThickness = 0.12;

    // How long a stem is, in half spaces, measured from the notehead it
    // attaches to. Seven half spaces is three and a half staff spaces, which is
    // the conventional length.
    StaffSteps stemLength = 7;

    // The least a stem may be shortened to when a beam's slope pulls it in, in
    // staff spaces. Shorter than this and the notehead and the beam read as one
    // blot.
    double minimumStemLength = 2.5;

    // How far a beamed group's far end may rise or fall against its near end,
    // in staff spaces. A beam follows the shape of its notes, but only so far:
    // an unclamped slope through a wide leap is a diagonal, not a beam.
    double maximumBeamRise = 1.5;

    // One beam's depth, and the gap between two stacked beams.
    double beamThickness = 0.5;
    double beamSeparation = 0.25;

    // How far a partial beam reaches from its stem.
    double partialBeamLength = 1.0;

    // Vertical room above a staff for tempo marks and measure numbers, and
    // below it for dynamics.
    double spaceAboveStaff = 4.0;
    double spaceBelowStaff = 3.0;

    // Room per lyric verse, below the staff and above the dynamics.
    double lyricRowHeight = 1.8;
};

// Everything the engine produced, and everything it did not.
struct ScoreLayout
{
    LayoutOutcome outcome = LayoutOutcome::noScore;

    // How to name what is being drawn. Empty when nothing is.
    std::string scoreTitle;
    std::string partName;

    std::vector<System> systems;

    // Total height in staff spaces, for a scrollable canvas.
    double totalHeight = 0.0;

    // How many lyric verses the drawn part uses, so a caller sizing a system
    // does not have to work it out from the glyphs.
    int lyricVerseCount = 0;

    // Everything in the open score this pass did not draw, as sentences meant
    // to be shown to a user.
    //
    // This list is the reason a first pass of a renderer is an acceptable thing
    // to ship. #200's acceptance criteria already state the rule in the general
    // case: content must be "either rendered correctly or reported, never
    // silently misplaced".
    std::vector<std::string> notices;
};
} // namespace engraving
