#pragma once

#include <algorithm>

class WorkspaceLayoutState
{
  public:
    // Opaque identifier for a workspace tool. This engine has no notion of a
    // fixed roster of tools or how many exist -- callers map their own tool
    // enumeration onto these ids (e.g. static_cast<Tool>(ToolType::tuner)),
    // which is what lets the same two-pane split/tab logic keep working as
    // more tools are added.
    enum class Tool : int
    {
    };

    enum class Layout
    {
        horizontal,
        vertical,
        tabbed
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

    enum class Destination
    {
        unchanged,
        docked,
        floating
    };

    struct DropResult
    {
        Destination destination = Destination::unchanged;
        bool layoutChanged = false;
    };

    [[nodiscard]] static DropZone dropZoneForPosition(int x, int y, int width, int height) noexcept
    {
        if (x < 0 || y < 0 || x >= width || y >= height || width <= 0 || height <= 0)
            return DropZone::none;

        constexpr int floatingTargetWidth = 170;
        constexpr int floatingTargetHeight = 64;
        if (x >= std::max(0, width - floatingTargetWidth) && y < floatingTargetHeight)
            return DropZone::floating;

        const auto horizontalEdge = std::max(100, width / 4);
        const auto verticalEdge = std::max(80, height / 4);
        if (x < horizontalEdge)
            return DropZone::left;
        if (x >= width - horizontalEdge)
            return DropZone::right;
        if (y < verticalEdge)
            return DropZone::top;
        if (y >= height - verticalEdge)
            return DropZone::bottom;
        return DropZone::centre;
    }

    // draggedTool is the tool being dropped. pairedTool is whichever other
    // tool would share a tile with it if a tile is requested; the caller
    // decides who that is (e.g. the other currently docked tool) and reports
    // whether it is actually docked via pairedToolIsDocked. When it isn't,
    // there is nothing to tile against, so the drop simply docks/activates
    // the dragged tool without changing the stored split layout.
    [[nodiscard]] DropResult
    applyDrop(Tool draggedTool, Tool pairedTool, DropZone zone, bool pairedToolIsDocked) noexcept
    {
        if (zone == DropZone::none)
            return {};
        if (zone == DropZone::floating)
            return {Destination::floating, false};
        if (!pairedToolIsDocked)
        {
            activeTool = draggedTool;
            return {Destination::docked, false};
        }

        const auto previousLayout = currentLayout;
        const auto previousFirst = firstTool;
        switch (zone)
        {
        case DropZone::left:
            currentLayout = Layout::horizontal;
            firstTool = draggedTool;
            break;
        case DropZone::right:
            currentLayout = Layout::horizontal;
            firstTool = pairedTool;
            break;
        case DropZone::top:
            currentLayout = Layout::vertical;
            firstTool = draggedTool;
            break;
        case DropZone::bottom:
            currentLayout = Layout::vertical;
            firstTool = pairedTool;
            break;
        case DropZone::centre:
            currentLayout = Layout::tabbed;
            activeTool = draggedTool;
            break;
        case DropZone::none:
        case DropZone::floating:
            break;
        }
        return {Destination::docked, previousLayout != currentLayout || previousFirst != firstTool};
    }

    void setLayout(Layout layout) noexcept
    {
        currentLayout = layout;
    }

    void setActiveTool(Tool tool) noexcept
    {
        activeTool = tool;
    }

    [[nodiscard]] Layout layout() const noexcept
    {
        return currentLayout;
    }

    [[nodiscard]] Tool first() const noexcept
    {
        return firstTool;
    }

    [[nodiscard]] Tool active() const noexcept
    {
        return activeTool;
    }

  private:
    Layout currentLayout = Layout::horizontal;
    Tool firstTool{};
    Tool activeTool{};
};
