#include <catch2/catch_test_macros.hpp>

#include "features/score/engraving/Pagination.h"

#include <vector>

using namespace engraving;

namespace
{
// Systems of the given heights, stacked from the top of the layout with their
// bands touching, as the layout produces them.
[[nodiscard]] std::vector<System> stackOf(const std::vector<double>& heights)
{
    std::vector<System> systems;
    double cursor = 0.0;

    for (const double height : heights)
    {
        System system;
        system.bandTop = cursor;
        system.bandBottom = cursor + height;
        cursor = system.bandBottom;

        systems.push_back(system);
    }

    return systems;
}
} // namespace

TEST_CASE("no systems, no pages", "[score][engraving][pagination]")
{
    CHECK(paginate({}, 100.0).empty());
}

TEST_CASE("systems that fit share one page", "[score][engraving][pagination]")
{
    const std::vector<Page> pages = paginate(stackOf({20.0, 20.0, 20.0}), 100.0);

    REQUIRE(pages.size() == 1);
    CHECK(pages.front().firstSystem == 0);
    CHECK(pages.front().lastSystem == 2);
    CHECK(pages.front().top == 0.0);
    CHECK_FALSE(pages.front().oversize);
}

TEST_CASE("a page that fits exactly is not broken", "[score][engraving][pagination]")
{
    const std::vector<Page> pages = paginate(stackOf({25.0, 25.0, 25.0, 25.0}), 100.0);

    REQUIRE(pages.size() == 1);
    CHECK(pages.front().lastSystem == 3);
}

TEST_CASE("a system that would not fit starts the next page", "[score][engraving][pagination]")
{
    const std::vector<Page> pages = paginate(stackOf({40.0, 40.0, 40.0, 10.0}), 100.0);

    REQUIRE(pages.size() == 2);
    CHECK(pages.at(0).firstSystem == 0);
    CHECK(pages.at(0).lastSystem == 1);
    CHECK(pages.at(1).firstSystem == 2);
    CHECK(pages.at(1).lastSystem == 3);

    // The second page's content starts where its first system's band does.
    CHECK(pages.at(1).top == 80.0);
}

TEST_CASE("every system is on exactly one page, in order", "[score][engraving][pagination]")
{
    const std::vector<System> systems =
        stackOf({12.0, 30.0, 18.0, 44.0, 7.0, 25.0, 25.0, 60.0, 9.0, 33.0});
    const std::vector<Page> pages = paginate(systems, 70.0);

    std::size_t expected = 0;

    for (const Page& page : pages)
    {
        CHECK(page.firstSystem == expected);
        CHECK(page.lastSystem >= page.firstSystem);

        // Nothing on a page reaches past its foot unless the page says so.
        if (!page.oversize)
        {
            CHECK(systems.at(page.lastSystem).bandBottom - page.top <= 70.0);
        }

        expected = page.lastSystem + 1;
    }

    CHECK(expected == systems.size());
}

TEST_CASE("a system taller than a page gets a page of its own", "[score][engraving][pagination]")
{
    const std::vector<Page> pages = paginate(stackOf({20.0, 150.0, 20.0}), 100.0);

    REQUIRE(pages.size() == 3);
    CHECK_FALSE(pages.at(0).oversize);

    CHECK(pages.at(1).firstSystem == 1);
    CHECK(pages.at(1).lastSystem == 1);
    CHECK(pages.at(1).oversize);

    // Nothing is added to an oversize page.
    CHECK(pages.at(2).firstSystem == 2);
    CHECK_FALSE(pages.at(2).oversize);
}

TEST_CASE(
    "a system wider than a page goes on the next page, alone",
    "[score][engraving][pagination]")
{
    // One bar too wide for the page's width: it cannot share a page, because
    // the page has to be drawn smaller to show it whole.
    std::vector<System> systems = stackOf({20.0, 20.0, 20.0});
    systems.at(0).width = 60.0;
    systems.at(1).width = 95.0;
    systems.at(2).width = 60.0;

    const std::vector<Page> pages = paginate(systems, 100.0, 80.0);

    REQUIRE(pages.size() == 3);
    CHECK_FALSE(pages.at(0).oversize);
    CHECK(pages.at(1).firstSystem == 1);
    CHECK(pages.at(1).lastSystem == 1);
    CHECK(pages.at(1).oversize);
    CHECK(pages.at(2).firstSystem == 2);
    CHECK_FALSE(pages.at(2).oversize);
}

TEST_CASE("a page that fits is drawn at full size", "[score][engraving][pagination]")
{
    std::vector<System> systems = stackOf({30.0, 30.0});
    systems.at(0).width = 70.0;
    systems.at(1).width = 75.0;

    const std::vector<Page> pages = paginate(systems, 100.0, 80.0);

    REQUIRE(pages.size() == 1);
    CHECK(fitScale(pages.front(), systems, 100.0, 80.0) == 1.0);
}

TEST_CASE(
    "an oversize page is drawn just small enough to show whole",
    "[score][engraving][pagination]")
{
    SECTION("too tall")
    {
        const std::vector<System> systems = stackOf({150.0});
        const std::vector<Page> pages = paginate(systems, 100.0, 80.0);

        REQUIRE(pages.size() == 1);
        const double scale = fitScale(pages.front(), systems, 100.0, 80.0);

        CHECK(scale < 1.0);
        CHECK(150.0 * scale <= 100.0 + 1.0e-9);
    }

    SECTION("too wide")
    {
        std::vector<System> systems = stackOf({30.0});
        systems.front().width = 120.0;

        const std::vector<Page> pages = paginate(systems, 100.0, 80.0);

        REQUIRE(pages.size() == 1);
        const double scale = fitScale(pages.front(), systems, 100.0, 80.0);

        CHECK(scale < 1.0);
        CHECK(120.0 * scale <= 80.0 + 1.0e-9);
    }

    SECTION("too tall and too wide: the smaller scale wins")
    {
        std::vector<System> systems = stackOf({200.0});
        systems.front().width = 120.0;

        const std::vector<Page> pages = paginate(systems, 100.0, 80.0);
        const double scale = fitScale(pages.front(), systems, 100.0, 80.0);

        CHECK(200.0 * scale <= 100.0 + 1.0e-9);
        CHECK(120.0 * scale <= 80.0 + 1.0e-9);
    }
}

TEST_CASE("pagination is the same every time", "[score][engraving][pagination]")
{
    const std::vector<System> systems = stackOf({31.0, 17.5, 42.0, 8.25, 60.0});
    const std::vector<Page> first = paginate(systems, 75.0);
    const std::vector<Page> second = paginate(systems, 75.0);

    REQUIRE(first.size() == second.size());

    for (std::size_t index = 0; index < first.size(); ++index)
    {
        CHECK(first.at(index).firstSystem == second.at(index).firstSystem);
        CHECK(first.at(index).lastSystem == second.at(index).lastSystem);
        CHECK(first.at(index).top == second.at(index).top);
    }
}
