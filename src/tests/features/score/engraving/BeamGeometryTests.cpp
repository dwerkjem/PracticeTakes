#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "features/score/engraving/BeamGeometry.h"

#include <algorithm>
#include <cmath>
#include <vector>

// Where a beam group's bars and stems end up.
//
// This is the arithmetic half of beaming: a slope, a clamp on it, and a shift
// that has to leave every stem in the group long enough. Each of those three is
// a case here, because each is a rule a reader would see broken.
using Catch::Approx;
using engraving::BeamGroupGeometry;
using engraving::BeamStem;
using engraving::layOutBeamGroup;
using engraving::LayoutOptions;
using engraving::spacesBelowTopLine;
using engraving::StaffSteps;

namespace
{
// Default spacing, named so a case can say which number it is about.
[[nodiscard]] LayoutOptions options()
{
    return LayoutOptions{};
}

// One note's stem, with no chord above or below it.
[[nodiscard]] BeamStem stemAt(double x, StaffSteps position, int levels = 1)
{
    return BeamStem{x, position, position, levels};
}

// How long a stem came out, in staff spaces, however it points.
[[nodiscard]] double lengthOf(const BeamStem& stem, double tip)
{
    return std::abs(tip - spacesBelowTopLine(stem.attachPosition));
}

[[nodiscard]] bool isFlat(const engraving::BeamGlyph& beam)
{
    return beam.startY == Approx(beam.endY);
}
} // namespace

TEST_CASE("fewer than two stems is not a group", "[score][engraving][beams]")
{
    // A lone eighth takes a flag. Returning an empty geometry rather than a
    // beam of zero width is what lets the caller treat "beamed" as a single
    // question.
    CHECK(layOutBeamGroup({}, true, options()).beams.empty());
    CHECK(layOutBeamGroup({stemAt(0.0, 4)}, true, options()).beams.empty());
    CHECK(layOutBeamGroup({stemAt(0.0, 4)}, true, options()).stemTips.empty());
}

TEST_CASE("two notes at one pitch take a flat beam", "[score][engraving][beams]")
{
    const std::vector<BeamStem> stems{stemAt(0.0, 4), stemAt(4.0, 4)};

    const BeamGroupGeometry geometry = layOutBeamGroup(stems, true, options());

    REQUIRE(geometry.beams.size() == 1);

    const engraving::BeamGlyph& beam = geometry.beams.front();

    CHECK(beam.level == 1);
    CHECK_FALSE(beam.partial);
    CHECK(beam.up);
    CHECK(isFlat(beam));

    // The middle line is two spaces down; a conventional stem is three and a
    // half long, so the beam's near edge sits one and a half spaces above the
    // top line.
    CHECK(beam.startY == Approx(-1.5));
    CHECK(beam.startX == Approx(0.0));
    CHECK(beam.endX == Approx(4.0));

    REQUIRE(geometry.stemTips.size() == 2);

    // The stem ends at the far edge of the beam, so it is the beam's thickness
    // longer than the beam's near edge.
    CHECK(geometry.stemTips.at(0) == Approx(-2.0));
    CHECK(geometry.stemTips.at(1) == Approx(-2.0));
}

TEST_CASE("a beam slopes with its notes", "[score][engraving][beams]")
{
    // Rising a third, from the bottom line to the space below the middle one:
    // gentle enough that the slope clamp does not come into it.
    const std::vector<BeamStem> stems{stemAt(0.0, 8), stemAt(4.0, 6)};

    const BeamGroupGeometry geometry = layOutBeamGroup(stems, true, options());

    REQUIRE(geometry.beams.size() == 1);

    const engraving::BeamGlyph& beam = geometry.beams.front();

    // A rising line in pitch is a falling one in y, and both ends keep the
    // conventional stem length because nothing had to be shifted.
    CHECK(beam.endY < beam.startY);
    CHECK(beam.startY == Approx(0.5));
    CHECK(beam.endY == Approx(-0.5));
    CHECK(std::abs(beam.endY - beam.startY) < options().maximumBeamRise);

    CHECK(lengthOf(stems.at(0), geometry.stemTips.at(0)) >= options().minimumStemLength);
    CHECK(lengthOf(stems.at(1), geometry.stemTips.at(1)) >= options().minimumStemLength);
}

