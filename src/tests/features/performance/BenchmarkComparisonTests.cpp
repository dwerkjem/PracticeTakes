#include "features/performance/BenchmarkComparison.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

namespace
{
performance::BenchmarkRunRecord
recordWithSummaries(std::vector<performance::MetricSummary> summaries)
{
    performance::BenchmarkRunRecord record;
    record.configuration.scenarioId = "scenario";
    record.configuration.workloadId = "workload";
    record.summaries = std::move(summaries);
    return record;
}
} // namespace

TEST_CASE("benchmark comparison calculates absolute and relative median changes")
{
    const auto baseline = recordWithSummaries({{"latency", "ms", 3, 10.0, 12.0, 8.0, 12.0, 1.0}});
    const auto candidate = recordWithSummaries({{"latency", "ms", 3, 8.0, 10.0, 7.0, 10.0, 1.0}});

    const auto result = performance::compareBenchmarkRuns(baseline, candidate);

    REQUIRE(result.comparable);
    REQUIRE(result.metrics.front().absoluteChange == Catch::Approx(-2.0));
    REQUIRE(result.metrics.front().relativePercentChange == Catch::Approx(-20.0));
}

TEST_CASE("benchmark comparison preserves missing metrics")
{
    const auto baseline =
        recordWithSummaries({{"baseline-only", "ms", 1, 1.0, 1.0, 1.0, 1.0, 0.0}});
    const auto candidate =
        recordWithSummaries({{"candidate-only", "bytes", 1, 2.0, 2.0, 2.0, 2.0, 0.0}});

    const auto result = performance::compareBenchmarkRuns(baseline, candidate);

    REQUIRE(result.metrics.size() == 2);
    REQUIRE(result.metrics[0].baselineValue.has_value());
    REQUIRE_FALSE(result.metrics[0].candidateValue.has_value());
    REQUIRE_FALSE(result.metrics[1].baselineValue.has_value());
    REQUIRE(result.metrics[1].candidateValue.has_value());
}

TEST_CASE("benchmark comparison omits relative change for a zero baseline")
{
    const auto baseline = recordWithSummaries({{"dropouts", "count", 1, 0.0, 0.0, 0.0, 0.0, 0.0}});
    const auto candidate = recordWithSummaries({{"dropouts", "count", 1, 2.0, 2.0, 2.0, 2.0, 0.0}});

    const auto result = performance::compareBenchmarkRuns(baseline, candidate);

    REQUIRE(result.metrics.front().absoluteChange == 2.0);
    REQUIRE_FALSE(result.metrics.front().relativePercentChange.has_value());
}

TEST_CASE("benchmark comparison blocks calculations for incompatible runs")
{
    const auto baseline = recordWithSummaries({{"latency", "ms", 1, 1.0, 1.0, 1.0, 1.0, 0.0}});
    auto candidate = baseline;
    candidate.configuration.workloadId = "different";

    const auto result = performance::compareBenchmarkRuns(baseline, candidate);

    REQUIRE_FALSE(result.comparable);
    REQUIRE(result.metrics.empty());
    REQUIRE(result.mismatches.front().field == "workloadId");
}