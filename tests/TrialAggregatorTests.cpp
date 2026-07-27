#include "features/performance/TrialAggregator.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <stdexcept>

namespace
{
performance::TrialMeasurement trial(
    double latency,
    std::uint64_t deadlineMisses = 0,
    std::uint64_t dropouts = 0,
    std::uint64_t underruns = 0)
{
    performance::TrialMeasurement result;
    result.samples.push_back({"latency", latency, "milliseconds"});
    result.deadlineMisses = deadlineMisses;
    result.dropouts = dropouts;
    result.underruns = underruns;
    return result;
}
} // namespace

TEST_CASE("trial aggregation reports deterministic continuous statistics")
{
    const auto aggregation = performance::TrialAggregator::aggregate(
        {trial(5.0), trial(1.0), trial(3.0), trial(2.0), trial(4.0)});

    REQUIRE(aggregation.summaries.size() == 1);
    const auto& summary = aggregation.summaries.front();
    REQUIRE(summary.metricId == "latency");
    REQUIRE(summary.unit == "milliseconds");
    REQUIRE(summary.sampleCount == 5);
    REQUIRE(summary.median == 3.0);
    REQUIRE(summary.tailPercentile == 5.0);
    REQUIRE(summary.minimum == 1.0);
    REQUIRE(summary.maximum == 5.0);
    REQUIRE(summary.variability == Catch::Approx(std::sqrt(2.0)));
}

TEST_CASE("trial aggregation handles even samples and totals events")
{
    const auto aggregation =
        performance::TrialAggregator::aggregate({trial(10.0, 1, 2, 3), trial(20.0, 4, 5, 6)});

    REQUIRE(aggregation.summaries.front().median == 15.0);
    REQUIRE(aggregation.summaries.front().tailPercentile == 20.0);
    REQUIRE(aggregation.eventTotals.deadlineMisses == 5);
    REQUIRE(aggregation.eventTotals.dropouts == 7);
    REQUIRE(aggregation.eventTotals.underruns == 9);
}

TEST_CASE("trial aggregation orders metrics and rejects inconsistent units")
{
    auto first = trial(1.0);
    first.samples.push_back({"cpu", 10.0, "percent"});
    const auto aggregation = performance::TrialAggregator::aggregate({first});
    REQUIRE(aggregation.summaries[0].metricId == "cpu");
    REQUIRE(aggregation.summaries[1].metricId == "latency");

    auto second = trial(2.0);
    second.samples.front().unit = "seconds";
    REQUIRE_THROWS_AS(
        performance::TrialAggregator::aggregate({first, second}), std::invalid_argument);
}

TEST_CASE("trial aggregation returns empty results for no trials")
{
    const auto aggregation = performance::TrialAggregator::aggregate({});
    REQUIRE(aggregation.summaries.empty());
    REQUIRE(aggregation.eventTotals.deadlineMisses == 0);
    REQUIRE(aggregation.eventTotals.dropouts == 0);
    REQUIRE(aggregation.eventTotals.underruns == 0);
}