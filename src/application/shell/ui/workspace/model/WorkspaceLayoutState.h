#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

// Pure logic (no JUCE dependency) for the workspace's tiling arrangement.
//
// Tools are tiled the way a "fractal"/BSP tiling window manager (bspwm, i3,
// ...) tiles windows: the workspace is a binary tree. Every leaf is either a
// single tool or a group of tools sharing a tab strip; every internal node is
// a two-way split (horizontal or vertical). Dropping a tool onto an edge of
// an existing pane splits *that pane* in two, so panes keep subdividing
// indefinitely as more tools are added -- there is no limit on how many tools
// can be tiled at once and no special-casing of specific tools/counts.
class WorkspaceLayoutState
{
  public:
    // Opaque identifier for a workspace tool. This engine has no notion of a
    // fixed roster of tools -- callers map their own tool enumeration onto
    // these ids (the shell assigns one per live tool instance).
    enum class Tool : int
    {
    };

    enum class Orientation
    {
        horizontal,
        vertical
    };

    enum class DropZone
    {
        none,
        left,
        right,
        top,
        bottom,
        centre,
        floating
    };

    // A command for the "Arrange workspace" menu. Unlike DropZone/insert,
    // this doesn't target a specific pane -- it acts on the root of the tree.
    enum class Layout
    {
        horizontal,
        vertical,
        tabbed
    };

    enum class NodeKind
    {
        leaf,
        tabs,
        split
    };

    using NodeId = std::uint64_t;

    // Read-only description of one tree node. Treat this as an observation
    // window into the tree: mutate the tree only via WorkspaceLayoutState's
    // own methods, never by poking these fields directly.
    struct Node
    {
        NodeId id = 0;
        NodeKind kind = NodeKind::leaf;

        // NodeKind::leaf
        Tool tool{};

        // NodeKind::tabs
        std::vector<Tool> tabs;
        Tool activeTab{};

        // NodeKind::split
        Orientation orientation = Orientation::horizontal;
        double splitRatio = 0.5;
        std::unique_ptr<Node> first;
        std::unique_ptr<Node> second;
    };

    [[nodiscard]] static std::unique_ptr<Node> makeLeafNode(Tool tool)
    {
        auto node = std::make_unique<Node>();
        node->kind = NodeKind::leaf;
        node->tool = tool;
        return node;
    }

    [[nodiscard]] static std::unique_ptr<Node> makeTabsNode(std::vector<Tool> tabs, Tool activeTab)
    {
        auto node = std::make_unique<Node>();
        node->kind = NodeKind::tabs;
        node->tabs = std::move(tabs);
        node->activeTab = activeTab;
        return node;
    }

    [[nodiscard]] static std::unique_ptr<Node> makeSplitNode(
        Orientation orientation,
        double ratio,
        std::unique_ptr<Node> first,
        std::unique_ptr<Node> second)
    {
        auto node = std::make_unique<Node>();
        node->kind = NodeKind::split;
        node->orientation = orientation;
        node->splitRatio = ratio;
        node->first = std::move(first);
        node->second = std::move(second);
        return node;
    }

    [[nodiscard]] static DropZone dropZoneForPosition(int x, int y, int width, int height) noexcept
    {
        if (x < 0 || y < 0 || x >= width || y >= height || width <= 0 || height <= 0)
        {
            return DropZone::none;
        }

        constexpr int floatingTargetWidth = 170;
        constexpr int floatingTargetHeight = 64;
        if (x >= std::max(0, width - floatingTargetWidth) && y < floatingTargetHeight)
        {
            return DropZone::floating;
        }

        const auto horizontalEdge = std::max(100, width / 4);
        const auto verticalEdge = std::max(80, height / 4);
        if (x < horizontalEdge)
        {
            return DropZone::left;
        }
        if (x >= width - horizontalEdge)
        {
            return DropZone::right;
        }
        if (y < verticalEdge)
        {
            return DropZone::top;
        }
        if (y >= height - verticalEdge)
        {
            return DropZone::bottom;
        }
        return DropZone::centre;
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return root == nullptr;
    }

    [[nodiscard]] bool contains(Tool tool) const noexcept
    {
        return find(root.get(), tool) != nullptr;
    }

    [[nodiscard]] const Node* rootNode() const noexcept
    {
        return root.get();
    }

    [[nodiscard]] bool replaceRoot(std::unique_ptr<Node> replacement) noexcept
    {
        std::vector<Tool> seenTools;
        if (!validateNode(replacement.get(), seenTools))
        {
            return false;
        }

        assignFreshIds(replacement.get());
        root = std::move(replacement);
        return true;
    }

    [[nodiscard]] bool setSplitRatio(NodeId id, double ratio) noexcept
    {
        auto* node = findById(root.get(), id);
        if (node == nullptr || node->kind != NodeKind::split || !std::isfinite(ratio))
        {
            return false;
        }
        node->splitRatio = std::clamp(ratio, 0.1, 0.9);
        return true;
    }

