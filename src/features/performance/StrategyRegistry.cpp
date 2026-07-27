#include "StrategyRegistry.h"

#include "InTreeOptimizationStrategy.h"

#include <charconv>

namespace performance
{
StrategyRegistry::StrategyRegistry()
{
    entries.push_back(
        {"baseline", []
         {
             return std::make_unique<InTreeOptimizationStrategy>(
                 "baseline", "Production baseline", "1", std::vector<StrategyParameter>{},
                 [](const auto& parameters)
                 {
                     return parameters.empty()
                                ? ValidationResult::success()
                                : ValidationResult::failure("baseline accepts no parameters");
                 },
                 [](const auto&) { return true; }, [](const auto&) {}, [] {});
         }});
    entries.push_back(
        {"parameterized-fixture", []
         {
             return std::make_unique<InTreeOptimizationStrategy>(
                 "parameterized-fixture", "Parameterized optimization fixture", "1",
                 std::vector<StrategyParameter>{
                     {"batchSize", "Batch size", ParameterType::integer, "4", {}}},
                 [](const auto& parameters)
                 {
                     const auto value = parameters.find("batchSize");
                     if (value == parameters.end())
                         return ValidationResult::failure("batchSize is required");
                     int batchSize = 0;
                     const auto [end, error] = std::from_chars(
                         value->second.data(), value->second.data() + value->second.size(),
                         batchSize);
                     if (error == std::errc{} &&
                         end == value->second.data() + value->second.size() && batchSize > 0)
                         return ValidationResult::success();
                     return ValidationResult::failure("batchSize must be a positive integer");
                 },
                 [](const auto&) { return true; }, [](const auto&) {}, [] {});
         }});
}

std::vector<std::string> StrategyRegistry::ids() const
{
    std::vector<std::string> result;
    result.reserve(entries.size());
    for (const auto& entry : entries)
        result.push_back(entry.id);
    return result;
}

std::unique_ptr<OptimizationStrategy> StrategyRegistry::create(std::string_view id) const
{
    for (const auto& entry : entries)
        if (entry.id == id)
            return entry.factory();
    return {};
}
} // namespace performance