TEST_CASE("a beam's slope is clamped, so a leap is not a diagonal", "[score][engraving][beams]")
{
    // Two octaves inside one beat. Followed literally the beam would be a
    // diagonal line, which is not a beam.
    const std::vector<BeamStem> stems{stemAt(0.0, 12), stemAt(4.0, -4)};

    const BeamGroupGeometry geometry = layOutBeamGroup(stems, true, options());

    REQUIRE(geometry.beams.size() == 1);

    const engraving::BeamGlyph& beam = geometry.beams.front();

    CHECK(std::abs(beam.endY - beam.startY) == Approx(options().maximumBeamRise));

    // And the clamp does not get to shorten a stem below the minimum: the whole
    // line moves instead.
    CHECK(lengthOf(stems.at(0), geometry.stemTips.at(0)) >= options().minimumStemLength);
    CHECK(lengthOf(stems.at(1), geometry.stemTips.at(1)) >= options().minimumStemLength);
}

TEST_CASE("a note poking through a beam moves the whole beam", "[score][engraving][beams]")
{
    // Low, high, low. The outer notes decide the line and the middle one does
    // not fit under it; lengthening that one stem alone would bend the beam.
    const std::vector<BeamStem> stems{stemAt(0.0, 8), stemAt(3.0, 0), stemAt(6.0, 8)};

    const BeamGroupGeometry geometry = layOutBeamGroup(stems, true, options());

    REQUIRE(geometry.beams.size() == 1);

    const engraving::BeamGlyph& beam = geometry.beams.front();

    CHECK(isFlat(beam));

    // The middle note is on the top line, so a two-and-a-half space stem puts
    // the beam two and a half spaces above it.
    CHECK(beam.startY == Approx(-2.5));

    REQUIRE(geometry.stemTips.size() == 3);

    for (std::size_t index = 0; index < stems.size(); ++index)
    {
        CHECK(
            lengthOf(stems.at(index), geometry.stemTips.at(index)) >= options().minimumStemLength);
    }

    // And the outer stems grew rather than the beam bending to meet them.
    CHECK(lengthOf(stems.at(0), geometry.stemTips.at(0)) > options().minimumStemLength);
}

TEST_CASE("a beam clears the far notehead of a chord", "[score][engraving][beams]")
{
    // An up-stemmed chord from the bottom line to the top line: the stem
    // attaches to the lowest note and has to clear the highest.
    const std::vector<BeamStem> stems{BeamStem{0.0, 8, 0, 1}, BeamStem{4.0, 8, 0, 1}};

    const BeamGroupGeometry geometry = layOutBeamGroup(stems, true, options());

    REQUIRE(geometry.beams.size() == 1);

    // One space above the top line, which is where the top notehead is.
    CHECK(geometry.beams.front().startY == Approx(-1.0));
}

TEST_CASE("a down-stemmed group mirrors an up-stemmed one", "[score][engraving][beams]")
{
    const std::vector<BeamStem> stems{stemAt(0.0, 0), stemAt(4.0, 0)};

    const BeamGroupGeometry geometry = layOutBeamGroup(stems, false, options());

    REQUIRE(geometry.beams.size() == 1);

    const engraving::BeamGlyph& beam = geometry.beams.front();

    CHECK_FALSE(beam.up);
    CHECK(isFlat(beam));

    // The top line is y zero, so a three-and-a-half space stem downwards puts
    // the beam's near edge there, and the stem ends past it.
    CHECK(beam.startY == Approx(3.5));
    CHECK(geometry.stemTips.at(0) == Approx(4.0));
}

TEST_CASE("a second beam stacks away from the noteheads", "[score][engraving][beams]")
{
    SECTION("upwards for an up-stemmed group")
    {
        const std::vector<BeamStem> stems{stemAt(0.0, 4, 2), stemAt(4.0, 4, 2)};

        const BeamGroupGeometry geometry = layOutBeamGroup(stems, true, options());

        REQUIRE(geometry.beams.size() == 2);

        const engraving::BeamGlyph& first = geometry.beams.at(0);
        const engraving::BeamGlyph& second = geometry.beams.at(1);

        CHECK(first.level == 1);
        CHECK(second.level == 2);
        CHECK_FALSE(second.partial);

        // One thickness plus one gap further from the staff.
        CHECK(
            second.startY ==
            Approx(first.startY - options().beamThickness - options().beamSeparation));

        // And the stem reaches the outermost of them.
        CHECK(geometry.stemTips.at(0) == Approx(second.startY - options().beamThickness));
    }

    SECTION("downwards for a down-stemmed group")
    {
        const std::vector<BeamStem> stems{stemAt(0.0, 0, 3), stemAt(4.0, 0, 3)};

        const BeamGroupGeometry geometry = layOutBeamGroup(stems, false, options());

        REQUIRE(geometry.beams.size() == 3);

        CHECK(geometry.beams.at(2).startY > geometry.beams.at(0).startY);
        CHECK(
            geometry.stemTips.at(0) ==
            Approx(geometry.beams.at(2).startY + options().beamThickness));
    }
}

