#pragma once

#include "ToolCatalog.h"
#include "ToolComponent.h"
#include "ToolServices.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Binds a tool id to the code that builds it.
struct ToolRegistration
{
    std::string id;
    std::function<std::unique_ptr<ToolComponent>(const ToolServices&)> factory;
};

// The catalog plus the factories: everything needed to go from a tool id to a
// live component.
//
// Split from ToolCatalog so that identity, naming, and instance policy stay
// free of JUCE and can be tested without a display. This half is the only part
// that needs a window to exercise.
class ToolRegistry
{
  public:
    ToolRegistry(const ToolCatalog& toolCatalog, std::vector<ToolRegistration> toolRegistrations)
        : definitions(toolCatalog)
    {
        for (auto& registration : toolRegistrations)
        {
            if (definitions.find(registration.id) == nullptr || registration.factory == nullptr ||
                findRegistration(registration.id) != nullptr)
            {
                unmatched.push_back(registration.id);
                continue;
            }
            registrations.push_back(std::move(registration));
        }

        // A catalog entry with no factory would show up in the Tools menu and
        // then fail to open. Surfacing it here lets the registration site
        // assert rather than shipping a dead menu item.
        for (const auto& definition : definitions.tools())
        {
            if (findRegistration(definition.id) == nullptr)
            {
                unmatched.push_back(definition.id);
            }
        }
    }

    [[nodiscard]] const ToolCatalog& catalog() const noexcept
    {
        return definitions;
    }

    [[nodiscard]] const std::vector<ToolDefinition>& tools() const noexcept
    {
        return definitions.tools();
    }

    // Ids that were registered without a catalog entry, or catalogued without a
    // factory. Non-empty means the two halves have drifted, which is a
    // programming error rather than a runtime condition.
    [[nodiscard]] const std::vector<std::string>& unmatchedIds() const noexcept
    {
        return unmatched;
    }

    [[nodiscard]] bool isConsistent() const noexcept
    {
        return unmatched.empty() && definitions.rejectedIds().empty();
    }

    // Builds a new component for the given tool. Null when the id is unknown,
    // which callers must treat as an error rather than an empty pane.
    [[nodiscard]] std::unique_ptr<ToolComponent>
    create(std::string_view toolId, const ToolServices& services) const
    {
        const auto* registration = findRegistration(toolId);
        if (registration == nullptr)
        {
            return nullptr;
        }

        auto component = registration->factory(services);
        if (component != nullptr)
        {
            component->setTheme(services.initialTheme);
        }
        return component;
    }

  private:
    [[nodiscard]] const ToolRegistration* findRegistration(std::string_view id) const noexcept
    {
        for (const auto& registration : registrations)
        {
            if (registration.id == id)
            {
                return &registration;
            }
        }
        return nullptr;
    }

    const ToolCatalog& definitions;
    std::vector<ToolRegistration> registrations;
    std::vector<std::string> unmatched;
};
