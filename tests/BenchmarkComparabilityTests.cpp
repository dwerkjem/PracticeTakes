#include "features/performance/BenchmarkComparability.h"

#include <catch2/catch_test_macros.hpp>

#include <functional>

namespace
{
performance::BenchmarkRunRecord comparableRecord()
{
    performance::BenchmarkRunRecord record;
    record.configuration.scenarioId = "scenario";
    record.configuration.workloadId = "workload";
    record.configuration.strategyId = "baseline";
    record.provenance.commit = "commit";
    record.provenance.buildType = "Release";
    record.provenance.operatingSystem = "Linux";
    record.provenance.cpu = "CPU";
    record.provenance.memoryBytes = 1024;
    record.provenance.audio = {"Device", "ALSA", 48000.0, 128};
    record.provenance.instrumentationVersion = "1";
    return record;
}
} // namespace

TEST_CASE("comparability validation names every material mismatch")
{
    using Mutator = std::function<void(performance::BenchmarkRunRecord&)>;
    const std::vector<std::pair<std::string, Mutator>> cases = {
        {"scenarioId", [](auto& record) { record.configuration.scenarioId = "other"; }},
        {"workloadId", [](auto& record) { record.configuration.workloadId = "other"; }},
        {"applicationCommit", [](auto& record) { record.provenance.commit = "other"; }},
        {"buildType", [](auto& record) { record.provenance.buildType = "Debug"; }},
        {"operatingSystem", [](auto& record) { record.provenance.operatingSystem = "other"; }},
        {"cpu", [](auto& record) { record.provenance.cpu = "other"; }},
        {"memoryBytes", [](auto& record) { record.provenance.memoryBytes = 2048; }},
        {"audioDevice", [](auto& record) { record.provenance.audio.deviceName = "other"; }},
        {"audioBackend", [](auto& record) { record.provenance.audio.backend = "other"; }},
        {"sampleRateHz", [](auto& record) { record.provenance.audio.sampleRateHz = 44100.0; }},
        {"bufferSizeFrames", [](auto& record) { record.provenance.audio.bufferSizeFrames = 256; }},
        {"instrumentationVersion",
         [](auto& record) { record.provenance.instrumentationVersion = "2"; }}};

    for (const auto& [field, mutate] : cases)
    {
        DYNAMIC_SECTION(field)
        {
            const auto baseline = comparableRecord();
            auto candidate = baseline;
            mutate(candidate);

            const auto result = performance::validateComparability(baseline, candidate);

            REQUIRE_FALSE(result.comparable);
            REQUIRE(result.mismatches.size() == 1);
            REQUIRE(result.mismatches.front().field == field);
            REQUIRE(
                result.mismatches.front().baselineValue !=
                result.mismatches.front().candidateValue);
        }
    }
}

TEST_CASE("strategy differences are intentionally comparable")
{
    const auto baseline = comparableRecord();
    auto candidate = baseline;
    candidate.configuration.strategyId = "parameterized-fixture";
    candidate.configuration.strategyParameters = {{"batchSize", "8"}};

    REQUIRE(performance::validateComparability(baseline, candidate).comparable);
}