    [[nodiscard]] bool setActiveTab(NodeId id, Tool tool) noexcept
    {
        auto* node = findById(root.get(), id);
        if (node == nullptr || node->kind != NodeKind::tabs ||
            std::find(node->tabs.begin(), node->tabs.end(), tool) == node->tabs.end())
        {
            return false;
        }
        node->activeTab = tool;
        return true;
    }

    // Removes every tool from the tree.
    void clear() noexcept
    {
        root.reset();
    }

    // Places `tool` into the tree, splitting or tabbing with `target`'s pane:
    //  - If the tree is empty, or `target` is not supplied, `tool` becomes
    //    (or replaces) the sole root pane, regardless of `zone`.
    //  - `zone == centre` merges `tool` into target's tab group (creating one
    //    if target was a single pane).
    //  - Any other zone (left/right/top/bottom) splits target's pane in half
    //    in that direction, with `tool` on the requested side. Since this
    //    only ever touches the single pane being targeted, repeating this on
    //    different panes subdivides the workspace fractally to any depth.
    //  - `zone == none`/`floating` are not placement requests and are a
    //    no-op here; callers should handle those cases themselves (do
    //    nothing, or float the tool in its own window).
    // If `tool` is already present elsewhere in the tree, it is moved rather
    // than duplicated.
    void insert(Tool tool, std::optional<Tool> target, DropZone zone) noexcept
    {
        if (zone == DropZone::none || zone == DropZone::floating)
        {
            return;
        }
        if (target.has_value() && *target == tool)
        {
            return;
        }

        remove(tool);

        if (root == nullptr)
        {
            root = std::make_unique<Node>();
            root->id = allocateNodeId();
            root->kind = NodeKind::leaf;
            root->tool = tool;
            return;
        }

        // Fall back to the tree's first pane (never an interior split node,
        // so it's always safe to tab/merge into) if no target was given, or
        // the given one couldn't be found. This must never fall back to
        // `root` itself when root is a split, or the merge below would
        // silently discard everything under the other branch.
        auto* targetNode = target.has_value() ? find(root.get(), *target) : nullptr;
        if (targetNode == nullptr)
        {
            targetNode = firstLeafOrTabs(root.get());
            zone = DropZone::centre;
        }

        if (zone == DropZone::centre)
        {
            if (targetNode->kind == NodeKind::tabs)
            {
                targetNode->tabs.push_back(tool);
            }
            else
            {
                const auto existing = targetNode->tool;
                targetNode->kind = NodeKind::tabs;
                targetNode->tabs = {existing, tool};
            }
            targetNode->activeTab = tool;
            return;
        }

        convertToSplit(*targetNode, tool, zone);
    }

    // Removes `tool` from the tree, collapsing the tree back up so no empty
    // splits or single-pane tab groups are left behind. A no-op if `tool`
    // isn't present.
    void remove(Tool tool) noexcept
    {
        removeRec(root, tool);
    }

    // "Arrange workspace" menu commands. These act on the root of the tree
    // rather than a specific pane.
    void applyLayoutCommand(Layout command) noexcept
    {
        if (command == Layout::tabbed)
        {
            flattenToTabs();
            return;
        }
        if (root != nullptr && root->kind == NodeKind::split)
        {
            root->orientation =
                command == Layout::vertical ? Orientation::vertical : Orientation::horizontal;
        }
    }

    [[nodiscard]] bool canSplitRoot() const noexcept
    {
        return root != nullptr && root->kind == NodeKind::split;
    }

    [[nodiscard]] bool isRootOrientation(Orientation orientation) const noexcept
    {
        return canSplitRoot() && root->orientation == orientation;
    }

    [[nodiscard]] bool isFlattenedTabs() const noexcept
    {
        return root != nullptr && root->kind == NodeKind::tabs;
    }

  private:
    [[nodiscard]] NodeId allocateNodeId() noexcept
    {
        return nextNodeId++;
    }

    void assignFreshIds(Node* node) noexcept
    {
        if (node == nullptr)
        {
            return;
        }
        node->id = allocateNodeId();
        assignFreshIds(node->first.get());
        assignFreshIds(node->second.get());
    }

    [[nodiscard]] static bool validateNode(const Node* node, std::vector<Tool>& seenTools) noexcept
    {
        if (node == nullptr)
        {
            return true;
        }

        const auto addTool = [&seenTools](Tool tool)
        {
            if (std::find(seenTools.begin(), seenTools.end(), tool) != seenTools.end())
            {
                return false;
            }
            seenTools.push_back(tool);
            return true;
        };

        if (node->kind == NodeKind::leaf)
        {
            return addTool(node->tool);
        }
        if (node->kind == NodeKind::tabs)
        {
            if (node->tabs.size() < 2 ||
                std::find(node->tabs.begin(), node->tabs.end(), node->activeTab) ==
                    node->tabs.end())
            {
                return false;
            }
            for (const auto tool : node->tabs)
            {
                if (!addTool(tool))
                {
                    return false;
                }
            }
            return true;
        }
        return node->first != nullptr && node->second != nullptr &&
               std::isfinite(node->splitRatio) && node->splitRatio >= 0.1 &&
               node->splitRatio <= 0.9 && validateNode(node->first.get(), seenTools) &&
               validateNode(node->second.get(), seenTools);
    }

