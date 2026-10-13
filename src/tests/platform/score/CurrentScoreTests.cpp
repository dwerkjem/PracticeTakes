#include <catch2/catch_test_macros.hpp>

#include "platform/score/CurrentScore.h"

#include <memory>

using namespace score;

namespace
{
[[nodiscard]] std::shared_ptr<const Score> makeScoreTitled(const std::string& title)
{
    auto built = std::make_shared<Score>();
    built->metadata.workTitle = title;

    return built;
}
} // namespace

TEST_CASE("the current score is absent before anything is published", "[score][current]")
{
    const CurrentScore holder;

    CHECK_FALSE(holder.hasScore());
    CHECK(holder.score() == nullptr);
}

TEST_CASE("publishing a score makes it the current one", "[score][current]")
{
    CurrentScore holder;
    const auto published = makeScoreTitled("Winterreise");

    holder.publish(published);

    REQUIRE(holder.hasScore());
    CHECK(holder.score() == published);
    CHECK(holder.score()->metadata.workTitle == "Winterreise");
}

TEST_CASE("publishing a second score replaces the first", "[score][current]")
{
    CurrentScore holder;
    holder.publish(makeScoreTitled("Winterreise"));

    const auto replacement = makeScoreTitled("Die schöne Müllerin");
    holder.publish(replacement);

    CHECK(holder.score() == replacement);
}

// The guarantee the whole ownership model rests on: a reader keeps reading the
// score it took, even after someone opens a different one. Without it, every
// consumer would have to be re-entrant against an import finishing mid-paint.
TEST_CASE("a score a reader holds survives being replaced", "[score][current]")
{
    CurrentScore holder;
    holder.publish(makeScoreTitled("Winterreise"));

    const std::shared_ptr<const Score> held = holder.score();
    holder.publish(makeScoreTitled("Die schöne Müllerin"));

    REQUIRE(held != nullptr);
    CHECK(held->metadata.workTitle == "Winterreise");
    CHECK(held != holder.score());
}

// What makes pointer identity a sound change check rather than a cheap one: two
// scores that are both live are never the same pointer, so a reader comparing
// what it holds against what the holder has now cannot be fooled by an address
// reused after a free.
TEST_CASE("two live scores are never the same pointer", "[score][current]")
{
    CurrentScore holder;
    holder.publish(makeScoreTitled("Winterreise"));
    const std::shared_ptr<const Score> first = holder.score();

    holder.publish(makeScoreTitled("Winterreise"));
    const std::shared_ptr<const Score> second = holder.score();

    CHECK(first != second);
}

TEST_CASE("publishing nothing clears the current score", "[score][current]")
{
    CurrentScore holder;
    holder.publish(makeScoreTitled("Winterreise"));

    holder.publish(nullptr);

    CHECK_FALSE(holder.hasScore());
    CHECK(holder.score() == nullptr);
}
