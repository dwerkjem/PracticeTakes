#pragma once

#include <cstddef>
#include <vector>

#include "ScoreLayout.h"
#include "StaffGeometry.h"

// Where a beam group's beams and stems end up (no JUCE dependency).
//
// `BeamGrouping` decides *which* notes beam together; this decides where the
// bars go and how long each stem under them is. It is the part of beaming with
// real arithmetic in it -- a slope, a clamp, and a shift that has to keep every
// stem in the group long enough -- so it is a function over numbers returning
// numbers, and every rule in it is one assertion away.
namespace engraving
{
// One stem a beam has to reach.
struct BeamStem
{
    double x = 0.0;

    // The notehead the stem attaches to, and the far notehead of its chord.
    // Equal for a single note. Both are needed: the beam has to clear the far
    // one and the stem is measured from the near one.
    StaffSteps attachPosition = 0;
    StaffSteps farPosition = 0;

    // Beams meeting this stem: one for an eighth, two for a sixteenth.
    int beamLevels = 1;
};

// What a group's geometry comes out as.
struct BeamGroupGeometry
{
    // Where each input stem ends, in spaces below the top line, in the order
    // the stems came in.
    std::vector<double> stemTips;

    std::vector<BeamGlyph> beams;
};

// Lay out one beam group.
//
// `up` is the group's single stem direction -- a beam joins stems pointing the
// same way, so the direction is the group's and not each note's.
//
// The beam is a straight line through the two outer stems' natural tips, with
// its rise clamped to `maximumBeamRise` and the whole line then pushed away
// from the staff by however much it takes to leave every stem in the group at
// least `minimumStemLength` long and clear of its chord's far notehead. Pushing
// the line rather than lengthening one stem is what keeps it straight, and a
// beam that is not straight is not a beam.
//
// Returns an empty result for fewer than two stems: a lone note takes a flag.
[[nodiscard]] BeamGroupGeometry
layOutBeamGroup(const std::vector<BeamStem>& stems, bool up, const LayoutOptions& options);
} // namespace engraving
