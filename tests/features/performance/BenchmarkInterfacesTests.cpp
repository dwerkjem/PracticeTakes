#include "features/performance/BenchmarkInterfaces.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
class TestScenario final : public performance::BenchmarkScenario
{
  public:
    [[nodiscard]] std::string_view id() const noexcept override
    {
        return "sustained-analysis";
    }
    [[nodiscard]] std::string_view displayName() const noexcept override
    {
        return "Sustained analysis";
    }
    [[nodiscard]] std::string_view workloadId() const noexcept override
    {
        return "reference-audio-v1";
    }
    [[nodiscard]] performance::ValidationResult
    validate(const performance::RunConfiguration&) const override
    {
        return performance::ValidationResult::success();
    }
    void prepare() override {}
    void runTrial() override {}
    [[nodiscard]] performance::ValidationResult verifyCorrectness() const override
    {
        return performance::ValidationResult::success();
    }
    void restore() noexcept override {}
};

class TestStrategy final : public performance::OptimizationStrategy
{
  public:
    [[nodiscard]] std::string_view id() const noexcept override
    {
        return "refresh-rate";
    }
    [[nodiscard]] std::string_view displayName() const noexcept override
    {
        return "Refresh rate";
    }
    [[nodiscard]] std::string_view implementationVersion() const noexcept override
    {
        return "1";
    }
    [[nodiscard]] std::vector<performance::StrategyParameter> parameterSchema() const override
    {
        return {{"refreshHz", "Refresh rate", performance::ParameterType::integer, "30", {}}};
    }
    [[nodiscard]] performance::ValidationResult validateParameters(
        const std::unordered_map<std::string, std::string>& parameters) const override
    {
        return parameters.contains("refreshHz")
                   ? performance::ValidationResult::success()
                   : performance::ValidationResult::failure("refreshHz is required");
    }
    [[nodiscard]] bool
    isCompatible(const performance::BenchmarkScenario& scenario) const noexcept override
    {
        return scenario.id() == "sustained-analysis";
    }
    void activate(const std::unordered_map<std::string, std::string>&) override {}
    void deactivate() noexcept override {}
};
} // namespace

TEST_CASE("benchmark interfaces expose stable scenario and strategy metadata")
{
    const TestScenario scenario;
    const TestStrategy strategy;

    REQUIRE(scenario.id() == "sustained-analysis");
    REQUIRE(scenario.workloadId() == "reference-audio-v1");
    REQUIRE(strategy.id() == "refresh-rate");
    REQUIRE(strategy.implementationVersion() == "1");
    REQUIRE(strategy.parameterSchema().front().id == "refreshHz");
    REQUIRE(strategy.isCompatible(scenario));
}

TEST_CASE("strategy parameter and scenario correctness validation return actionable results")
{
    const TestScenario scenario;
    const TestStrategy strategy;

    const auto invalidParameters = strategy.validateParameters({});
    REQUIRE_FALSE(invalidParameters.valid);
    REQUIRE(invalidParameters.errors == std::vector<std::string>{"refreshHz is required"});
    REQUIRE(scenario.verifyCorrectness().valid);
}