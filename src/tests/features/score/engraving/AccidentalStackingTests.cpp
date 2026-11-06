#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "features/score/engraving/AccidentalStacking.h"

#include <algorithm>
#include <vector>

// The column rule for a chord's accidentals, by named case (design.md decision
// 3). Positions are treble-staff half spaces from the top line: F5 is 0, the
// middle line B4 is 4, the bottom line E4 is 8, middle C is 10.
using namespace engraving;
using Catch::Approx;

namespace
{
constexpr StaffSteps c4 = 10;
constexpr StaffSteps d4 = 9;
constexpr StaffSteps e4 = 8;
constexpr StaffSteps f4 = 7;
constexpr StaffSteps g4 = 6;
constexpr StaffSteps a4 = 5;
constexpr StaffSteps b4 = 4;
constexpr StaffSteps c5 = 3;

[[nodiscard]] AccidentalToStack sharpOn(StaffSteps position)
{
    return {position, AccidentalKind::sharp};
}

[[nodiscard]] AccidentalToStack flatOn(StaffSteps position)
{
    return {position, AccidentalKind::flat};
}

[[nodiscard]] double rightEdgeOf(
    const AccidentalStack& stack,
    const std::vector<AccidentalToStack>& signs,
    std::size_t index)
{
    return stack.x.at(index) + accidentalMetricsFor(signs.at(index).kind).width;
}
} // namespace

TEST_CASE("a chord with no accidentals reserves no room", "[score][engraving][accidentals]")
{
    const std::vector<AccidentalToStack> signs{
        {c4, AccidentalKind::none}, {e4, AccidentalKind::none}};

    const AccidentalStack stack = stackAccidentals(signs, LayoutOptions{});

    CHECK(stack.columnCount == 0);
    CHECK(stack.width == 0.0);
    CHECK(stack.column == std::vector<int>{-1, -1});
}

TEST_CASE("one sharp sits a gap before its notehead", "[score][engraving][accidentals]")
{
    const LayoutOptions options;
    const std::vector<AccidentalToStack> signs{sharpOn(f4)};

    const AccidentalStack stack = stackAccidentals(signs, options);

    CHECK(stack.columnCount == 1);
    CHECK(rightEdgeOf(stack, signs, 0) == Approx(-options.accidentalGap));
    CHECK(
        stack.width ==
        Approx(options.accidentalGap + accidentalMetricsFor(AccidentalKind::sharp).width));
}

TEST_CASE(
    "a triad with a sharp on every note stacks in three columns",
    "[score][engraving][accidentals]")
{
    // C-sharp, E-sharp, G-sharp. No two are far enough apart to share: the
    // outer pair is a fifth, and two sharps a fifth apart in one column put
    // the upper one's right upright straight into the lower one's. So the top
    // sign goes nearest the chord, the bottom one next, the middle one
    // furthest out -- outside in, the order the reader looks.
    const std::vector<AccidentalToStack> signs{sharpOn(c4), sharpOn(e4), sharpOn(g4)};

    const AccidentalStack stack = stackAccidentals(signs, LayoutOptions{});

    CHECK(stack.columnCount == 3);
    CHECK(stack.column == std::vector<int>{1, 2, 0});

    // Each column wholly left of the one before it: nothing overlaps.
    CHECK(rightEdgeOf(stack, signs, 0) < stack.x.at(2));
    CHECK(rightEdgeOf(stack, signs, 1) < stack.x.at(0));
}

TEST_CASE(
    "a chord spanning a seventh puts its outer signs in one column",
    "[score][engraving][accidentals]")
{
    // C-sharp, F-sharp, B-sharp: the outer two are a seventh apart, far enough
    // for two sharps to stand one above the other, so they share the nearest
    // column and only the middle one goes out.
    const std::vector<AccidentalToStack> signs{sharpOn(c4), sharpOn(f4), sharpOn(b4)};

    const AccidentalStack stack = stackAccidentals(signs, LayoutOptions{});

    CHECK(stack.columnCount == 2);
    CHECK(stack.column == std::vector<int>{0, 1, 0});
    CHECK(stack.x.at(0) == stack.x.at(2));
    CHECK(rightEdgeOf(stack, signs, 1) < stack.x.at(0));
}

TEST_CASE("a cluster of seconds needs a column each", "[score][engraving][accidentals]")
{
    // C-sharp, D-sharp, E-sharp. Top first, then bottom, then the middle.
    const std::vector<AccidentalToStack> signs{sharpOn(c4), sharpOn(d4), sharpOn(e4)};

    const AccidentalStack stack = stackAccidentals(signs, LayoutOptions{});

    CHECK(stack.columnCount == 3);
    CHECK(stack.column == std::vector<int>{1, 2, 0});
}

TEST_CASE("sharps share a column from a seventh, not a sixth", "[score][engraving][accidentals]")
{
    const AccidentalStack sixth = stackAccidentals({sharpOn(c4), sharpOn(a4)}, LayoutOptions{});
    const AccidentalStack seventh = stackAccidentals({sharpOn(c4), sharpOn(b4)}, LayoutOptions{});

    CHECK(sixth.columnCount == 2);
    CHECK(seventh.columnCount == 1);
}

TEST_CASE("two flats a sixth apart do not meet stem to stem", "[score][engraving][accidentals]")
{
    // Their boxes clear by less than a twentieth of a space, which on the page
    // is one long stem with two bowls on it. The clearance sends the lower one
    // out; a seventh apart there is room.
    const AccidentalStack sixth = stackAccidentals({flatOn(c4), flatOn(a4)}, LayoutOptions{});
    const AccidentalStack seventh = stackAccidentals({flatOn(c4), flatOn(b4)}, LayoutOptions{});

    CHECK(sixth.columnCount == 2);
    CHECK(seventh.columnCount == 1);
}

