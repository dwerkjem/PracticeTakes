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
    double x = 0.0;

    // The notehead the stem attaches to: the lowest of a chord for an upward
    // stem, the highest for a downward one.
    StaffSteps attachPosition = 0;

    // The far end.
    StaffSteps tipPosition = 0;

    // None for a quarter note and longer. These are **flags**, not beams: this
    // pass does not beam, which the layout says in a notice.
    int flags = 0;

    bool up = true;

    bool isGrace = false;
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

    double noteheadWidth = 1.2;
    double accidentalWidth = 1.4;
    double graceNoteWidth = 1.7;
    double clefWidth = 3.6;
    double keyAccidentalWidth = 1.1;
    double timeSignatureWidth = 2.8;

    // Room after the last onset of a measure, before its barline.
    double measureTrailingWidth = 1.8;

    // How long a stem is, in half spaces, measured from the notehead it
    // attaches to. Seven half spaces is three and a half staff spaces, which is
    // the conventional length.
    StaffSteps stemLength = 7;

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
