#pragma once

#include "WorkspaceBuiltIns.h"
#include "WorkspaceDocuments.h"
#include "application/tools/BuiltInToolCatalog.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

struct WorkspaceNormalizationReport
{
    bool usedFallback = false;
    bool exceededDepthLimit = false;
    bool exceededCollectionLimit = false;
};

struct WorkspaceNormalizationResult
{
    WorkspaceSnapshot snapshot;
    WorkspaceNormalizationReport report;
};

class WorkspaceNormalizer
{
  public:
    static constexpr std::size_t maximumDockDepth = 32;
    static constexpr std::size_t maximumTabs = 64;
    static constexpr std::size_t maximumFloatingTools = 64;
    static constexpr std::size_t maximumToolSettings = 64;

    [[nodiscard]] static WorkspaceNormalizationResult normalize(
        const WorkspaceSnapshot& source,
        const std::vector<WorkspaceBounds>& displayAreas,
        const ToolCatalog& registry = builtInToolCatalog())
    {
        Context context{registry, displayAreas, {}, {}, {}};
        WorkspaceSnapshot normalized;

        if (source.dockRoot.has_value())
        {
            normalized.dockRoot = normalizeNode(*source.dockRoot, 1, context);
        }

        if (source.floating.size() > maximumFloatingTools)
        {
            context.report.exceededCollectionLimit = true;
        }
        const auto floatingCount = std::min(source.floating.size(), maximumFloatingTools);
        for (std::size_t index = 0; index < floatingCount; ++index)
        {
            const auto& entry = source.floating[index];
            const auto resolved = placeableInstance(entry.toolId, context);
            if (!resolved.has_value() || !context.placed.insert(*resolved).second)
            {
                continue;
            }

            normalized.floating.push_back({*resolved, constrainBounds(entry.bounds, displayAreas)});
            context.visibleOrder.push_back(*resolved);
        }

        if (source.toolSettings.size() > maximumToolSettings)
        {
            context.report.exceededCollectionLimit = true;
        }
        std::size_t settingsCount = 0;
        for (const auto& [id, payload] : source.toolSettings)
        {
            if (settingsCount++ >= maximumToolSettings)
            {
                break;
            }
            const auto resolved = placeableInstance(id, context);
            if (!resolved.has_value())
            {
                continue;
            }
            const auto* definition = registry.findForInstance(ToolInstanceId(*resolved));
            if (definition != nullptr && definition->settingsVersion == payload.version)
            {
                normalized.toolSettings.emplace(*resolved, payload);
            }
        }

        const auto resolvedFocus =
            source.focusedTool.has_value() ? placeableInstance(*source.focusedTool, context)
                                           : std::nullopt;
        if (resolvedFocus.has_value() && context.placed.contains(*resolvedFocus))
        {
            normalized.focusedTool = *resolvedFocus;
        }
        else if (!context.visibleOrder.empty())
        {
            normalized.focusedTool = context.visibleOrder.front();
        }

        if (context.visibleOrder.empty())
        {
            normalized = pitchPracticeFallback();
            context.report.usedFallback = true;
        }

        return {std::move(normalized), context.report};
    }

  private:
    struct Context
    {
        const ToolCatalog& registry;
        const std::vector<WorkspaceBounds>& displayAreas;
        std::unordered_set<std::string> placed;
        std::vector<std::string> visibleOrder;
        WorkspaceNormalizationReport report;
    };

