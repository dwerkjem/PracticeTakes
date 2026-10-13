#pragma once

#include "GlyphMetrics.h"

#include <utility>
#include <vector>

#include "platform/score/ScoreAttributes.h"

// Beams: how notes group into them, and where the beam itself lies.
//
// The grouping rule is the part that matters most and is the least like
// arithmetic. A beaming rule that ignores the time signature does not merely
// look wrong -- it *misrepresents the bar*, telling a reader that 6/8 is three
// groups of two when it is two groups of three. So the rule lives in one
// function with named cases in its tests, and the proposal's long-term section
// is explicit that changing it later must be a visible diff rather than a quiet
// re-reading of everyone's scores.
namespace score::engraving
{
// A staff space is two staff steps, and the font states beam thickness and
// separation in spaces.
inline constexpr double beamThicknessSteps = engravingDefaults.beamThickness * 2.0;
inline constexpr double beamSpacingSteps = engravingDefaults.beamSpacing * 2.0;

// Distance between one beam and the next in a stack, centre to centre.
inline constexpr double beamPitchSteps = beamThicknessSteps + beamSpacingSteps;

// The most a beam may rise or fall over its whole length, in staff steps.
//
// Unclamped, a group whose first and last notes are an octave apart produces a
// beam at roughly forty-five degrees, which no engraver would set. Two staff
// spaces is the usual limit.
inline constexpr double maximumBeamRiseSteps = 4.0;

// How far a lone secondary beam sticks out from its stem, in staff spaces.
//
// A sixteenth alone inside a beamed group -- the second half of a dotted
// rhythm -- has no neighbour to beam to. Without this it would be drawn with
// one beam and read as an eighth, which is the wrong note length rather than a
// missing flourish.
inline constexpr double beamStubWidth = 1.0;

// Whether a time signature is compound: its beat divides into three rather than
// two, so its beams group in threes.
//
// `beats > 3` is the ordinary test, and `beatType >= 8` is what makes 3/8 a
// single group of three rather than three unbeamed eighths, while leaving 3/4
// alone as three quarter-note groups.
[[nodiscard]] constexpr bool isCompound(const TimeSignature& time) noexcept
{
    return time.beats > 0 && (time.beats % 3) == 0 && (time.beats > 3 || time.beatType >= 8);
}

// Ticks spanned by one beam group under this time signature: three divisions of
// the beat type when compound, one when simple.
[[nodiscard]] Tick beamGroupTicks(const TimeSignature& time) noexcept;

// The line a beam's outer edge runs along, already clamped.
struct BeamLine
{
    double startX = 0.0;
    double startStep = 0.0;
    double endX = 0.0;
    double endStep = 0.0;

    // Where the line sits at some x. Constant outside the group's span, which
    // is what a stub needs.
    [[nodiscard]] double stepAt(double x) const noexcept;
};

// A beam line through two stem ends, with its rise clamped.
//
// Clamping is applied symmetrically about the midpoint, so a clamped beam stays
// centred on the group rather than pivoting about its first note.
[[nodiscard]] BeamLine
clampedBeamLine(double startX, double startStep, double endX, double endStep) noexcept;

// Move a beam line far enough that every stem in the group still reaches it.
//
// `requiredSteps` is the stem end each event would have had on its own. A beam
// may lengthen a stem and must never shorten one below that, so the line is
// shifted bodily -- never tilted, which would change the slope the clamp just
// decided.
[[nodiscard]] BeamLine shiftedToClear(
    BeamLine line,
    const std::vector<std::pair<double, double>>& required,
    bool up) noexcept;
} // namespace score::engraving
