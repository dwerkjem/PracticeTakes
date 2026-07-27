#include "features/performance/BenchmarkRecordCodec.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("benchmark records survive a structured serialization round trip")
{
    performance::BenchmarkRunRecord original;
    original.runId = "run-42";
    original.status = performance::RunStatus::cancelled;
    original.configuration = {
        "sustained-analysis",
        "reference-audio-v1",
        "refresh-rate",
        {{"refreshHz", "30"}},
        2,
        7,
        {"USB Interface", "ALSA", 48000.0, 128}};
    original.provenance = {
        "1.2.3",
        "abc123",
        "Release",
        "Linux",
        "Reference CPU",
        16ULL * 1024ULL * 1024ULL * 1024ULL,
        {"USB Interface", "ALSA", 48000.0, 128},
        "telemetry-v1"};
    original.trials = {
        {3,
         std::chrono::system_clock::time_point(std::chrono::seconds(123)),
         std::chrono::milliseconds(250),
         {{"process-cpu", 12.5, "percent"}},
         1,
         2,
         3}};
    original.summaries = {{"process-cpu", "percent", 1, 12.5, 12.5, 12.5, 12.5, 0.0}};
    original.warnings = {{performance::WarningCode::ambientLoad, "Background load detected"}};
    original.statusDetail = "Cancelled by user";

    const auto decoded = performance::BenchmarkRecordCodec::decode(
        performance::BenchmarkRecordCodec::encode(original));

    REQUIRE(decoded.status == performance::RecordDecodeStatus::loaded);
    REQUIRE(decoded.record.has_value());
    REQUIRE(decoded.record->runId == original.runId);
    REQUIRE(decoded.record->status == original.status);
    REQUIRE(
        decoded.record->configuration.strategyParameters ==
        original.configuration.strategyParameters);
    REQUIRE(decoded.record->provenance.memoryBytes == original.provenance.memoryBytes);
    REQUIRE(decoded.record->trials.front().startedAt == original.trials.front().startedAt);
    REQUIRE(decoded.record->trials.front().samples.front().value == 12.5);
    REQUIRE(decoded.record->summaries.front().sampleCount == 1);
    REQUIRE(decoded.record->warnings.front().code == performance::WarningCode::ambientLoad);
    REQUIRE(decoded.record->statusDetail == original.statusDetail);
}

TEST_CASE("benchmark record decoding rejects a forward schema version")
{
    auto encoded = performance::BenchmarkRecordCodec::encode({});
    encoded.getDynamicObject()->setProperty(
        "schemaVersion", static_cast<int>(performance::benchmarkRecordSchemaVersion + 1));

    const auto decoded = performance::BenchmarkRecordCodec::decode(encoded);

    REQUIRE(decoded.status == performance::RecordDecodeStatus::newerSchema);
    REQUIRE_FALSE(decoded.record.has_value());
}