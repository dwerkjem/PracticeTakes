#include "features/performance/BenchmarkTypes.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("benchmark records default to the current pending schema")
{
    const performance::BenchmarkRunRecord record;

    REQUIRE(record.schemaVersion == performance::benchmarkRecordSchemaVersion);
    REQUIRE(record.status == performance::RunStatus::pending);
    REQUIRE(record.configuration.strategyId == "baseline");
    REQUIRE(record.configuration.warmUpCount == 1);
    REQUIRE(record.configuration.measuredTrialCount == 5);
}

TEST_CASE("benchmark records compose configuration provenance trials and summaries")
{
    performance::BenchmarkRunRecord record;
    record.runId = "reference-run";
    record.status = performance::RunStatus::completed;
    record.configuration.scenarioId = "sustained-analysis";
    record.configuration.workloadId = "reference-audio-v1";
    record.configuration.strategyId = "reduced-refresh";
    record.configuration.strategyParameters = {{"refreshHz", "30"}};
    record.provenance.commit = "abc123";
    record.provenance.cpu = "reference-cpu";
    record.provenance.memoryBytes = 16ULL * 1024ULL * 1024ULL * 1024ULL;
    record.trials.push_back({
        .trialIndex = 0,
        .duration = std::chrono::seconds(30),
        .samples = {{"process-cpu", 12.5, "percent"}},
        .deadlineMisses = 1,
    });
    record.summaries.push_back({
        .metricId = "process-cpu",
        .unit = "percent",
        .sampleCount = 1,
        .median = 12.5,
        .tailPercentile = 12.5,
        .minimum = 12.5,
        .maximum = 12.5,
    });

    REQUIRE(record.configuration.strategyParameters.at("refreshHz") == "30");
    REQUIRE(record.provenance.memoryBytes.has_value());
    REQUIRE(record.trials.front().deadlineMisses == 1);
    REQUIRE(record.summaries.front().sampleCount == 1);
}