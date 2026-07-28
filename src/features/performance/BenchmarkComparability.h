#pragma once

#include "BenchmarkTypes.h"

#include <string>
#include <vector>

namespace performance
{
struct ComparabilityMismatch
{
    std::string field;
    std::string baselineValue;
    std::string candidateValue;
};

struct ComparabilityResult
{
    bool comparable = true;
    std::vector<ComparabilityMismatch> mismatches;
};

[[nodiscard]] ComparabilityResult
validateComparability(const BenchmarkRunRecord& baseline, const BenchmarkRunRecord& candidate);
} // namespace performance