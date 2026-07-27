#include "features/performance/PerformanceLabController.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
performance::BenchmarkRunRecord recordFor(
    std::string runId,
    std::string strategyId,
    double median,
    performance::RunStatus status = performance::RunStatus::completed)
{
    performance::BenchmarkRunRecord record;
    record.runId = std::move(runId);
    record.status = status;
    record.configuration.scenarioId = "sustained-audio-analysis";
    record.configuration.workloadId = "deterministic-harmonic-fixture";
    record.configuration.strategyId = std::move(strategyId);
    record.configuration.audio = {"Fixture device", "Test", 48000.0, 256};
    record.provenance.applicationVersion = "1.0.0";
    record.provenance.commit = "same-commit";
    record.provenance.buildType = "Debug";
    record.provenance.operatingSystem = "Test OS";
    record.provenance.cpu = "Test CPU";
    record.provenance.memoryBytes = 1024;
    record.provenance.audio = record.configuration.audio;
    record.provenance.instrumentationVersion = "1";
    record.summaries.push_back({"analysis-latency", "ms", 2, median, median, median, median, 0.0});
    return record;
}

performance::RunConfiguration validConfiguration(std::string strategyId = "baseline")
{
    performance::RunConfiguration configuration;
    configuration.scenarioId = "sustained-audio-analysis";
    configuration.workloadId = "deterministic-harmonic-fixture";
    configuration.strategyId = std::move(strategyId);
    configuration.warmUpCount = 1;
    configuration.measuredTrialCount = 2;
    configuration.audio = {"Fixture device", "Test", 48000.0, 256};
    return configuration;
}
} // namespace

TEST_CASE("performance lab validates configuration inline and blocks invalid runs")
{
    performance::PerformanceLabController controller(
        [](const auto& configuration)
        {
            return configuration.audio.sampleRateHz > 0.0
                       ? performance::ValidationResult::success()
                       : performance::ValidationResult::failure("Select a sample rate");
        },
        [](const auto&, const auto&, const auto&) { return recordFor("unused", "baseline", 1.0); });

    REQUIRE_FALSE(controller.state().validation.valid);
    REQUIRE_FALSE(controller.startRun());
    REQUIRE(
        controller.state().validation.errors == std::vector<std::string>{"Select a sample rate"});

    controller.setConfiguration(validConfiguration());
    REQUIRE(controller.state().validation.valid);
}

TEST_CASE("performance lab reports stable progress and preserves cancelled trial results")
{
    performance::PerformanceLabController* controllerAddress = nullptr;
    performance::PerformanceLabController controller(
        [](const auto&) { return performance::ValidationResult::success(); },
        [&](const auto& configuration, const auto& reportProgress,
            const auto& cancellationRequested)
        {
            reportProgress(
                {performance::RunnerPhase::warmingUp, 0, configuration.measuredTrialCount});
            controllerAddress->requestCancellation();
            REQUIRE(cancellationRequested());
            auto record = recordFor(
                "cancelled", configuration.strategyId, 2.0, performance::RunStatus::cancelled);
            record.statusDetail = "Cancelled after a safe trial boundary";
            record.trials.push_back({});
            return record;
        });
    controllerAddress = &controller;
    controller.setConfiguration(validConfiguration());

    REQUIRE(controller.startRun());
    REQUIRE_FALSE(controller.state().running);
    REQUIRE(controller.state().progress.phase == performance::RunnerPhase::warmingUp);
    REQUIRE(controller.state().history.front().status == performance::RunStatus::cancelled);
    REQUIRE(controller.state().history.front().trials.size() == 1);
    REQUIRE(controller.state().view == performance::PerformanceLabView::results);
}

TEST_CASE("performance lab reopens and exports selected results and reports errors")
{
    bool exported = false;
    performance::PerformanceLabController controller(
        [](const auto&) { return performance::ValidationResult::success(); },
        [](const auto&, const auto&, const auto&) { return recordFor("run-1", "baseline", 2.0); },
        [](std::string_view runId) -> std::optional<performance::BenchmarkRunRecord>
        {
            if (runId == "saved")
                return recordFor("saved", "baseline", 2.0);
            return std::nullopt;
        },
        [&](const auto& record, std::string_view destination)
        {
            exported = record.runId == "saved" && destination == "report.json";
            return exported;
        });

    REQUIRE_FALSE(controller.exportSelected("report.json"));
    REQUIRE(controller.state().error.has_value());
    REQUIRE_FALSE(controller.reopen("missing"));
    REQUIRE(controller.reopen("saved"));
    REQUIRE(controller.exportSelected("report.json"));
    REQUIRE(exported);
    REQUIRE_FALSE(controller.state().error.has_value());
}

TEST_CASE("performance lab blocks comparison labels and explains incompatible fields")
{
    std::size_t runNumber = 0;
    performance::PerformanceLabController controller(
        [](const auto&) { return performance::ValidationResult::success(); },
        [&](const auto& configuration, const auto&, const auto&)
        {
            auto record = recordFor(std::to_string(++runNumber), configuration.strategyId, 2.0);
            if (runNumber == 2)
                record.provenance.audio.bufferSizeFrames = 512;
            return record;
        });

    controller.setConfiguration(validConfiguration("baseline"));
    REQUIRE(controller.startRun());
    controller.setConfiguration(validConfiguration("parameterized-analysis"));
    REQUIRE(controller.startRun());
    REQUIRE(controller.selectComparison(0, 1));
    REQUIRE_FALSE(controller.state().comparison->comparable);
    REQUIRE_FALSE(controller.state().comparison->mismatches.empty());
    REQUIRE(controller.state().comparison->metrics.empty());
}

TEST_CASE(
    "performance lab runs baseline and parameterized fixture in one session and compares them")
{
    std::size_t runNumber = 0;
    performance::PerformanceLabController controller(
        [](const auto& configuration)
        {
            return configuration.strategyId == "baseline" ||
                           configuration.strategyId == "parameterized-analysis"
                       ? performance::ValidationResult::success()
                       : performance::ValidationResult::failure("Unknown strategy");
        },
        [&](const auto& configuration, const auto& reportProgress, const auto&)
        {
            reportProgress({performance::RunnerPhase::measuring, 2, 2});
            return recordFor(
                std::to_string(++runNumber), configuration.strategyId,
                configuration.strategyId == "baseline" ? 2.0 : 1.5);
        });

    controller.setConfiguration(validConfiguration("baseline"));
    REQUIRE(controller.startRun());
    controller.setConfiguration(validConfiguration("parameterized-analysis"));
    REQUIRE(controller.startRun());
    REQUIRE(controller.state().history.size() == 2);
    REQUIRE(controller.selectComparison(0, 1));
    REQUIRE(controller.state().comparison->comparable);
    REQUIRE(controller.state().comparison->metrics.size() == 1);
    REQUIRE(controller.state().comparison->metrics.front().relativePercentChange == -25.0);
}
