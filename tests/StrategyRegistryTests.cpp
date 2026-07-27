#include "features/performance/StrategyRegistry.h"
#include "support/BenchmarkFakes.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("strategy registry exposes production baseline and runtime fixture")
{
    performance::StrategyRegistry registry;

    REQUIRE(registry.ids() == std::vector<std::string>{"baseline", "parameterized-fixture"});
    REQUIRE(registry.create("missing") == nullptr);

    auto baseline = registry.create("baseline");
    auto fixture = registry.create("parameterized-fixture");
    FakeBenchmarkScenario scenario;

    REQUIRE(baseline->id() == "baseline");
    REQUIRE(baseline->isCompatible(scenario));
    REQUIRE(baseline->validateParameters({}).valid);
    REQUIRE_FALSE(baseline->validateParameters({{"unexpected", "value"}}).valid);
    REQUIRE(fixture->isCompatible(scenario));
    REQUIRE(fixture->parameterSchema().front().id == "batchSize");
    REQUIRE(fixture->validateParameters({{"batchSize", "8"}}).valid);
    REQUIRE_FALSE(fixture->validateParameters({{"batchSize", "0"}}).valid);
    fixture->activate({{"batchSize", "8"}});
    fixture->deactivate();
}

TEST_CASE("strategy registry creates independent instances from one executable")
{
    performance::StrategyRegistry registry;

    const auto first = registry.create("parameterized-fixture");
    const auto second = registry.create("parameterized-fixture");

    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    REQUIRE(first.get() != second.get());
    REQUIRE(first->implementationVersion() == second->implementationVersion());
}