#pragma once

#include "WorkspaceDocuments.h"
#include "WorkspaceLayoutState.h"
#include "WorkspaceToolState.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class WorkspaceSnapshotCapture
{
  public:
    struct ToolObservation
    {
        std::string id;
        WorkspaceLayoutState::Tool tool;
        WorkspaceToolState::Presentation presentation = WorkspaceToolState::Presentation::closed;
        WorkspaceBounds floatingBounds;
        std::optional<ToolSettingsPayload> settings;
    };

    [[nodiscard]] static WorkspaceSnapshot capture(
        const WorkspaceLayoutState& layout,
        const std::vector<ToolObservation>& tools,
        std::optional<std::string> focusedTool)
    {
        WorkspaceSnapshot snapshot;
        snapshot.dockRoot = captureNode(layout.rootNode(), tools);

        for (const auto& tool : tools)
        {
            if (tool.presentation == WorkspaceToolState::Presentation::floating)
            {
                snapshot.floating.push_back({tool.id, tool.floatingBounds});
            }
            if (tool.settings.has_value())
            {
                snapshot.toolSettings.insert_or_assign(tool.id, *tool.settings);
            }
        }

        if (focusedTool.has_value() &&
            snapshot.presentationOf(*focusedTool) != WorkspaceToolPresentation::closed)
        {
            snapshot.focusedTool = std::move(focusedTool);
        }
        return snapshot;
    }

  private:
    [[nodiscard]] static const ToolObservation*
    findTool(const std::vector<ToolObservation>& tools, WorkspaceLayoutState::Tool tool)
    {
        const auto found = std::find_if(
            tools.begin(), tools.end(),
            [tool](const auto& observation) { return observation.tool == tool; });
        return found != tools.end() ? &*found : nullptr;
    }

    [[nodiscard]] static std::optional<WorkspaceNode>
    captureNode(const WorkspaceLayoutState::Node* node, const std::vector<ToolObservation>& tools)
    {
        if (node == nullptr)
        {
            return std::nullopt;
        }
        if (node->kind == WorkspaceLayoutState::NodeKind::leaf)
        {
            const auto* tool = findTool(tools, node->tool);
            if (tool == nullptr || tool->presentation != WorkspaceToolState::Presentation::docked)
            {
                return std::nullopt;
            }
            return WorkspaceNode::leaf(tool->id);
        }
        if (node->kind == WorkspaceLayoutState::NodeKind::tabs)
        {
            std::vector<std::string> tabIds;
            std::optional<std::string> activeId;
            for (const auto tab : node->tabs)
            {
                const auto* tool = findTool(tools, tab);
                if (tool == nullptr ||
                    tool->presentation != WorkspaceToolState::Presentation::docked)
                {
                    continue;
                }
                tabIds.push_back(tool->id);
                if (tab == node->activeTab)
                {
                    activeId = tool->id;
                }
            }
            if (tabIds.empty())
            {
                return std::nullopt;
            }
            if (tabIds.size() == 1)
            {
                return WorkspaceNode::leaf(tabIds.front());
            }
            auto selectedId = activeId.value_or(tabIds.front());
            return WorkspaceNode::tabGroup(std::move(tabIds), std::move(selectedId));
        }

        auto first = captureNode(node->first.get(), tools);
        auto second = captureNode(node->second.get(), tools);
        if (!first.has_value())
        {
            return second;
        }
        if (!second.has_value())
        {
            return first;
        }
        return WorkspaceNode::split(
            node->orientation == WorkspaceLayoutState::Orientation::horizontal
                ? WorkspaceOrientation::horizontal
                : WorkspaceOrientation::vertical,
            node->splitRatio, std::move(*first), std::move(*second));
    }
};