TEST_CASE("which sign is on top matters", "[score][engraving][accidentals]")
{
    // A flat barely reaches below its note and a sharp reaches well above, so
    // a flat over a sharp a sixth apart clears and a sharp over a flat does
    // not: the flat's tall stem rises into the sharp.
    const AccidentalStack flatOverSharp =
        stackAccidentals({flatOn(a4), sharpOn(c4)}, LayoutOptions{});
    const AccidentalStack sharpOverFlat =
        stackAccidentals({sharpOn(a4), flatOn(c4)}, LayoutOptions{});

    CHECK(flatOverSharp.columnCount == 1);
    CHECK(sharpOverFlat.columnCount == 2);
}

TEST_CASE("an octave apart, any two signs share a column", "[score][engraving][accidentals]")
{
    const std::vector<AccidentalKind> kinds{
        AccidentalKind::natural, AccidentalKind::sharp, AccidentalKind::flat,
        AccidentalKind::doubleSharp, AccidentalKind::doubleFlat};

    for (const AccidentalKind upper : kinds)
    {
        for (const AccidentalKind lower : kinds)
        {
            INFO(nameOf(upper) << " over " << nameOf(lower));

            const AccidentalStack stack =
                stackAccidentals({{c5, upper}, {c4, lower}}, LayoutOptions{});

            CHECK(stack.columnCount == 1);
        }
    }
}

TEST_CASE("signs in one column line up by their right edges", "[score][engraving][accidentals]")
{
    // A natural is narrower than a sharp; in one column it sits against the
    // chord, not against the sharp's left edge.
    const LayoutOptions options;
    const std::vector<AccidentalToStack> signs{{c5, AccidentalKind::natural}, sharpOn(c4)};

    const AccidentalStack stack = stackAccidentals(signs, options);

    REQUIRE(stack.columnCount == 1);
    CHECK(rightEdgeOf(stack, signs, 0) == Approx(rightEdgeOf(stack, signs, 1)));
    CHECK(
        stack.width ==
        Approx(options.accidentalGap + accidentalMetricsFor(AccidentalKind::sharp).width));
}

TEST_CASE("columns do not overlap one another", "[score][engraving][accidentals]")
{
    const LayoutOptions options;
    const std::vector<AccidentalToStack> signs{sharpOn(c4), sharpOn(d4), sharpOn(e4)};

    const AccidentalStack stack = stackAccidentals(signs, options);

    REQUIRE(stack.columnCount == 3);

    // Column by column, nearest first: each one's right edge is the previous
    // one's left edge less the column gap.
    std::vector<std::size_t> byColumn{0, 1, 2};
    std::sort(
        byColumn.begin(), byColumn.end(), [&stack](std::size_t lhs, std::size_t rhs)
        { return stack.column.at(lhs) < stack.column.at(rhs); });

    for (std::size_t index = 1; index < byColumn.size(); ++index)
    {
        CHECK(
            rightEdgeOf(stack, signs, byColumn.at(index)) ==
            Approx(stack.x.at(byColumn.at(index - 1)) - options.accidentalColumnGap));
    }

    CHECK(stack.width == Approx(-stack.x.at(byColumn.back())));
}

TEST_CASE("the input order does not change the result", "[score][engraving][accidentals]")
{
    // The model holds a chord's notes in file order, which an exporter chooses.
    // Where each sign goes is a property of the chord.
    const std::vector<AccidentalToStack> upward{sharpOn(c4), sharpOn(e4), sharpOn(g4)};
    const std::vector<AccidentalToStack> downward{sharpOn(g4), sharpOn(e4), sharpOn(c4)};

    const AccidentalStack first = stackAccidentals(upward, LayoutOptions{});
    const AccidentalStack second = stackAccidentals(downward, LayoutOptions{});

    CHECK(first.width == second.width);

    for (std::size_t index = 0; index < upward.size(); ++index)
    {
        const std::size_t mirrored = upward.size() - 1 - index;

        CHECK(first.column.at(index) == second.column.at(mirrored));
        CHECK(first.x.at(index) == second.x.at(mirrored));
    }
}

TEST_CASE("a grace note's accidentals take a grace note's room", "[score][engraving][accidentals]")
{
    // One sign: every distance scales, so the room is the grace fraction of a
    // full-size sign's.
    const AccidentalStack one = stackAccidentals({sharpOn(f4)}, LayoutOptions{});
    const AccidentalStack oneGrace =
        stackAccidentals({sharpOn(f4)}, LayoutOptions{}, graceNoteScale);

    CHECK(oneGrace.width == Approx(one.width * graceNoteScale));

    // A chord: the signs shrink but the staff does not, so small sharps a fifth
    // apart clear each other where full-size ones do not, and the grace chord
    // needs fewer columns -- never more.
    const std::vector<AccidentalToStack> triad{sharpOn(c4), sharpOn(e4), sharpOn(g4)};

    const AccidentalStack full = stackAccidentals(triad, LayoutOptions{});
    const AccidentalStack grace = stackAccidentals(triad, LayoutOptions{}, graceNoteScale);

    CHECK(full.columnCount == 3);
    CHECK(grace.columnCount == 2);
    CHECK(grace.column == std::vector<int>{0, 1, 0});
}
