#include <catch2/catch_test_macros.hpp>

#include "features/score/engraving/BeamGrouping.h"

#include <cstddef>
#include <vector>

// Which notes beam together.
//
// The grouping rules are the part of beaming a reader judges the metre by, so
// each metre this names is a case of its own rather than a row in one loop: a
// future change to any of them should be a visible diff against the metre it
// changed.
using engraving::BeamCandidate;
using engraving::beamGroupsFor;
using engraving::beamGroupTicks;
using engraving::isCompoundMetre;
using score::Tick;
using score::ticksPerQuarterNote;

namespace
{
constexpr Tick eighth = ticksPerQuarterNote / 2;
constexpr Tick sixteenth = ticksPerQuarterNote / 4;

[[nodiscard]] score::TimeSignature timeOf(int beats, int beatType)
{
    return score::TimeSignature{beats, beatType};
}

// A run of equal notes from tick zero, each carrying `levels` beams.
[[nodiscard]] std::vector<BeamCandidate> runOf(std::size_t count, Tick duration, int levels)
{
    std::vector<BeamCandidate> candidates;
    candidates.reserve(count);

    for (std::size_t index = 0; index < count; ++index)
    {
        candidates.push_back(BeamCandidate{static_cast<Tick>(index) * duration, duration, levels});
    }

    return candidates;
}
} // namespace

TEST_CASE("a beam group is one beat of a simple metre", "[score][engraving][beams]")
{
    SECTION("4/4 groups by the quarter")
    {
        CHECK_FALSE(isCompoundMetre(timeOf(4, 4)));
        CHECK(beamGroupTicks(timeOf(4, 4)) == ticksPerQuarterNote);
    }

    SECTION("3/4 groups by the quarter, not by the bar")
    {
        CHECK(beamGroupTicks(timeOf(3, 4)) == ticksPerQuarterNote);
    }

    SECTION("cut time groups by the half")
    {
        CHECK_FALSE(isCompoundMetre(timeOf(2, 2)));
        CHECK(beamGroupTicks(timeOf(2, 2)) == ticksPerQuarterNote * 2);
    }

    SECTION("6/4 is three halves of music but still groups by the quarter")
    {
        // Six beats of a quarter each. Compound means the *beat* divides in
        // three, which a quarter beat does not.
        CHECK_FALSE(isCompoundMetre(timeOf(6, 4)));
        CHECK(beamGroupTicks(timeOf(6, 4)) == ticksPerQuarterNote);
    }
}

TEST_CASE("a beam group is one dotted beat of a compound metre", "[score][engraving][beams]")
{
    SECTION("6/8")
    {
        CHECK(isCompoundMetre(timeOf(6, 8)));
        CHECK(beamGroupTicks(timeOf(6, 8)) == eighth * 3);
    }

    SECTION("9/8 and 12/8 use the same group, and differ only in how many")
    {
        CHECK(beamGroupTicks(timeOf(9, 8)) == eighth * 3);
        CHECK(beamGroupTicks(timeOf(12, 8)) == eighth * 3);
    }

    SECTION("3/8 is one group of three, which is what an engraver writes")
    {
        CHECK(isCompoundMetre(timeOf(3, 8)));
        CHECK(beamGroupTicks(timeOf(3, 8)) == eighth * 3);
    }

    SECTION("6/16")
    {
        CHECK(isCompoundMetre(timeOf(6, 16)));
        CHECK(beamGroupTicks(timeOf(6, 16)) == sixteenth * 3);
    }
}

TEST_CASE("a metre that cannot be read groups nothing", "[score][engraving][beams]")
{
    // Beaming everything together would be the worse failure: it would assert a
    // metre, and the point of the rule is that the metre is what is in doubt.
    CHECK(beamGroupTicks(timeOf(0, 4)) == 0);
    CHECK(beamGroupTicks(timeOf(4, 0)) == 0);
    CHECK(beamGroupTicks(timeOf(-3, -8)) == 0);

    CHECK(beamGroupsFor(runOf(4, eighth, 1), timeOf(0, 0)).empty());
}

TEST_CASE("six eighths beam by the metre they are written in", "[score][engraving][beams]")
{
    const std::vector<BeamCandidate> six = runOf(6, eighth, 1);

    SECTION("6/8 is two groups of three")
    {
        const std::vector<std::vector<std::size_t>> groups = beamGroupsFor(six, timeOf(6, 8));

        REQUIRE(groups.size() == 2);
        CHECK(groups.at(0) == std::vector<std::size_t>{0, 1, 2});
        CHECK(groups.at(1) == std::vector<std::size_t>{3, 4, 5});
    }

    SECTION("3/4 is three groups of two")
    {
        // The same six notes. Beamed as 3+3 they read as 6/8, which is the
        // whole reason this is a metrical decision and not a visual one.
        const std::vector<std::vector<std::size_t>> groups = beamGroupsFor(six, timeOf(3, 4));

        REQUIRE(groups.size() == 3);
        CHECK(groups.at(0) == std::vector<std::size_t>{0, 1});
        CHECK(groups.at(1) == std::vector<std::size_t>{2, 3});
        CHECK(groups.at(2) == std::vector<std::size_t>{4, 5});
    }
}

