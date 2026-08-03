#include <catch2/catch_test_macros.hpp>

#include "application/tools/ToolInstanceId.h"

TEST_CASE("the first instance of a tool is spelled as the bare tool id", "[tools][instance]")
{
    // This is the whole reason multi-instance costs no format migration: what
    // this change writes is byte-identical to what the current code writes.
    const auto first = ToolInstanceId::forOrdinal("tuner", 1);

    CHECK(first.value() == "tuner");
    CHECK(first.toolId() == "tuner");
    CHECK(first.ordinal() == 1);
    CHECK(first.isWellFormed());
}

TEST_CASE("later instances carry an ordinal suffix", "[tools][instance]")
{
    const auto second = ToolInstanceId::forOrdinal("tuner", 2);
    const auto tenth = ToolInstanceId::forOrdinal("tuner", 10);

    CHECK(second.value() == "tuner#2");
    CHECK(second.toolId() == "tuner");
    CHECK(second.ordinal() == 2);

    CHECK(tenth.value() == "tuner#10");
    CHECK(tenth.toolId() == "tuner");
    CHECK(tenth.ordinal() == 10);
}

TEST_CASE("instances of the same tool are distinct ids", "[tools][instance]")
{
    const auto first = ToolInstanceId::forOrdinal("tuner", 1);
    const auto second = ToolInstanceId::forOrdinal("tuner", 2);

    CHECK(first != second);
    CHECK(first.toolId() == second.toolId());
}

TEST_CASE("a malformed instance id reports no ordinal rather than guessing", "[tools][instance]")
{
    // Guessing would let a corrupt saved workspace collide with instance 1 and
    // silently resurrect a tool the user had closed.
    CHECK_FALSE(ToolInstanceId("tuner#").ordinal().has_value());
    CHECK_FALSE(ToolInstanceId("tuner#x").ordinal().has_value());
    CHECK_FALSE(ToolInstanceId("tuner#2x").ordinal().has_value());
    CHECK_FALSE(ToolInstanceId("").ordinal().has_value());

    CHECK_FALSE(ToolInstanceId("tuner#").isWellFormed());
    CHECK_FALSE(ToolInstanceId("#2").isWellFormed());
}

TEST_CASE("instance one has exactly one spelling", "[tools][instance]")
{
    // "tuner#1" is rejected rather than accepted as an alias of "tuner": one
    // instance with two ids would defeat the dedup that keeps a
    // single-instance tool single.
    CHECK_FALSE(ToolInstanceId("tuner#1").ordinal().has_value());
    CHECK_FALSE(ToolInstanceId("tuner#0").ordinal().has_value());
}
