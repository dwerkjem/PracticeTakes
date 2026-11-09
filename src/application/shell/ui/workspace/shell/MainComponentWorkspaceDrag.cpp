#include "../../../MainComponent.h"

#include "../components/DockedToolPanel.h"
#include "../components/WorkspaceTabbedComponent.h"

namespace
{
// Tool identity is encoded as an integer suffix so beginToolDrag/draggedTool
// never need a per-tool branch -- registering a new tool just works.
constexpr auto workspaceToolDragPrefix = "workspace-tool:";

[[nodiscard]] juce::Rectangle<int> dropZoneBounds(
    WorkspaceLayoutState::DropZone zone,
    juce::Rectangle<int> workspace,
    int textScale = appearance::TextScale::defaultPercent)
{
    const int floatingTargetWidth = appearance::scaledHeight(170, textScale);
    const int floatingTargetHeight = appearance::scaledHeight(64, textScale);
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
    const AppPalette& palette,
    int textScale)
{
    graphics.setColour(active ? palette.accent.withAlpha(0.72f) : palette.panel.withAlpha(0.88f));
    graphics.fillRoundedRectangle(bounds.toFloat(), 7.0f);
    graphics.setColour(active ? juce::Colours::white : palette.foreground);
    graphics.drawRoundedRectangle(bounds.toFloat(), 7.0f, 2.0f);
    graphics.setFont(
        appearance::textFont(appearance::TextRole::subheading, textScale, juce::Font::bold));
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

MainComponent::DropTarget
MainComponent::resolveDropTarget(juce::Point<int> position, std::optional<ToolInstanceId> dragged)
{
    const auto* draggedEntry = dragged.has_value() ? findLiveTool(*dragged) : nullptr;
    // Whether the dragged tool sits in a tab group with others: then dropping
    // it on itself tiles it out of the group rather than doing nothing (#355).
    const auto inTabGroup = [](const LiveTool* entry)
    {
        return entry != nullptr && entry->dock != nullptr &&
               entry->dock->findParentComponentOfClass<WorkspaceTabbedComponent>() != nullptr;
    };

    // Over a strip of tabs: join that group -- or, for one of its own tabs,
    // tile out of it. The strip is not inside any tool's pane, which is why a
    // tab dropped on its own strip used to land nowhere.
    for (auto& entry : liveTools)
    {
        auto* dock = entry.dock.get();
        if (dock == nullptr || dock->getParentComponent() == nullptr || !dock->isVisible())
        {
            continue;
        }
        auto* tabs = dock->findParentComponentOfClass<WorkspaceTabbedComponent>();
        if (tabs == nullptr || !tabs->tabStripContains(tabs->getLocalPoint(this, position)))
        {
            continue;
        }
        if (draggedEntry != nullptr && tabs->holds(draggedEntry->handle))
        {
            return {WorkspaceLayoutState::DropZone::centre, dragged, true};
        }
        if (const auto shown = tabs->shownTool(); shown.has_value() && *shown == entry.handle)
        {
            return {WorkspaceLayoutState::DropZone::centre, entry.id};
        }
    }

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
        return {zone, pane, dragged == pane && inTabGroup(draggedEntry)};
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
    activeDropTarget = resolveDropTarget(details.localPosition, draggedTool(details));
    repaint();
}

void MainComponent::itemDragMove(const juce::DragAndDropTarget::SourceDetails& details)
{
    const auto target = resolveDropTarget(details.localPosition, draggedTool(details));
    if (target.zone != activeDropTarget.zone || target.pane != activeDropTarget.pane ||
        target.tilesOut != activeDropTarget.tilesOut)
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
    const auto target = resolveDropTarget(details.localPosition, tool);
    activeDropTarget = {};
    repaint();
    if (!tool.has_value() || target.zone == WorkspaceLayoutState::DropZone::none)
    {
        return;
    }

    const auto& dragged = *tool;
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
    // Over the tools, beneath any drop targets: a muted microphone frames the
    // workspace (#114), and a drag in progress still reads on top of it.
    paintMutedFrame(graphics);

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
        graphics,
        dropZoneBounds(
            WorkspaceLayoutState::DropZone::floating, workspace, appLookAndFeel.textScale())
            .reduced(5),
        dropZoneLabel(WorkspaceLayoutState::DropZone::floating),
        activeDropTarget.zone == WorkspaceLayoutState::DropZone::floating, palette,
        appLookAndFeel.textScale());

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
        // Over its own tab, the centre does not add a tab: it tiles the tool
        // beside the tab to its left (#355).
        const auto label =
            activeDropTarget.tilesOut && zone == WorkspaceLayoutState::DropZone::centre
                ? juce::String("Tile beside")
                : dropZoneLabel(zone);
        paintDropZone(
            graphics, dropZoneBounds(zone, paneBounds).reduced(5), label,
            zone == activeDropTarget.zone, palette, appLookAndFeel.textScale());
    }
}
