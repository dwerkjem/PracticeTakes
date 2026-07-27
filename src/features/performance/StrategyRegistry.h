#pragma once

#include "BenchmarkInterfaces.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace performance
{
class StrategyRegistry
{
  public:
    using Factory = std::function<std::unique_ptr<OptimizationStrategy>()>;

    StrategyRegistry();

    [[nodiscard]] std::vector<std::string> ids() const;
    [[nodiscard]] std::unique_ptr<OptimizationStrategy> create(std::string_view id) const;

  private:
    struct Entry
    {
        std::string id;
        Factory factory;
    };

    std::vector<Entry> entries;
};
} // namespace performance