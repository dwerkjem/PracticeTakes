#pragma once

#include "BenchmarkComparability.h"

#include <optional>

namespace performance
{
struct MetricComparison
{
    std::string metricId;
    std::string unit;
    std::optional<double> baselineValue;
    std::optional<double> candidateValue;
    std::optional<double> absoluteChange;
    std::optional<double> relativePercentChange;
};

struct BenchmarkComparisonResult
{
    bool comparable = false;
    std::vector<ComparabilityMismatch> mismatches;
    std::vector<MetricComparison> metrics;
};

[[nodiscard]] BenchmarkComparisonResult
compareBenchmarkRuns(const BenchmarkRunRecord& baseline, const BenchmarkRunRecord& candidate);
} // namespace performance