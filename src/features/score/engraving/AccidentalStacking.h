#pragma once

#include <vector>

#include "Accidental.h"
#include "ScoreLayout.h"
#include "StaffGeometry.h"

// Where a chord's accidentals go, side by side to its left (no JUCE
// dependency).
//
// Its own unit because it is the one placement rule that depends on more than
// one notehead: whether a sign fits in a column depends on every sign already
// in it. The painter sees one glyph at a time, so this used to be impossible
// there -- each sign was right-aligned against its own notehead, and a chord
// with a sharp on every note drew them on top of each other.
//
// The rule is the conventional one (Gould, *Behind Bars*, "Accidentals on
// chords"; design.md decision 3):
//
// 1. Order the signs top to bottom.
// 2. Place them alternately from the outside in -- top, bottom, second from
//    top, second from bottom -- because the outer notes are the ones a reader
//    finds first, and they get the column nearest the chord.
// 3. Each goes in the rightmost column where it clears every sign already
//    there; a new column opens to the left when none will have it.
//
// Columns are measured from the left edge of the chord's *leftmost* notehead,
// so a note a second away that has been pushed left of a down stem pushes every
// sign out with it, and a sign on a note pushed right of an up stem is no
// longer drawn inside the note it was pushed away from.
namespace engraving
{
struct AccidentalToStack
{
    StaffSteps position = 0;
    AccidentalKind kind = AccidentalKind::none;
};

struct AccidentalStack
{
    // Parallel to the input. Each sign's left edge, which is its SMuFL origin,
    // in staff spaces from the left edge of the chord's leftmost notehead --
    // so always negative for a sign that is drawn. Zero for `none`.
    std::vector<double> x;

    // Parallel to the input: which column each sign went in, 0 nearest the
    // chord. -1 for `none`. Exposed for tests, which name cases by column.
    std::vector<int> column;

    int columnCount = 0;

    // How far the stack reaches left of the chord's leftmost notehead,
    // including the gap between them. Zero when nothing was stacked, so a
    // chord with no accidentals reserves no room for one.
    double width = 0.0;
};

// `scale` is 1 for a full-size chord and `graceNoteScale` for a grace note,
// which is drawn smaller and so takes less room.
[[nodiscard]] AccidentalStack stackAccidentals(
    const std::vector<AccidentalToStack>& signs,
    const LayoutOptions& options,
    double scale = 1.0);
} // namespace engraving
