#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "platform/score/engraving/Beams.h"

using namespace score;
using namespace score::engraving;

TEST_CASE("beam groups come from the time signature, by name", "[engraving][beam]")
{
    // The named cases the proposal's long-term section asks for. These are the
    // rule, not an illustration of it: a change to any line here changes what
    // every score in the application says about its own metre, and the point of
    // writing them out is that such a change shows up as a diff.
    const Tick quarter = ticksPerQuarterNote;
    const Tick eighth = quarter / 2;

    // Simple: one beat, whatever the beat is.
    CHECK(beamGroupTicks(TimeSignature{4, 4}) == quarter);
    CHECK(beamGroupTicks(TimeSignature{3, 4}) == quarter);
    CHECK(beamGroupTicks(TimeSignature{2, 4}) == quarter);

    // Cut time beats in halves, so its groups are twice as long.
    CHECK(beamGroupTicks(TimeSignature{2, 2}) == 2 * quarter);

    // Compound: three divisions, so 6/8 is two dotted quarters and not three
    // quarters. This is the case a rule that ignored the signature would get
    // wrong, and getting it wrong misrepresents the bar rather than looking
    // untidy.
    CHECK(beamGroupTicks(TimeSignature{6, 8}) == 3 * eighth);
    CHECK(beamGroupTicks(TimeSignature{9, 8}) == 3 * eighth);
    CHECK(beamGroupTicks(TimeSignature{12, 8}) == 3 * eighth);

    // 3/8 is one group of three, not three of one -- the case that makes the
    // rule test the beat type as well as the count.
    CHECK(beamGroupTicks(TimeSignature{3, 8}) == 3 * eighth);

    // And 3/4 stays simple, which is the same count with a different beat.
    CHECK_FALSE(isCompound(TimeSignature{3, 4}));
    CHECK(isCompound(TimeSignature{3, 8}));
    CHECK(isCompound(TimeSignature{6, 8}));
    CHECK_FALSE(isCompound(TimeSignature{4, 4}));
}

TEST_CASE("a beam line reads its own height at any x", "[engraving][beam]")
{
    const BeamLine flat = clampedBeamLine(0.0, 10.0, 4.0, 10.0);

    CHECK(flat.stepAt(0.0) == 10.0);
    CHECK(flat.stepAt(2.0) == 10.0);
    CHECK(flat.stepAt(4.0) == 10.0);

    const BeamLine sloped = clampedBeamLine(0.0, 8.0, 4.0, 12.0);

    CHECK(sloped.stepAt(0.0) == 8.0);
    CHECK(sloped.stepAt(2.0) == 10.0);
    CHECK(sloped.stepAt(4.0) == 12.0);

    // Outside the span, which is where a stub sits.
    CHECK(sloped.stepAt(5.0) == 13.0);
}

TEST_CASE("a wide leap does not produce a steep beam", "[engraving][beam]")
{
    // An octave is seven steps. Unclamped this is a beam at roughly
    // forty-five degrees, which no engraver would set.
    const BeamLine line = clampedBeamLine(0.0, 7.0, 6.0, 14.0);

    CHECK(line.endStep - line.startStep == maximumBeamRiseSteps);

    // Clamped about the midpoint, so the beam stays centred on the group
    // rather than pivoting about its first note.
    CHECK((line.startStep + line.endStep) / 2.0 == (7.0 + 14.0) / 2.0);

    // Downward the same way.
    const BeamLine falling = clampedBeamLine(0.0, 14.0, 6.0, 7.0);

    CHECK(falling.startStep - falling.endStep == maximumBeamRiseSteps);
}

TEST_CASE("a gentle slope is left alone", "[engraving][beam]")
{
    const BeamLine line = clampedBeamLine(0.0, 10.0, 4.0, 12.0);

    CHECK(line.startStep == 10.0);
    CHECK(line.endStep == 12.0);
}

TEST_CASE("a beam is moved so no stem is shortened", "[engraving][beam]")
{
    // The middle note of the group wants a higher stem end than the line
    // through the outer two gives it. A beam may lengthen a stem and must
    // never shorten one, so the whole line moves.
    const std::vector<std::pair<double, double>> required{{0.0, 10.0}, {2.0, 14.0}, {4.0, 10.0}};

    const BeamLine line = shiftedToClear(clampedBeamLine(0.0, 10.0, 4.0, 10.0), required, true);

    CHECK(line.stepAt(2.0) >= 14.0);

    // Bodily, not tilted: the slope the clamp decided is not revisited.
    CHECK(line.endStep - line.startStep == 0.0);

    // And every stem still reaches it.
    for (const std::pair<double, double>& stem : required)
    {
        INFO("stem at " << stem.first);
        CHECK(line.stepAt(stem.first) >= stem.second);
    }
}

TEST_CASE("a down-stemmed group moves the other way", "[engraving][beam]")
{
    const std::vector<std::pair<double, double>> required{{0.0, -2.0}, {2.0, -6.0}, {4.0, -2.0}};

    const BeamLine line = shiftedToClear(clampedBeamLine(0.0, -2.0, 4.0, -2.0), required, false);

    for (const std::pair<double, double>& stem : required)
    {
        INFO("stem at " << stem.first);
        CHECK(line.stepAt(stem.first) <= stem.second);
    }
}

TEST_CASE("a beam already clearing every stem is not moved", "[engraving][beam]")
{
    const std::vector<std::pair<double, double>> required{{0.0, 10.0}, {2.0, 9.0}, {4.0, 10.0}};

    const BeamLine original = clampedBeamLine(0.0, 10.0, 4.0, 10.0);
    const BeamLine line = shiftedToClear(original, required, true);

    CHECK(line.startStep == original.startStep);
    CHECK(line.endStep == original.endStep);
}
