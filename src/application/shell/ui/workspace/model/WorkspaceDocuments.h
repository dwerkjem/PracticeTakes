#pragma once

#include "application/tools/ToolSettingsPayload.h"

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

enum class WorkspaceNodeKind
{
    leaf,
    tabs,
    split
};

enum class WorkspaceOrientation
{
    horizontal,
    vertical
};

enum class WorkspaceToolPresentation
{
    closed,
    docked,
    floating
};

struct WorkspaceBounds
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    bool operator==(const WorkspaceBounds&) const = default;
};

struct WorkspaceNode
{
    WorkspaceNodeKind kind = WorkspaceNodeKind::leaf;

    std::string toolId;

    std::vector<std::string> tabs;
    std::string activeTab;

    WorkspaceOrientation orientation = WorkspaceOrientation::horizontal;
    double splitRatio = 0.5;
    std::unique_ptr<WorkspaceNode> first;
    std::unique_ptr<WorkspaceNode> second;

    WorkspaceNode() = default;

    WorkspaceNode(const WorkspaceNode& other)
        : kind(other.kind), toolId(other.toolId), tabs(other.tabs), activeTab(other.activeTab),
          orientation(other.orientation), splitRatio(other.splitRatio),
          first(other.first != nullptr ? std::make_unique<WorkspaceNode>(*other.first) : nullptr),
          second(other.second != nullptr ? std::make_unique<WorkspaceNode>(*other.second) : nullptr)
    {
    }

    WorkspaceNode& operator=(const WorkspaceNode& other)
    {
        if (this != &other)
        {
            WorkspaceNode copy(other);
            *this = std::move(copy);
        }
        return *this;
    }

    WorkspaceNode(WorkspaceNode&&) noexcept = default;
    WorkspaceNode& operator=(WorkspaceNode&&) noexcept = default;

    [[nodiscard]] static WorkspaceNode leaf(std::string id)
    {
        WorkspaceNode node;
        node.kind = WorkspaceNodeKind::leaf;
        node.toolId = std::move(id);
        return node;
    }

    [[nodiscard]] static WorkspaceNode
    tabGroup(std::vector<std::string> toolIds, std::string selectedToolId)
    {
        WorkspaceNode node;
        node.kind = WorkspaceNodeKind::tabs;
        node.tabs = std::move(toolIds);
        node.activeTab = std::move(selectedToolId);
        return node;
    }

    [[nodiscard]] static WorkspaceNode split(
        WorkspaceOrientation splitOrientation,
        double ratio,
        WorkspaceNode firstChild,
        WorkspaceNode secondChild)
    {
        WorkspaceNode node;
        node.kind = WorkspaceNodeKind::split;
        node.orientation = splitOrientation;
        node.splitRatio = ratio;
        node.first = std::make_unique<WorkspaceNode>(std::move(firstChild));
        node.second = std::make_unique<WorkspaceNode>(std::move(secondChild));
        return node;
    }

    bool operator==(const WorkspaceNode& other) const
    {
        const auto childrenEqual = [](const auto& left, const auto& right)
        {
            if (left == nullptr || right == nullptr)
            {
                return left == nullptr && right == nullptr;
            }
            return *left == *right;
        };

        return kind == other.kind && toolId == other.toolId && tabs == other.tabs &&
               activeTab == other.activeTab && orientation == other.orientation &&
               splitRatio == other.splitRatio && childrenEqual(first, other.first) &&
               childrenEqual(second, other.second);
    }
};

struct FloatingWorkspaceTool
{
    std::string toolId;
    WorkspaceBounds bounds;

    bool operator==(const FloatingWorkspaceTool&) const = default;
};

struct WorkspaceSnapshot
{
    std::optional<WorkspaceNode> dockRoot;
    std::vector<FloatingWorkspaceTool> floating;
    std::optional<std::string> focusedTool;
    std::map<std::string, ToolSettingsPayload> toolSettings;

    [[nodiscard]] WorkspaceToolPresentation presentationOf(const std::string& toolId) const
    {
        if (dockRoot.has_value() && contains(*dockRoot, toolId))
        {
            return WorkspaceToolPresentation::docked;
        }
        const auto floatingTool = std::find_if(
            floating.begin(), floating.end(),
            [&toolId](const auto& entry) { return entry.toolId == toolId; });
        return floatingTool != floating.end() ? WorkspaceToolPresentation::floating
                                              : WorkspaceToolPresentation::closed;
    }

    bool operator==(const WorkspaceSnapshot&) const = default;

  private:
    [[nodiscard]] static bool contains(const WorkspaceNode& node, const std::string& toolId)
    {
        if (node.kind == WorkspaceNodeKind::leaf)
        {
            return node.toolId == toolId;
        }
        if (node.kind == WorkspaceNodeKind::tabs)
        {
            return std::find(node.tabs.begin(), node.tabs.end(), toolId) != node.tabs.end();
        }
        return (node.first != nullptr && contains(*node.first, toolId)) ||
               (node.second != nullptr && contains(*node.second, toolId));
    }
};

struct NamedWorkspace
{
    std::string id;
    std::string name;
    WorkspaceSnapshot snapshot;

    bool operator==(const NamedWorkspace&) const = default;
};

struct WorkspaceCatalog
{
    static constexpr int currentVersion = 1;

    int version = currentVersion;
    WorkspaceSnapshot active;
    std::optional<std::string> activeSource;
    std::vector<NamedWorkspace> named;

    bool operator==(const WorkspaceCatalog&) const = default;
};