    [[nodiscard]] static Node* findById(Node* node, NodeId id) noexcept
    {
        if (node == nullptr)
        {
            return nullptr;
        }
        if (node->id == id)
        {
            return node;
        }
        if (auto* hit = findById(node->first.get(), id))
        {
            return hit;
        }
        return findById(node->second.get(), id);
    }

    [[nodiscard]] static Node* firstLeafOrTabs(Node* node) noexcept
    {
        while (node->kind == NodeKind::split)
        {
            node = node->first.get();
        }
        return node;
    }

    [[nodiscard]] static Node* find(Node* node, Tool tool) noexcept
    {
        if (node == nullptr)
        {
            return nullptr;
        }
        if (node->kind == NodeKind::leaf)
        {
            return node->tool == tool ? node : nullptr;
        }
        if (node->kind == NodeKind::tabs)
        {
            const auto found = std::find(node->tabs.begin(), node->tabs.end(), tool);
            return found != node->tabs.end() ? node : nullptr;
        }
        if (auto* hit = find(node->first.get(), tool))
        {
            return hit;
        }
        return find(node->second.get(), tool);
    }

    void convertToSplit(Node& node, Tool newTool, DropZone zone) noexcept
    {
        const auto splitId = node.id;
        auto oldContent = std::make_unique<Node>(std::move(node));
        oldContent->id = allocateNodeId();
        auto newLeaf = std::make_unique<Node>();
        newLeaf->id = allocateNodeId();
        newLeaf->kind = NodeKind::leaf;
        newLeaf->tool = newTool;

        node = Node{};
        node.id = splitId;
        node.kind = NodeKind::split;
        node.orientation = (zone == DropZone::top || zone == DropZone::bottom)
                               ? Orientation::vertical
                               : Orientation::horizontal;
        const auto newToolIsFirst = (zone == DropZone::left || zone == DropZone::top);
        node.first = newToolIsFirst ? std::move(newLeaf) : std::move(oldContent);
        node.second = newToolIsFirst ? std::move(oldContent) : std::move(newLeaf);
    }

    enum class RemoveResult
    {
        notFound,
        removedNodeShrunk,
        removedNodeVanished
    };

    static RemoveResult removeRec(std::unique_ptr<Node>& node, Tool tool) noexcept
    {
        if (!node)
        {
            return RemoveResult::notFound;
        }

        if (node->kind == NodeKind::leaf)
        {
            if (node->tool != tool)
            {
                return RemoveResult::notFound;
            }
            node.reset();
            return RemoveResult::removedNodeVanished;
        }

        if (node->kind == NodeKind::tabs)
        {
            const auto found = std::find(node->tabs.begin(), node->tabs.end(), tool);
            if (found == node->tabs.end())
            {
                return RemoveResult::notFound;
            }
            node->tabs.erase(found);
            if (node->tabs.empty())
            {
                node.reset();
                return RemoveResult::removedNodeVanished;
            }
            if (node->tabs.size() == 1)
            {
                const auto remaining = node->tabs.front();
                node->kind = NodeKind::leaf;
                node->tool = remaining;
                node->tabs.clear();
            }
            else if (node->activeTab == tool)
            {
                node->activeTab = node->tabs.front();
            }
            return RemoveResult::removedNodeShrunk;
        }

        const auto firstResult = removeRec(node->first, tool);
        if (firstResult == RemoveResult::removedNodeVanished)
        {
            node = std::move(node->second);
            return RemoveResult::removedNodeShrunk;
        }
        if (firstResult != RemoveResult::notFound)
        {
            return RemoveResult::removedNodeShrunk;
        }

        const auto secondResult = removeRec(node->second, tool);
        if (secondResult == RemoveResult::removedNodeVanished)
        {
            node = std::move(node->first);
            return RemoveResult::removedNodeShrunk;
        }
        if (secondResult != RemoveResult::notFound)
        {
            return RemoveResult::removedNodeShrunk;
        }

        return RemoveResult::notFound;
    }

    static void collectLeaves(const Node* node, std::vector<Tool>& out) noexcept
    {
        if (node == nullptr)
        {
            return;
        }
        if (node->kind == NodeKind::leaf)
        {
            out.push_back(node->tool);
            return;
        }
        if (node->kind == NodeKind::tabs)
        {
            for (const auto tool : node->tabs)
            {
                out.push_back(tool);
            }
            return;
        }
        collectLeaves(node->first.get(), out);
        collectLeaves(node->second.get(), out);
    }

    void flattenToTabs() noexcept
    {
        if (root == nullptr)
        {
            return;
        }
        std::vector<Tool> allTools;
        collectLeaves(root.get(), allTools);
        if (allTools.size() <= 1)
        {
            return;
        }

        auto tabsNode = std::make_unique<Node>();
        tabsNode->id = allocateNodeId();
        tabsNode->kind = NodeKind::tabs;
        tabsNode->activeTab = allTools.front();
        tabsNode->tabs = std::move(allTools);
        root = std::move(tabsNode);
    }

    std::unique_ptr<Node> root;
    NodeId nextNodeId = 1;
};