TEST_CASE("a beam level with no neighbour is a stub", "[score][engraving][beams]")
{
    SECTION("pointing back towards the beat when there is a note behind it")
    {
        // An eighth then a sixteenth. The second beam belongs to the sixteenth
        // alone, and points back so a reader can see which note it is on.
        const std::vector<BeamStem> stems{stemAt(0.0, 4, 1), stemAt(4.0, 4, 2)};

        const BeamGroupGeometry geometry = layOutBeamGroup(stems, true, options());

        REQUIRE(geometry.beams.size() == 2);

        const engraving::BeamGlyph& full = geometry.beams.at(0);
        const engraving::BeamGlyph& stub = geometry.beams.at(1);

        CHECK(full.level == 1);
        CHECK_FALSE(full.partial);
        CHECK(full.startX == Approx(0.0));
        CHECK(full.endX == Approx(4.0));

        CHECK(stub.level == 2);
        CHECK(stub.partial);
        CHECK(stub.endX == Approx(4.0));
        CHECK(stub.startX == Approx(3.0));
    }

    SECTION("pointing forward when it is the first note of the group")
    {
        const std::vector<BeamStem> stems{stemAt(0.0, 4, 2), stemAt(4.0, 4, 1)};

        const BeamGroupGeometry geometry = layOutBeamGroup(stems, true, options());

        REQUIRE(geometry.beams.size() == 2);

        const engraving::BeamGlyph& stub = geometry.beams.at(1);

        CHECK(stub.partial);
        CHECK(stub.startX == Approx(0.0));
        CHECK(stub.endX == Approx(1.0));
    }

    SECTION("never more than halfway to its neighbour")
    {
        // Stems one space apart: a full-length stub would reach the next stem
        // and read as a bar joining them.
        const std::vector<BeamStem> stems{stemAt(0.0, 4, 1), stemAt(1.0, 4, 2)};

        const BeamGroupGeometry geometry = layOutBeamGroup(stems, true, options());

        REQUIRE(geometry.beams.size() == 2);

        const engraving::BeamGlyph& stub = geometry.beams.at(1);

        CHECK(stub.partial);
        CHECK(stub.startX == Approx(0.5));
        CHECK(stub.endX == Approx(1.0));
    }
}

TEST_CASE(
    "a run of sixteenths inside a group of eighths is a bar, not two stubs",
    "[score][engraving][beams]")
{
    // Eighth, sixteenth, sixteenth: the second beam joins the two sixteenths,
    // because two of them are a neighbouring pair.
    const std::vector<BeamStem> stems{stemAt(0.0, 4, 1), stemAt(3.0, 4, 2), stemAt(4.5, 4, 2)};

    const BeamGroupGeometry geometry = layOutBeamGroup(stems, true, options());

    REQUIRE(geometry.beams.size() == 2);

    const engraving::BeamGlyph& second = geometry.beams.at(1);

    CHECK(second.level == 2);
    CHECK_FALSE(second.partial);
    CHECK(second.startX == Approx(3.0));
    CHECK(second.endX == Approx(4.5));

    // The eighth's stem stops at the first beam; the sixteenths' reach the
    // second.
    CHECK(geometry.stemTips.at(0) > geometry.stemTips.at(1));
    CHECK(geometry.stemTips.at(1) == Approx(geometry.stemTips.at(2)));
}

TEST_CASE("every beamed stem is long enough to read as a stem", "[score][engraving][beams]")
{
    // A shape built to fight the clamp: a wide leap, a note far outside the
    // staff, and a chord in the middle of it.
    const std::vector<BeamStem> stems{
        BeamStem{0.0, 16, 16, 1}, BeamStem{2.0, 10, 2, 2}, BeamStem{4.0, -6, -6, 3},
        BeamStem{6.0, 4, 4, 1}};

    for (const bool up : {true, false})
    {
        const BeamGroupGeometry geometry = layOutBeamGroup(stems, up, options());

        REQUIRE(geometry.stemTips.size() == stems.size());

        for (std::size_t index = 0; index < stems.size(); ++index)
        {
            INFO("stem " << index << (up ? " up" : " down"));

            CHECK(
                lengthOf(stems.at(index), geometry.stemTips.at(index)) >=
                options().minimumStemLength);

            // And it points the way the group does.
            const double attach = spacesBelowTopLine(stems.at(index).attachPosition);

            CHECK((geometry.stemTips.at(index) < attach) == up);
        }
    }
}
