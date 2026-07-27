#pragma once

#include "WorkspaceDocuments.h"
#include "WorkspaceLayoutState.h"
#include "WorkspaceToolRegistry.h"
#include "WorkspaceToolState.h"

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

class WorkspaceSnapshotApply
{
  public:
    struct ToolBinding
    {
        std::string id;
        WorkspaceLayoutState::Tool tool;
    };

    struct ToolDirective
    {
        std::string id;
        WorkspaceLayoutState::Tool tool;
        WorkspaceToolState::Presentation presentation = WorkspaceToolState::Presentation::closed;
        WorkspaceBounds floatingBounds;
        std::optional<ToolSettingsPayload> settings;
    };

    struct Plan
    {
        std::unique_ptr<WorkspaceLayoutState::Node> layoutRoot;
        std::vector<ToolDirective> tools;
        std::optional<std::string> focusedTool;
    };

    struct Callbacks
    {
        std::function<void()> detachPresentations;
        std::function<void(std::unique_ptr<WorkspaceLayoutState::Node>)> replaceLayout;
        std::function<void(const ToolDirective&)> prepareTool;
        std::function<void(const ToolDirective&)> presentTool;
        std::function<void()> rebuildPresentation;
        std::function<void(const std::optional<std::string>&)> restoreFocus;
    };

    [[nodiscard]] static std::optional<Plan> plan(
        const WorkspaceSnapshot& snapshot,
        const WorkspaceToolRegistry& registry,
        const std::vector<ToolBinding>& bindings)
    {
        Context context{registry, bindings, {}, {}, {}};
        Plan result;

        if (snapshot.dockRoot.has_value())
        {
            result.layoutRoot = convertNode(*snapshot.dockRoot, context);
            if (result.layoutRoot == nullptr)
            {
                return std::nullopt;
            }
        }

        for (const auto& floating : snapshot.floating)
        {
            const auto resolved = registry.resolve(floating.toolId);
            const auto* binding = resolved.has_value() ? findBinding(bindings, *resolved) : nullptr;
            if (binding == nullptr || !context.placed.insert(binding->id).second)
            {
                return std::nullopt;
            }
            context.floatingBounds.insert_or_assign(binding->id, floating.bounds);
        }

        for (const auto& binding : bindings)
        {
            const auto canonicalId = registry.resolve(binding.id);
            if (!canonicalId.has_value() || *canonicalId != binding.id)
            {
                return std::nullopt;
            }

            auto directive = ToolDirective{
                binding.id,
                binding.tool,
                WorkspaceToolState::Presentation::closed,
                {},
                std::nullopt};
            if (context.docked.contains(binding.id))
            {
                directive.presentation = WorkspaceToolState::Presentation::docked;
            }
            else if (const auto floating = context.floatingBounds.find(binding.id);
                     floating != context.floatingBounds.end())
            {
                directive.presentation = WorkspaceToolState::Presentation::floating;
                directive.floatingBounds = floating->second;
            }
            if (const auto settings = snapshot.toolSettings.find(binding.id);
                settings != snapshot.toolSettings.end())
            {
                directive.settings = settings->second;
            }
            result.tools.push_back(std::move(directive));
        }

        if (snapshot.focusedTool.has_value())
        {
            const auto resolved = registry.resolve(*snapshot.focusedTool);
            if (resolved.has_value() && context.placed.contains(*resolved))
            {
                result.focusedTool = *resolved;
            }
        }
        return result;
    }

    [[nodiscard]] static bool
    execute(Plan plan, bool& automaticSaveEnabled, const Callbacks& callbacks)
    {
        if (!callbacks.detachPresentations || !callbacks.replaceLayout || !callbacks.prepareTool ||
            !callbacks.presentTool || !callbacks.rebuildPresentation || !callbacks.restoreFocus)
        {
            return false;
        }

        const ScopedSaveSuppression saveSuppression(automaticSaveEnabled);
        callbacks.detachPresentations();
        callbacks.replaceLayout(std::move(plan.layoutRoot));
        for (const auto& directive : plan.tools)
        {
            callbacks.prepareTool(directive);
        }
        for (const auto& directive : plan.tools)
        {
            callbacks.presentTool(directive);
        }
        callbacks.rebuildPresentation();
        callbacks.restoreFocus(plan.focusedTool);
        return true;
    }

  private:
    class ScopedSaveSuppression
    {
      public:
        explicit ScopedSaveSuppression(bool& saveEnabled) noexcept
            : flag(saveEnabled), previousValue(saveEnabled)
        {
            flag = false;
        }

        ~ScopedSaveSuppression()
        {
            flag = previousValue;
        }

      private:
        bool& flag;
        bool previousValue;
    };

    struct Context
    {
        const WorkspaceToolRegistry& registry;
        const std::vector<ToolBinding>& bindings;
        std::unordered_set<std::string> placed;
        std::unordered_set<std::string> docked;
        std::map<std::string, WorkspaceBounds> floatingBounds;
    };

    [[nodiscard]] static const ToolBinding*
    findBinding(const std::vector<ToolBinding>& bindings, const std::string& id)
    {
        const auto found = std::find_if(
            bindings.begin(), bindings.end(),
            [&id](const auto& binding) { return binding.id == id; });
        return found != bindings.end() ? &*found : nullptr;
    }

    [[nodiscard]] static std::unique_ptr<WorkspaceLayoutState::Node>
    convertNode(const WorkspaceNode& node, Context& context)
    {
        if (node.kind == WorkspaceNodeKind::leaf)
        {
            const auto resolved = context.registry.resolve(node.toolId);
            const auto* binding =
                resolved.has_value() ? findBinding(context.bindings, *resolved) : nullptr;
            if (binding == nullptr || !context.placed.insert(binding->id).second)
            {
                return nullptr;
            }
            context.docked.insert(binding->id);
            return WorkspaceLayoutState::makeLeafNode(binding->tool);
        }

        if (node.kind == WorkspaceNodeKind::tabs)
        {
            std::vector<WorkspaceLayoutState::Tool> tabs;
            for (const auto& id : node.tabs)
            {
                const auto resolved = context.registry.resolve(id);
                const auto* binding =
                    resolved.has_value() ? findBinding(context.bindings, *resolved) : nullptr;
                if (binding == nullptr || !context.placed.insert(binding->id).second)
                {
                    return nullptr;
                }
                context.docked.insert(binding->id);
                tabs.push_back(binding->tool);
            }

            const auto activeId = context.registry.resolve(node.activeTab);
            const auto* active =
                activeId.has_value() ? findBinding(context.bindings, *activeId) : nullptr;
            if (tabs.size() < 2 || active == nullptr ||
                std::find(tabs.begin(), tabs.end(), active->tool) == tabs.end())
            {
                return nullptr;
            }
            return WorkspaceLayoutState::makeTabsNode(std::move(tabs), active->tool);
        }

        if (node.first == nullptr || node.second == nullptr)
        {
            return nullptr;
        }
        auto first = convertNode(*node.first, context);
        auto second = convertNode(*node.second, context);
        if (first == nullptr || second == nullptr)
        {
            return nullptr;
        }
        return WorkspaceLayoutState::makeSplitNode(
            node.orientation == WorkspaceOrientation::horizontal
                ? WorkspaceLayoutState::Orientation::horizontal
                : WorkspaceLayoutState::Orientation::vertical,
            node.splitRatio, std::move(first), std::move(second));
    }
};