    [[nodiscard]] static std::optional<WorkspaceNode>
    normalizeNode(const WorkspaceNode& source, std::size_t depth, Context& context)
    {
        if (depth > maximumDockDepth)
        {
            context.report.exceededDepthLimit = true;
            return std::nullopt;
        }

        if (source.kind == WorkspaceNodeKind::leaf)
        {
            return normalizeLeaf(source.toolId, context);
        }

        if (source.kind == WorkspaceNodeKind::tabs)
        {
            if (source.tabs.size() > maximumTabs)
            {
                context.report.exceededCollectionLimit = true;
            }

            std::vector<std::string> tabs;
            const auto tabCount = std::min(source.tabs.size(), maximumTabs);
            for (std::size_t index = 0; index < tabCount; ++index)
            {
                const auto resolved = placeableInstance(source.tabs[index], context);
                if (resolved.has_value() && context.placed.insert(*resolved).second)
                {
                    tabs.push_back(*resolved);
                    context.visibleOrder.push_back(*resolved);
                }
            }

            if (tabs.empty())
            {
                return std::nullopt;
            }
            if (tabs.size() == 1)
            {
                return WorkspaceNode::leaf(tabs.front());
            }

            const auto resolvedActive = placeableInstance(source.activeTab, context);
            const auto active =
                resolvedActive.has_value() &&
                        std::find(tabs.begin(), tabs.end(), *resolvedActive) != tabs.end()
                    ? *resolvedActive
                    : tabs.front();
            return WorkspaceNode::tabGroup(std::move(tabs), active);
        }

        auto first = source.first != nullptr ? normalizeNode(*source.first, depth + 1, context)
                                             : std::nullopt;
        auto second = source.second != nullptr ? normalizeNode(*source.second, depth + 1, context)
                                               : std::nullopt;
        if (!first.has_value())
        {
            return second;
        }
        if (!second.has_value())
        {
            return first;
        }

        const auto ratio =
            std::isfinite(source.splitRatio) ? std::clamp(source.splitRatio, 0.1, 0.9) : 0.5;
        return WorkspaceNode::split(
            source.orientation, ratio, std::move(*first), std::move(*second));
    }

    // Canonicalises a saved id into the instance id it should be placed under,
    // or nullopt when it must be dropped.
    //
    // Deduplication throughout this class keys on the *instance* id, not the
    // tool id. Today the two are the same string for every tool, so this
    // behaves exactly as keying on the tool did -- a snapshot naming "tuner"
    // twice still collapses to one. Keying on the instance now is what lets
    // multi-instance be enabled later without revisiting this class.
    //
    // The single-instance policy is enforced here rather than by the dedup:
    // an id like "tuner#2" is a distinct instance and survives dedup, so it
    // has to be dropped explicitly while the tool that declared it is
    // single-instance.
    [[nodiscard]] static std::optional<std::string>
    placeableInstance(const std::string& id, Context& context)
    {
        const auto resolved = context.registry.resolveInstance(ToolInstanceId(id));
        if (!resolved.has_value())
        {
            return std::nullopt;
        }

        const auto* definition = context.registry.findForInstance(*resolved);
        if (definition == nullptr)
        {
            return std::nullopt;
        }
        if (definition->instancePolicy == ToolInstancePolicy::single &&
            resolved->ordinal().value_or(1) > 1)
        {
            return std::nullopt;
        }
        return resolved->value();
    }

    [[nodiscard]] static std::optional<WorkspaceNode>
    normalizeLeaf(const std::string& id, Context& context)
    {
        const auto resolved = placeableInstance(id, context);
        if (!resolved.has_value() || !context.placed.insert(*resolved).second)
        {
            return std::nullopt;
        }
        context.visibleOrder.push_back(*resolved);
        return WorkspaceNode::leaf(*resolved);
    }

    [[nodiscard]] static WorkspaceBounds
    constrainBounds(WorkspaceBounds bounds, const std::vector<WorkspaceBounds>& displayAreas)
    {
        if (displayAreas.empty())
        {
            return bounds;
        }

        const auto intersectionArea = [&bounds](const WorkspaceBounds& area)
        {
            const auto left = std::max(bounds.x, area.x);
            const auto top = std::max(bounds.y, area.y);
            const auto right = std::min(bounds.x + bounds.width, area.x + area.width);
            const auto bottom = std::min(bounds.y + bounds.height, area.y + area.height);
            return std::max(0, right - left) * std::max(0, bottom - top);
        };

        const auto target = std::max_element(
            displayAreas.begin(), displayAreas.end(),
            [&intersectionArea](const auto& left, const auto& right)
            { return intersectionArea(left) < intersectionArea(right); });

        bounds.width = std::clamp(bounds.width, 1, std::max(1, target->width));
        bounds.height = std::clamp(bounds.height, 1, std::max(1, target->height));
        bounds.x = std::clamp(bounds.x, target->x, target->x + target->width - bounds.width);
        bounds.y = std::clamp(bounds.y, target->y, target->y + target->height - bounds.height);
        return bounds;
    }

    [[nodiscard]] static WorkspaceSnapshot pitchPracticeFallback()
    {
        return WorkspaceBuiltIns::pitchPractice().snapshot;
    }
};