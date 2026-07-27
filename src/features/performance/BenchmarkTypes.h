#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace performance
{
inline constexpr std::uint32_t benchmarkRecordSchemaVersion = 1;

enum class RunStatus
{
    pending,
    running,
    completed,
    cancelled,
    failed
};

enum class WarningCode
{
    unknownProvenance,
    ambientLoad,
    thermalThrottling,
    instrumentationOverheadUnknown,
    incompleteRun
};

struct BenchmarkWarning
{
    WarningCode code = WarningCode::unknownProvenance;
    std::string message;
};

struct AudioConfiguration
{
    std::string deviceName;
    std::string backend;
    double sampleRateHz = 0.0;
    std::uint32_t bufferSizeFrames = 0;
};

struct RunConfiguration
{
    std::string scenarioId;
    std::string workloadId;
    std::string strategyId = "baseline";
    std::unordered_map<std::string, std::string> strategyParameters;
    std::uint32_t warmUpCount = 1;
    std::uint32_t measuredTrialCount = 5;
    AudioConfiguration audio;
};

struct RunProvenance
{
    std::string applicationVersion;
    std::string commit;
    std::string buildType;
    std::string operatingSystem;
    std::string cpu;
    std::optional<std::uint64_t> memoryBytes;
    AudioConfiguration audio;
    std::string instrumentationVersion;
};

struct MetricSample
{
    std::string metricId;
    double value = 0.0;
    std::string unit;
};

struct TrialMeasurement
{
    std::uint32_t trialIndex = 0;
    std::chrono::system_clock::time_point startedAt;
    std::chrono::nanoseconds duration{};
    std::vector<MetricSample> samples;
    std::uint64_t deadlineMisses = 0;
    std::uint64_t dropouts = 0;
    std::uint64_t underruns = 0;
};

struct MetricSummary
{
    std::string metricId;
    std::string unit;
    std::size_t sampleCount = 0;
    double median = 0.0;
    double tailPercentile = 0.0;
    double minimum = 0.0;
    double maximum = 0.0;
    double variability = 0.0;
};

struct BenchmarkRunRecord
{
    std::uint32_t schemaVersion = benchmarkRecordSchemaVersion;
    std::string runId;
    RunStatus status = RunStatus::pending;
    RunConfiguration configuration;
    RunProvenance provenance;
    std::vector<TrialMeasurement> trials;
    std::vector<MetricSummary> summaries;
    std::vector<BenchmarkWarning> warnings;
    std::optional<std::string> statusDetail;
};
} // namespace performance