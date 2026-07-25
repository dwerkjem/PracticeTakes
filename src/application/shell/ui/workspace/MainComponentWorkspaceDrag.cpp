#include "../../MainComponent.h"

namespace
{
constexpr auto tunerDragDescription = "workspace-tool:tuner";
constexpr auto spectrogramDragDescription = "workspace-tool:spectrogram";
constexpr auto harmonicDragDescription = "workspace-tool:harmonics";

[[nodiscard]] juce::Rectangle<int>
dropZoneBounds(WorkspaceLayoutState::DropZone zone, juce::Rectangle<int> workspace)
{
    constexpr int floatingTargetWidth = 170;
    constexpr int floatingTargetHeight = 64;
    if (zone == WorkspaceLayoutState::DropZone::floating)
        return workspace.removeFromTop(floatingTargetHeight).removeFromRight(floatingTargetWidth);

    const auto horizontalEdge = juce::jmax(100, workspace.getWidth() / 4);
    const auto verticalEdge = juce::jmax(80, workspace.getHeight() / 4);
    if (zone == WorkspaceLayoutState::DropZone::left)
        return workspace.removeFromLeft(horizontalEdge);
    if (zone == WorkspaceLayoutState::DropZone::right)
        return workspace.removeFromRight(horizontalEdge);

    workspace.removeFromLeft(horizontalEdge);
    workspace.removeFromRight(horizontalEdge);
    if (zone == WorkspaceLayoutState::DropZone::top)
        return workspace.removeFromTop(verticalEdge);
    if (zone == WorkspaceLayoutState::DropZone::bottom)
        return workspace.removeFromBottom(verticalEdge);
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
} // namespace

void MainComponent::beginToolDrag(ToolType tool, juce::Component& source)
{
    activeDropZone = WorkspaceLayoutState::DropZone::none;
    const auto description =
        tool == ToolType::tuner
            ? tunerDragDescription
            : (tool == ToolType::spectrogram ? spectrogramDragDescription
                                             : harmonicDragDescription);
    startDragging(description, &source, juce::ScaledImage(), true);
}

std::optional<MainComponent::ToolType>
MainComponent::draggedTool(const juce::DragAndDropTarget::SourceDetails& details) const
{
    const auto description = details.description.toString();
    if (description == tunerDragDescription)
        return ToolType::tuner;
    if (description == spectrogramDragDescription)
        return ToolType::spectrogram;
    if (description == harmonicDragDescription)
        return ToolType::harmonics;
    return std::nullopt;
}

WorkspaceLayoutState::DropZone MainComponent::dropZoneAt(juce::Point<int> position) const
{
    const auto workspace = getLocalBounds().reduced(18);
    const auto local = position - workspace.getPosition();
    return WorkspaceLayoutState::dropZoneForPosition(
        local.x, local.y, workspace.getWidth(), workspace.getHeight());
}

bool MainComponent::isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails& details)
{
    return draggedTool(details).has_value();
}

void MainComponent::itemDragEnter(const juce::DragAndDropTarget::SourceDetails& details)
{
    activeDropZone = dropZoneAt(details.localPosition);
    repaint();
}

void MainComponent::itemDragMove(const juce::DragAndDropTarget::SourceDetails& details)
{
    const auto zone = dropZoneAt(details.localPosition);
    if (zone != activeDropZone)
    {
        activeDropZone = zone;
        repaint();
    }
}

void MainComponent::itemDragExit(const juce::DragAndDropTarget::SourceDetails&)
{
    activeDropZone = WorkspaceLayoutState::DropZone::none;
    repaint();
}

void MainComponent::itemDropped(const juce::DragAndDropTarget::SourceDetails& details)
{
    const auto tool = draggedTool(details);
    const auto zone = dropZoneAt(details.localPosition);
    activeDropZone = WorkspaceLayoutState::DropZone::none;
    repaint();
    if (!tool.has_value() || zone == WorkspaceLayoutState::DropZone::none)
        return;

    if (*tool == ToolType::harmonics)
    {
        presentTool(
            *tool, zone == WorkspaceLayoutState::DropZone::floating
                       ? WorkspaceToolState::Presentation::floating
                       : WorkspaceToolState::Presentation::docked);
        rebuildWorkspaceContainer();
        return;
    }

    const auto other = *tool == ToolType::tuner ? ToolType::spectrogram : ToolType::tuner;
    const auto otherIsDocked =
        stateFor(other).presentation() == WorkspaceToolState::Presentation::docked;
    const auto result = workspaceLayoutState.applyDrop(layoutTool(*tool), zone, otherIsDocked);
    if (result.destination == WorkspaceLayoutState::Destination::floating)
        presentTool(*tool, WorkspaceToolState::Presentation::floating);
    else if (result.destination == WorkspaceLayoutState::Destination::docked)
    {
        if (stateFor(*tool).presentation() != WorkspaceToolState::Presentation::docked)
            presentTool(*tool, WorkspaceToolState::Presentation::docked);
        rebuildWorkspaceContainer();
    }
}

void MainComponent::paintOverChildren(juce::Graphics& graphics)
{
    if (activeDropZone == WorkspaceLayoutState::DropZone::none)
        return;

    const auto palette = appPaletteFor(currentTheme);
    const auto workspace = getLocalBounds().reduced(18);
    constexpr WorkspaceLayoutState::DropZone zones[]{
        WorkspaceLayoutState::DropZone::left,   WorkspaceLayoutState::DropZone::right,
        WorkspaceLayoutState::DropZone::top,    WorkspaceLayoutState::DropZone::bottom,
        WorkspaceLayoutState::DropZone::centre, WorkspaceLayoutState::DropZone::floating};
    for (const auto zone : zones)
    {
        const auto bounds = dropZoneBounds(zone, workspace).reduced(5);
        graphics.setColour(
            zone == activeDropZone ? palette.accent.withAlpha(0.72f)
                                   : palette.panel.withAlpha(0.88f));
        graphics.fillRoundedRectangle(bounds.toFloat(), 7.0f);
        graphics.setColour(zone == activeDropZone ? juce::Colours::white : palette.foreground);
        graphics.drawRoundedRectangle(bounds.toFloat(), 7.0f, 2.0f);
        graphics.setFont(juce::FontOptions(16.0f, juce::Font::bold));
        graphics.drawFittedText(
            dropZoneLabel(zone), bounds.reduced(8), juce::Justification::centred, 1);
    }
}
