#include "../../../MainComponent.h"

#include "../components/DockedToolPanel.h"

namespace
{
// Tool identity is encoded as an integer suffix so beginToolDrag/draggedTool
// never need a per-tool branch -- registering a new tool just works.
constexpr auto workspaceToolDragPrefix = "workspace-tool:";

[[nodiscard]] juce::Rectangle<int>
dropZoneBounds(WorkspaceLayoutState::DropZone zone, juce::Rectangle<int> workspace)
{
    constexpr int floatingTargetWidth = 170;
    constexpr int floatingTargetHeight = 64;
    if (zone == WorkspaceLayoutState::DropZone::floating)
    {
        return workspace.removeFromTop(floatingTargetHeight).removeFromRight(floatingTargetWidth);
    }

    const auto horizontalEdge = juce::jmax(100, workspace.getWidth() / 4);
    const auto verticalEdge = juce::jmax(80, workspace.getHeight() / 4);
    if (zone == WorkspaceLayoutState::DropZone::left)
    {
        return workspace.removeFromLeft(horizontalEdge);
    }
    if (zone == WorkspaceLayoutState::DropZone::right)
    {
        return workspace.removeFromRight(horizontalEdge);
    }

    workspace.removeFromLeft(horizontalEdge);
    workspace.removeFromRight(horizontalEdge);
    if (zone == WorkspaceLayoutState::DropZone::top)
    {
        return workspace.removeFromTop(verticalEdge);
    }
    if (zone == WorkspaceLayoutState::DropZone::bottom)
    {
        return workspace.removeFromBottom(verticalEdge);
    }
    if (zone == WorkspaceLayoutState::DropZone::centre)
    {
        workspace.removeFromTop(verticalEdge);
        workspace.removeFromBottom(verticalEdge);
        return workspace;
    }
    return {};
}

[[nodiscard]] juce::String dropZoneLabel(WorkspaceLayoutState::DropZone zone)
{
    switch (zone)
    {
    case WorkspaceLayoutState::DropZone::left:
        return "Tile left";
    case WorkspaceLayoutState::DropZone::right:
        return "Tile right";
    case WorkspaceLayoutState::DropZone::top:
        return "Tile top";
    case WorkspaceLayoutState::DropZone::bottom:
        return "Tile bottom";
    case WorkspaceLayoutState::DropZone::centre:
        return "Add as tab";
    case WorkspaceLayoutState::DropZone::floating:
        return "Float";
    case WorkspaceLayoutState::DropZone::none:
        return {};
    }
    return {};
}

void paintDropZone(
    juce::Graphics& graphics,
    juce::Rectangle<int> bounds,
    const juce::String& label,
    bool active,
    const AppPalette& palette)
{
    graphics.setColour(active ? palette.accent.withAlpha(0.72f) : palette.panel.withAlpha(0.88f));
    graphics.fillRoundedRectangle(bounds.toFloat(), 7.0f);
    graphics.setColour(active ? juce::Colours::white : palette.foreground);
    graphics.drawRoundedRectangle(bounds.toFloat(), 7.0f, 2.0f);
    graphics.setFont(juce::FontOptions(16.0f, juce::Font::bold));
    graphics.drawFittedText(label, bounds.reduced(8), juce::Justification::centred, 1);
}
} // namespace

void MainComponent::beginToolDrag(
    const ToolInstanceId& instance,
    juce::Component& source,
    const juce::ScaledImage& dragImage)
{
    activeDropTarget = {};
    // The drag payload carries the instance id itself rather than a numeric
    // handle, so it stays meaningful even if the workspace is rebuilt
    // mid-drag.
    const auto description = juce::String(workspaceToolDragPrefix) + juce::String(instance.value());
    startDragging(description, &source, dragImage, true);
}

std::optional<ToolInstanceId>
MainComponent::draggedTool(const juce::DragAndDropTarget::SourceDetails& details) const
{
    const auto description = details.description.toString();
    if (!description.startsWith(workspaceToolDragPrefix))
    {
        return std::nullopt;
    }

    const auto id =
        description.substring(juce::String(workspaceToolDragPrefix).length()).toStdString();
    const auto instance = ToolInstanceId(id);
    return findLiveTool(instance) != nullptr ? std::optional<ToolInstanceId>(instance)
                                             : std::nullopt;
}

// Hit-tests every currently docked tool's pane (however deeply nested inside
// split panes/tab groups it is) and returns whichever one contains
// `position` (in MainComponent's own coordinate space), if any.
std::optional<ToolInstanceId> MainComponent::dockedToolAt(juce::Point<int> position)
{
    for (auto& entry : liveTools)
    {
        auto& dock = entry.dock;
        if (dock == nullptr || dock->getParentComponent() == nullptr || !dock->isVisible())
        {
            continue;
        }
        const auto local = dock->getLocalPoint(this, position);
        if (dock->getLocalBounds().contains(local))
        {
            return entry.id;
        }
    }
    return std::nullopt;
}

