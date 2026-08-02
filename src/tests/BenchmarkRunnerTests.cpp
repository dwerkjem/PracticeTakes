#include "features/performance/BenchmarkRunner.h"
#include "support/BenchmarkFakes.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("benchmark runner completes stabilization warm-up measurement aggregation and cleanup")
{
    FakeBenchmarkScenario scenario;
    FakeOptimizationStrategy strategy;
    FakeTelemetryCollector telemetry;
    std::vector<performance::RunnerPhase> phases;
    std::uint32_t stabilizationCount = 0;
    std::uint32_t aggregationCount = 0;
    performance::BenchmarkRunner runner(
        [&] { ++stabilizationCount; },
        [&](const auto& progress) { phases.push_back(progress.phase); },
        [&](const auto& trials) { aggregationCount = static_cast<std::uint32_t>(trials.size()); });
    performance::RunConfiguration configuration;
    configuration.scenarioId = scenario.scenarioId;
    configuration.workloadId = scenario.workload;
    configuration.strategyId = strategy.strategyId;
    configuration.warmUpCount = 2;
    configuration.measuredTrialCount = 3;

    const auto result = runner.run(configuration, scenario, strategy, telemetry);

    REQUIRE(result.validation.valid);
    REQUIRE(result.status == performance::RunStatus::completed);
    REQUIRE(result.trials.size() == 3);
    REQUIRE(stabilizationCount == 1);
    REQUIRE(scenario.prepareCount == 1);
    REQUIRE(scenario.runCount == 5);
    REQUIRE(telemetry.beginCount == 3);
    REQUIRE(telemetry.endCount == 3);
    REQUIRE(aggregationCount == 3);
    REQUIRE(scenario.restoreCount == 1);
    REQUIRE(strategy.activateCount == 1);
    REQUIRE(strategy.deactivateCount == 1);
    REQUIRE(phases.front() == performance::RunnerPhase::validating);
    REQUIRE(phases.back() == performance::RunnerPhase::completed);
    REQUIRE(runner.progress().completedTrials == 3);
    REQUIRE(runner.progress().totalTrials == 3);
}

TEST_CASE("benchmark runner cancels between trials and preserves completed measurements")
{
    FakeBenchmarkScenario scenario;
    FakeOptimizationStrategy strategy;
    FakeTelemetryCollector telemetry;
    performance::RunConfiguration configuration;
    configuration.warmUpCount = 0;
    configuration.measuredTrialCount = 5;
    performance::BenchmarkRunner runner({}, {}, {}, [&] { return telemetry.endCount == 2; });

    const auto result = runner.run(configuration, scenario, strategy, telemetry);

    REQUIRE(result.status == performance::RunStatus::cancelled);
    REQUIRE(result.trials.size() == 2);
    REQUIRE(telemetry.beginCount == telemetry.endCount);
    REQUIRE(scenario.restoreCount == 1);
    REQUIRE(strategy.deactivateCount == 1);
}

TEST_CASE("benchmark runner restores application state after a scenario failure")
{
    FakeBenchmarkScenario scenario;
    scenario.throwOnRunNumber = 1;
    FakeOptimizationStrategy strategy;
    FakeTelemetryCollector telemetry;
    performance::RunConfiguration configuration;
    configuration.warmUpCount = 0;
    configuration.measuredTrialCount = 3;
    performance::BenchmarkRunner runner;

    const auto result = runner.run(configuration, scenario, strategy, telemetry);

    REQUIRE(result.status == performance::RunStatus::failed);
    REQUIRE(result.trials.empty());
    REQUIRE(telemetry.beginCount == 1);
    REQUIRE(telemetry.endCount == 0);
    REQUIRE(telemetry.abortCount == 1);
    REQUIRE(result.statusDetail == "Fake scenario trial failed");
    REQUIRE(scenario.restoreCount == 1);
    REQUIRE(strategy.deactivateCount == 1);
}

TEST_CASE("benchmark runner rejects invalid configuration before changing application state")
{
    FakeBenchmarkScenario scenario;
    scenario.validationResult = performance::ValidationResult::failure("Invalid scenario setup");
    FakeOptimizationStrategy strategy;
    strategy.parameterValidationResult =
        performance::ValidationResult::failure("Invalid strategy parameter");
    FakeTelemetryCollector telemetry;
    performance::BenchmarkRunner runner;

    const auto result = runner.run(performance::RunConfiguration{}, scenario, strategy, telemetry);

    REQUIRE_FALSE(result.validation.valid);
    REQUIRE(result.validation.errors.size() == 2);
    REQUIRE(result.status == performance::RunStatus::pending);
    REQUIRE(scenario.prepareCount == 0);
    REQUIRE(strategy.activateCount == 0);
    REQUIRE(telemetry.beginCount == 0);
}

TEST_CASE("benchmark runner fails correctness after preserving trials and cleaning up")
{
    FakeBenchmarkScenario scenario;
    scenario.correctnessResult =
        performance::ValidationResult::failure("Output differs from baseline");
    FakeOptimizationStrategy strategy;
    FakeTelemetryCollector telemetry;
    performance::RunConfiguration configuration;
    configuration.warmUpCount = 0;
    configuration.measuredTrialCount = 2;
    performance::BenchmarkRunner runner;

    const auto result = runner.run(configuration, scenario, strategy, telemetry);

    REQUIRE_FALSE(result.validation.valid);
    REQUIRE(result.status == performance::RunStatus::failed);
    REQUIRE(result.statusDetail == "Scenario correctness verification failed");
    REQUIRE(result.trials.size() == 2);
    REQUIRE(scenario.restoreCount == 1);
    REQUIRE(strategy.deactivateCount == 1);
}