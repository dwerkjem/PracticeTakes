#include "support/BenchmarkFakes.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("benchmark fakes provide deterministic time telemetry and lifecycle observations")
{
    FakeBenchmarkClock clock;
    FakeTelemetryCollector telemetry;
    FakeBenchmarkScenario scenario;
    FakeOptimizationStrategy strategy;

    clock.advance(std::chrono::milliseconds(25));
    telemetry.nextMeasurement.samples = {{"process-cpu", 8.0, "percent"}};
    telemetry.beginTrial(4);
    scenario.prepare();
    strategy.activate({{"refreshHz", "30"}});
    scenario.runTrial();
    const auto measurement = telemetry.endTrial();
    strategy.deactivate();
    scenario.restore();

    REQUIRE(clock.now().time_since_epoch() == std::chrono::milliseconds(25));
    REQUIRE(measurement.trialIndex == 4);
    REQUIRE(measurement.samples.front().value == 8.0);
    REQUIRE(scenario.prepareCount == 1);
    REQUIRE(scenario.runCount == 1);
    REQUIRE(scenario.restoreCount == 1);
    REQUIRE(strategy.activeParameters.at("refreshHz") == "30");
    REQUIRE(strategy.activateCount == 1);
    REQUIRE(strategy.deactivateCount == 1);
}

TEST_CASE("benchmark fakes expose configurable validation and compatibility outcomes")
{
    FakeBenchmarkScenario scenario;
    FakeOptimizationStrategy strategy;
    scenario.correctnessResult = performance::ValidationResult::failure("output mismatch");
    strategy.compatible = false;
    strategy.parameterValidationResult =
        performance::ValidationResult::failure("invalid parameter");

    REQUIRE_FALSE(scenario.verifyCorrectness().valid);
    REQUIRE(scenario.verifyCorrectness().errors.front() == "output mismatch");
    REQUIRE_FALSE(strategy.isCompatible(scenario));
    REQUIRE_FALSE(strategy.validateParameters({}).valid);
}