TEST_CASE("cut time beams eight eighths in two fours", "[score][engraving][beams]")
{
    const std::vector<std::vector<std::size_t>> groups =
        beamGroupsFor(runOf(8, eighth, 1), timeOf(2, 2));

    REQUIRE(groups.size() == 2);
    CHECK(groups.at(0) == std::vector<std::size_t>{0, 1, 2, 3});
    CHECK(groups.at(1) == std::vector<std::size_t>{4, 5, 6, 7});

    SECTION("where 4/4 beams the same notes in four twos")
    {
        const std::vector<std::vector<std::size_t>> inFourFour =
            beamGroupsFor(runOf(8, eighth, 1), timeOf(4, 4));

        REQUIRE(inFourFour.size() == 4);
        CHECK(inFourFour.at(0) == std::vector<std::size_t>{0, 1});
        CHECK(inFourFour.at(3) == std::vector<std::size_t>{6, 7});
    }
}

TEST_CASE("four sixteenths in one beat are one group", "[score][engraving][beams]")
{
    const std::vector<std::vector<std::size_t>> groups =
        beamGroupsFor(runOf(4, sixteenth, 2), timeOf(4, 4));

    REQUIRE(groups.size() == 1);
    CHECK(groups.front() == std::vector<std::size_t>{0, 1, 2, 3});
}

TEST_CASE("triplet eighths beam together", "[score][engraving][beams]")
{
    // Three in the time of a quarter: 1280 ticks each, written as eighths, so
    // they fill one beat exactly and group as one. Nothing about tuplets is
    // special-cased -- the arithmetic simply works out.
    const std::vector<BeamCandidate> triplet = runOf(3, ticksPerQuarterNote / 3, 1);

    const std::vector<std::vector<std::size_t>> groups = beamGroupsFor(triplet, timeOf(4, 4));

    REQUIRE(groups.size() == 1);
    CHECK(groups.front() == std::vector<std::size_t>{0, 1, 2});
}

TEST_CASE("a group breaks on what cannot be beamed", "[score][engraving][beams]")
{
    SECTION("a quarter note in the middle")
    {
        const std::vector<BeamCandidate> candidates{
            BeamCandidate{0, eighth, 1},
            BeamCandidate{eighth, ticksPerQuarterNote, 0},
            BeamCandidate{eighth * 3, eighth, 1},
        };

        CHECK(beamGroupsFor(candidates, timeOf(2, 2)).empty());
    }

    SECTION("a rest, even one inside the beat")
    {
        const std::vector<BeamCandidate> candidates{
            BeamCandidate{0, eighth, 1},
            BeamCandidate{eighth, eighth, 0},
            BeamCandidate{eighth * 2, eighth, 1},
            BeamCandidate{eighth * 3, eighth, 1},
        };

        const std::vector<std::vector<std::size_t>> groups =
            beamGroupsFor(candidates, timeOf(2, 2));

        REQUIRE(groups.size() == 1);
        CHECK(groups.front() == std::vector<std::size_t>{2, 3});
    }

    SECTION("a gap in time, with nothing written in it at all")
    {
        // Two eighths in one half-note beat with a quarter of silence between
        // them. Adjacent indices, not adjacent notes.
        const std::vector<BeamCandidate> candidates{
            BeamCandidate{0, eighth, 1},
            BeamCandidate{ticksPerQuarterNote * 3 / 2, eighth, 1},
        };

        CHECK(beamGroupsFor(candidates, timeOf(2, 2)).empty());
    }

    SECTION("an onset that goes backwards, which is a broken file and not a group")
    {
        const std::vector<BeamCandidate> candidates{
            BeamCandidate{eighth, eighth, 1},
            BeamCandidate{0, eighth, 1},
        };

        CHECK(beamGroupsFor(candidates, timeOf(2, 2)).empty());
    }
}

TEST_CASE("one note is a flag, not a beam", "[score][engraving][beams]")
{
    SECTION("a lone eighth on the beat")
    {
        const std::vector<BeamCandidate> candidates{BeamCandidate{0, eighth, 1}};

        CHECK(beamGroupsFor(candidates, timeOf(4, 4)).empty());
    }

    SECTION("one eighth per beat, four times over")
    {
        const std::vector<BeamCandidate> candidates{
            BeamCandidate{0, eighth, 1},
            BeamCandidate{ticksPerQuarterNote, eighth, 1},
            BeamCandidate{ticksPerQuarterNote * 2, eighth, 1},
            BeamCandidate{ticksPerQuarterNote * 3, eighth, 1},
        };

        CHECK(beamGroupsFor(candidates, timeOf(4, 4)).empty());
    }

    SECTION("nothing beamable at all")
    {
        CHECK(beamGroupsFor(runOf(4, ticksPerQuarterNote, 0), timeOf(4, 4)).empty());
        CHECK(beamGroupsFor({}, timeOf(4, 4)).empty());
    }
}

TEST_CASE("mixed values inside one beat stay one group", "[score][engraving][beams]")
{
    // An eighth and two sixteenths. One beam joins all three; the second beam
    // is the geometry's problem, not the grouping's.
    const std::vector<BeamCandidate> candidates{
        BeamCandidate{0, eighth, 1},
        BeamCandidate{eighth, sixteenth, 2},
        BeamCandidate{eighth + sixteenth, sixteenth, 2},
    };

    const std::vector<std::vector<std::size_t>> groups = beamGroupsFor(candidates, timeOf(4, 4));

    REQUIRE(groups.size() == 1);
    CHECK(groups.front() == std::vector<std::size_t>{0, 1, 2});
}
