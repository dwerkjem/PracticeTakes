#pragma once

#include "BenchmarkInterfaces.h"

#include <functional>

namespace performance
{
class InTreeOptimizationStrategy final : public OptimizationStrategy
{
  public:
    using Parameters = std::unordered_map<std::string, std::string>;
    using Validator = std::function<ValidationResult(const Parameters&)>;
    using Compatibility = std::function<bool(const BenchmarkScenario&)>;
    using Activator = std::function<void(const Parameters&)>;
    using Deactivator = std::function<void()>;

    InTreeOptimizationStrategy(
        std::string id,
        std::string displayName,
        std::string implementationVersion,
        std::vector<StrategyParameter> parameters,
        Validator validator,
        Compatibility compatibility,
        Activator activator,
        Deactivator deactivator);

    [[nodiscard]] std::string_view id() const noexcept override;
    [[nodiscard]] std::string_view displayName() const noexcept override;
    [[nodiscard]] std::string_view implementationVersion() const noexcept override;
    [[nodiscard]] std::vector<StrategyParameter> parameterSchema() const override;
    [[nodiscard]] ValidationResult validateParameters(const Parameters& parameters) const override;
    [[nodiscard]] bool isCompatible(const BenchmarkScenario& scenario) const noexcept override;
    void activate(const Parameters& parameters) override;
    void deactivate() noexcept override;

  private:
    std::string strategyId;
    std::string strategyDisplayName;
    std::string strategyImplementationVersion;
    std::vector<StrategyParameter> parameters;
    Validator validator;
    Compatibility compatibility;
    Activator activator;
    Deactivator deactivator;
};
} // namespace performance