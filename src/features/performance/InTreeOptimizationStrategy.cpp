#include "InTreeOptimizationStrategy.h"

#include <utility>

namespace performance
{
InTreeOptimizationStrategy::InTreeOptimizationStrategy(
    std::string id,
    std::string displayName,
    std::string implementationVersion,
    std::vector<StrategyParameter> parameters,
    Validator validator,
    Compatibility compatibility,
    Activator activator,
    Deactivator deactivator)
    : strategyId(std::move(id)), strategyDisplayName(std::move(displayName)),
      strategyImplementationVersion(std::move(implementationVersion)),
      parameters(std::move(parameters)), validator(std::move(validator)),
      compatibility(std::move(compatibility)), activator(std::move(activator)),
      deactivator(std::move(deactivator))
{
}

std::string_view InTreeOptimizationStrategy::id() const noexcept
{
    return strategyId;
}

std::string_view InTreeOptimizationStrategy::displayName() const noexcept
{
    return strategyDisplayName;
}

std::string_view InTreeOptimizationStrategy::implementationVersion() const noexcept
{
    return strategyImplementationVersion;
}

std::vector<StrategyParameter> InTreeOptimizationStrategy::parameterSchema() const
{
    return parameters;
}

ValidationResult
InTreeOptimizationStrategy::validateParameters(const Parameters& suppliedParameters) const
{
    return validator(suppliedParameters);
}

bool InTreeOptimizationStrategy::isCompatible(const BenchmarkScenario& scenario) const noexcept
{
    return compatibility(scenario);
}

void InTreeOptimizationStrategy::activate(const Parameters& suppliedParameters)
{
    activator(suppliedParameters);
}

void InTreeOptimizationStrategy::deactivate() noexcept
{
    deactivator();
}
} // namespace performance