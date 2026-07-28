#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct WorkspaceToolDefinition
{
    std::string id;
    std::vector<std::string> aliases;
    std::optional<int> settingsVersion;
};

class WorkspaceToolRegistry
{
  public:
    WorkspaceToolRegistry()
        : definitions({
              {"tuner", {"pitch-detector"}, 1},
              {"spectrogram", {"spectrum"}, std::nullopt},
              {"harmonic-analyzer", {"harmonics", "harmonicAnalyzer"}, std::nullopt},
          })
    {
    }

    explicit WorkspaceToolRegistry(std::vector<WorkspaceToolDefinition> toolDefinitions)
        : definitions(std::move(toolDefinitions))
    {
    }

    [[nodiscard]] const std::vector<WorkspaceToolDefinition>& tools() const noexcept
    {
        return definitions;
    }

    [[nodiscard]] const WorkspaceToolDefinition* find(std::string_view id) const noexcept
    {
        for (const auto& definition : definitions)
        {
            if (definition.id == id)
            {
                return &definition;
            }
        }
        return nullptr;
    }

    [[nodiscard]] std::optional<std::string> resolve(std::string_view id) const
    {
        if (const auto* definition = find(id))
        {
            return definition->id;
        }

        for (const auto& definition : definitions)
        {
            for (const auto& alias : definition.aliases)
            {
                if (alias == id)
                {
                    return definition.id;
                }
            }
        }
        return std::nullopt;
    }

  private:
    std::vector<WorkspaceToolDefinition> definitions;
};