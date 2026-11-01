#pragma once

// The units the engraving layer measures in (no JUCE dependency).
//
// Everything the layout produces is in **staff spaces** -- the distance between
// two adjacent staff lines, which is the unit engravers measure in and the unit
// a music font is designed in. Nothing here is a pixel. The tool multiplies by
// one scale factor to draw, and that factor is the whole of zoom.
//
// Laying out in pixels at paint time would be the shorter way to write a
// renderer and would make the result untestable: the thing most worth pinning
// -- which line is this note on -- would only be checkable by looking at a
// picture. See design.md decision 1.
namespace engraving
{
// A staff has five lines and four spaces between them, so it is four staff
// spaces tall.
inline constexpr int staffLineCount = 5;
inline constexpr double staffHeightInSpaces = 4.0;

// A staff **position** is an integer count of *half* spaces measured downwards
// from the top line, because one diatonic step is exactly one half space.
//
//   0  top line          (F5 in treble)
//   1  the space below it
//   2  the fourth line
//   4  the middle line   (B4 in treble)
//   8  the bottom line   (E4 in treble)
//
// Negative is above the staff. Integers rather than doubles so that "on a line"
// is a parity test rather than a floating-point comparison, and so that a test
// assertion reads as a position a reader can count off a staff.
using StaffSteps = int;

inline constexpr StaffSteps topLineSteps = 0;
inline constexpr StaffSteps middleLineSteps = 4;
inline constexpr StaffSteps bottomLineSteps = 8;

// Whether a position sits on a line rather than in a space. Even positions are
// lines; the top line is 0.
[[nodiscard]] constexpr bool isOnLine(StaffSteps steps) noexcept
{
    return ((steps % 2) + 2) % 2 == 0;
}

// Vertical offset in staff spaces, for a renderer whose origin is the top line
// and whose y grows downwards -- which is both JUCE's convention and, usefully,
// the direction a lower pitch lies in.
[[nodiscard]] constexpr double spacesBelowTopLine(StaffSteps steps) noexcept
{
    return static_cast<double>(steps) * 0.5;
}

// Where the n-th staff line sits, counting from the bottom line as 1, which is
// how MusicXML numbers the line a clef centres on.
[[nodiscard]] constexpr StaffSteps stepsForLineFromBottom(int line) noexcept
{
    return (staffLineCount - line) * 2;
}
} // namespace engraving
