#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "application/tools/CompactPresentation.h"

// The rule three tools follow when their pane runs out of room.
//
// It is JUCE-free precisely so it can be checked here rather than by looking at
// a screenshot of each tool at each size — which is how the defect this belongs
// to was found, and is not a way to notice that two tools disagreed by twenty
// pixels about when to switch.

TEST_CASE("a pane with room draws its full display", "[tools][compact]")
{
    CHECK(compact::shapeFor(1280, 800) == compact::Shape::full);
    CHECK(compact::shapeFor(compact::fullWidth, compact::fullHeight) == compact::Shape::full);
    CHECK_FALSE(compact::isCompact(1280, 800));
}

TEST_CASE("one dimension short is enough to go compact", "[tools][compact]")
{
    // A pane wide enough but only 100px tall has no more room for a plot than a
    // narrow one does. Requiring both to be short would leave a squashed
    // full-size display in exactly the case the compact form exists for.
    CHECK(compact::isCompact(1280, 100));
    CHECK(compact::isCompact(180, 800));
}

TEST_CASE("the shape follows whichever axis has room", "[tools][compact]")
{
    // Tall and thin: draw down the pane. Short and wide: draw across it.
    CHECK(compact::shapeFor(180, 800) == compact::Shape::vertical);
    CHECK(compact::shapeFor(1280, 100) == compact::Shape::horizontal);
}

TEST_CASE("a small square pane draws horizontally", "[tools][compact]")
{
    // Deliberate: horizontal is the arrangement the full displays already have,
    // so it is the smaller change from what the tool was showing a moment ago.
    CHECK(compact::shapeFor(200, 200) == compact::Shape::horizontal);
}

TEST_CASE("detail falls away as the axis shortens", "[tools][compact]")
{
    // The point of this is that a fixed scale turns a graph into a smear: six
    // semitones over 90px is not six gridlines.
    CHECK(compact::detailFor(90) == Catch::Approx(0.0f));
    CHECK(compact::detailFor(420) == Catch::Approx(1.0f));
    CHECK(compact::detailFor(255) > 0.4f);
    CHECK(compact::detailFor(255) < 0.6f);
}

TEST_CASE("detail is bounded at both ends", "[tools][compact]")
{
    // A caller multiplying a count by this must never get a negative one, and
    // must never get more detail than the full display would draw.
    CHECK(compact::detailFor(0) == Catch::Approx(0.0f));
    CHECK(compact::detailFor(-50) == Catch::Approx(0.0f));
    CHECK(compact::detailFor(4000) == Catch::Approx(1.0f));
}

TEST_CASE("the thresholds leave room below the pane floor", "[tools][compact]")
{
    // A pane at the workspace's floor must already be showing a compact form.
    // If the floor were above these, a pane could be at its smallest and still
    // be trying to draw the full display.
    CHECK(compact::fullWidth > 180);
    CHECK(compact::fullHeight > 120);
}
