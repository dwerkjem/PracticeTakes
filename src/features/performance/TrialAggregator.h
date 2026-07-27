#pragma once

#include "BenchmarkTypes.h"

#include <cstdint>
#include <vector>

namespace performance
{
struct TrialEventTotals
{
    std::uint64_t deadlineMisses = 0;
    std::uint64_t dropouts = 0;
    std::uint64_t underruns = 0;
};

struct TrialAggregation
{
    std::vector<MetricSummary> summaries;
    TrialEventTotals eventTotals;
};

class TrialAggregator final
{
  public:
    [[nodiscard]] static TrialAggregation aggregate(const std::vector<TrialMeasurement>& trials);
};
} // namespace performance