MainComponent::DropTarget MainComponent::resolveDropTarget(juce::Point<int> position)
{
    const auto workspace = getLocalBounds().reduced(18);
    const auto workspaceLocal = position - workspace.getPosition();
    const auto workspaceZone = WorkspaceLayoutState::dropZoneForPosition(
        workspaceLocal.x, workspaceLocal.y, workspace.getWidth(), workspace.getHeight());

    // The floating target is a fixed workspace-corner control, independent
    // of whichever pane happens to be underneath it.
    if (workspaceZone == WorkspaceLayoutState::DropZone::floating)
    {
        return {WorkspaceLayoutState::DropZone::floating, std::nullopt};
    }

    if (const auto pane = dockedToolAt(position); pane.has_value())
    {
        auto* entry = findLiveTool(*pane);
        auto* dock = entry != nullptr ? entry->dock.get() : nullptr;
        if (dock == nullptr)
        {
            return {workspaceZone, std::nullopt};
        }
        const auto local = dock->getLocalPoint(this, position);
        auto zone = WorkspaceLayoutState::dropZoneForPosition(
            local.x, local.y, dock->getWidth(), dock->getHeight());
        // Floating only means anything at the whole-workspace level (handled
        // above); a pane-relative "floating" result (e.g. a small pane's own
        // corner) just means "drop here", i.e. add as a tab.
        if (zone == WorkspaceLayoutState::DropZone::floating)
        {
            zone = WorkspaceLayoutState::DropZone::centre;
        }
        return {zone, pane};
    }

    // No pane under the pointer (empty workspace, or hovering a divider) --
    // fall back to whole-workspace edge detection so the very first tool can
    // still be docked via an edge/centre gesture.
    return {workspaceZone, std::nullopt};
}

bool MainComponent::isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails& details)
{
    return draggedTool(details).has_value();
}

void MainComponent::itemDragEnter(const juce::DragAndDropTarget::SourceDetails& details)
{
    activeDropTarget = resolveDropTarget(details.localPosition);
    repaint();
}

void MainComponent::itemDragMove(const juce::DragAndDropTarget::SourceDetails& details)
{
    const auto target = resolveDropTarget(details.localPosition);
    if (target.zone != activeDropTarget.zone || target.pane != activeDropTarget.pane)
    {
        activeDropTarget = target;
        repaint();
    }
}

void MainComponent::itemDragExit(const juce::DragAndDropTarget::SourceDetails&)
{
    activeDropTarget = {};
    repaint();
}

void MainComponent::itemDropped(const juce::DragAndDropTarget::SourceDetails& details)
{
    const auto tool = draggedTool(details);
    const auto target = resolveDropTarget(details.localPosition);
    activeDropTarget = {};
    repaint();
    if (!tool.has_value() || target.zone == WorkspaceLayoutState::DropZone::none)
    {
        return;
    }

    const auto dragged = *tool;
    auto* draggedEntry = findLiveTool(dragged);
    if (draggedEntry == nullptr)
    {
        return;
    }
    const auto draggedHandle = draggedEntry->handle;

    if (target.zone == WorkspaceLayoutState::DropZone::floating)
    {
        workspaceLayoutState.remove(draggedHandle);
        presentTool(dragged, WorkspaceToolState::Presentation::floating);
        rebuildWorkspaceContainer();
        return;
    }

    if (draggedEntry->state.presentation() != WorkspaceToolState::Presentation::docked)
    {
        presentTool(dragged, WorkspaceToolState::Presentation::docked);
    }

    auto paneHandle = std::optional<WorkspaceLayoutState::Tool>{};
    if (target.pane.has_value())
    {
        if (const auto* paneEntry = findLiveTool(*target.pane))
        {
            paneHandle = paneEntry->handle;
        }
    }
    workspaceLayoutState.insert(draggedHandle, paneHandle, target.zone);

    currentTool = dragged;
    focusTool(dragged);
    rebuildWorkspaceContainer();
}

void MainComponent::paintOverChildren(juce::Graphics& graphics)
{
    // Nothing is being dragged over the workspace right now -- draw none of
    // the drop-zone indicators (including the floating target) at all.
    if (activeDropTarget.zone == WorkspaceLayoutState::DropZone::none)
    {
        return;
    }

    const auto palette = appPaletteFor(currentTheme);
    const auto workspace = getLocalBounds().reduced(18);

    // The floating target is shown workspace-relative for the whole
    // duration of a drag, regardless of whether a pane is currently hovered
    // underneath it.
    paintDropZone(
        graphics, dropZoneBounds(WorkspaceLayoutState::DropZone::floating, workspace).reduced(5),
        dropZoneLabel(WorkspaceLayoutState::DropZone::floating),
        activeDropTarget.zone == WorkspaceLayoutState::DropZone::floating, palette);

    if (activeDropTarget.zone == WorkspaceLayoutState::DropZone::floating)
    {
        return;
    }

    auto paneBounds = workspace;
    if (activeDropTarget.pane.has_value())
    {
        if (auto* entry = findLiveTool(*activeDropTarget.pane);
            entry != nullptr && entry->dock != nullptr)
        {
            paneBounds = getLocalArea(entry->dock.get(), entry->dock->getLocalBounds());
        }
    }

    constexpr WorkspaceLayoutState::DropZone tilingZones[]{
        WorkspaceLayoutState::DropZone::left, WorkspaceLayoutState::DropZone::right,
        WorkspaceLayoutState::DropZone::top, WorkspaceLayoutState::DropZone::bottom,
        WorkspaceLayoutState::DropZone::centre};
    for (const auto zone : tilingZones)
    {
        paintDropZone(
            graphics, dropZoneBounds(zone, paneBounds).reduced(5), dropZoneLabel(zone),
            zone == activeDropTarget.zone, palette);
    }
}
