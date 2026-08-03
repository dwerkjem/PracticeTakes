#pragma once

#include "ToolInstanceId.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// How many live copies of a tool may exist at once.
//
// Every tool currently ships `single`. `multi` is declared and enforced but
// unexercised in production -- it exists so that adding a duplicatable tool
// later is a one-field change rather than a redesign. See
// openspec/changes/tool-registry-contract/design.md.
enum class ToolInstancePolicy
{
    single,
    multi
};

struct ToolPreferredSize
{
    int width = 0;
    int height = 0;

    bool operator==(const ToolPreferredSize&) const = default;
};

// Everything about a tool that can be stated without JUCE.
//
// The factory and the settings codec live in ToolRegistry instead, so that this
// half stays testable without a display -- which is also what lets the test
// control surface name tools without reaching behind JuceHeader.
struct ToolDefinition
{
    std::string id;
    std::string displayName;

    // Identifiers this tool used to be saved under. Kept so an older saved
    // workspace still resolves after a rename.
    std::vector<std::string> aliases;

    ToolInstancePolicy instancePolicy = ToolInstancePolicy::single;

    // Set when the tool serialises per-instance settings; a stored payload
    // whose version differs is discarded rather than migrated.
    std::optional<int> settingsVersion;

    ToolPreferredSize preferredSize;
};

// The roster of tools the application knows about, keyed by tool id.
//
// Instances are keyed separately -- see ToolInstanceId. A catalog entry is
// shared by every instance of that tool, which is why two instances of one tool
// would share a name and a factory but get their own settings payloads.
class ToolCatalog
{
  public:
    // A tool id becomes part of an instance id, where '#' separates the tool
    // from its ordinal. Allowing it in an id would make "a#2" ambiguous.
    [[nodiscard]] static bool isValidToolId(std::string_view id) noexcept
    {
        return !id.empty() && id.find('#') == std::string_view::npos;
    }

    ToolCatalog() = default;

    // Definitions with an invalid id are dropped and listed in rejectedIds(),
    // rather than throwing from what is often a static initialiser. Callers
    // that build a catalog from hardcoded registrations should treat a
    // non-empty rejectedIds() as a programming error.
    explicit ToolCatalog(std::vector<ToolDefinition> toolDefinitions)
    {
        definitions.reserve(toolDefinitions.size());
        for (auto& definition : toolDefinitions)
        {
            if (!isValidToolId(definition.id) || find(definition.id) != nullptr)
            {
                rejected.push_back(definition.id);
                continue;
            }
            definitions.push_back(std::move(definition));
        }
    }

    [[nodiscard]] const std::vector<ToolDefinition>& tools() const noexcept
    {
        return definitions;
    }

    [[nodiscard]] const std::vector<std::string>& rejectedIds() const noexcept
    {
        return rejected;
    }

    [[nodiscard]] const ToolDefinition* find(std::string_view id) const noexcept
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

    // The definition an instance was created from.
    [[nodiscard]] const ToolDefinition* findForInstance(const ToolInstanceId& instance) const
    {
        return instance.isWellFormed() ? find(instance.toolId()) : nullptr;
    }

    // Maps an id or a historical alias onto the canonical tool id.
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

    // Maps a saved instance id onto its canonical spelling, resolving an alias
    // in the tool half while preserving the ordinal. An id whose tool is
    // unknown, or whose ordinal is malformed, yields nullopt so the caller can
    // drop it rather than restore something wrong.
    [[nodiscard]] std::optional<ToolInstanceId>
    resolveInstance(const ToolInstanceId& instance) const
    {
        const auto ordinal = instance.ordinal();
        if (!ordinal.has_value())
        {
            return std::nullopt;
        }

        const auto toolId = resolve(instance.toolId());
        if (!toolId.has_value())
        {
            return std::nullopt;
        }
        return ToolInstanceId::forOrdinal(*toolId, *ordinal);
    }

  private:
    std::vector<ToolDefinition> definitions;
    std::vector<std::string> rejected;
};
