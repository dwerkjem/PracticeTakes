#include "features/performance/InTreeOptimizationStrategy.h"
#include "support/BenchmarkFakes.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("an in-tree strategy runs a parameterized optimization against baseline")
{
    int activeRefreshRate = 60;
    const auto baselineRefreshRate = activeRefreshRate;
    performance::InTreeOptimizationStrategy strategy(
        "refresh-rate", "Refresh rate", "1",
        {{"refreshHz", "Refresh rate", performance::ParameterType::integer, "30", {}}},
        [](const auto& parameters)
        {
            return parameters.contains("refreshHz")
                       ? performance::ValidationResult::success()
                       : performance::ValidationResult::failure("refreshHz is required");
        },
        [](const auto& scenario) { return scenario.id() == "fake-scenario"; },
        [&](const auto& parameters) { activeRefreshRate = std::stoi(parameters.at("refreshHz")); },
        [&] { activeRefreshRate = baselineRefreshRate; });
    FakeBenchmarkScenario scenario;

    REQUIRE(strategy.id() == "refresh-rate");
    REQUIRE(strategy.isCompatible(scenario));
    REQUIRE_FALSE(strategy.validateParameters({}).valid);
    REQUIRE(activeRefreshRate == baselineRefreshRate);

    strategy.activate({{"refreshHz", "30"}});
    REQUIRE(activeRefreshRate == 30);

    strategy.deactivate();
    REQUIRE(activeRefreshRate == baselineRefreshRate);
}

TEST_CASE("baseline and an in-tree strategy share one executable implementation")
{
    bool optimizationEnabled = false;
    performance::InTreeOptimizationStrategy strategy(
        "optimized-path", "Optimized path", "1", {}, [](const auto&)
        { return performance::ValidationResult::success(); }, [](const auto&) { return true; },
        [&](const auto&) { optimizationEnabled = true; }, [&] { optimizationEnabled = false; });

    REQUIRE_FALSE(optimizationEnabled);
    strategy.activate({});
    REQUIRE(optimizationEnabled);
    strategy.deactivate();
    REQUIRE_FALSE(optimizationEnabled);
}