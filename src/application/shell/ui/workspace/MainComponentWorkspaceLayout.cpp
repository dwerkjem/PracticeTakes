#include "../../MainComponent.h"

#include "DockedToolPanel.h"

namespace
{
constexpr int minimumHorizontalToolSize = 480;
constexpr int minimumVerticalToolSize = 280;
constexpr int dividerSize = 8;
} // namespace

void MainComponent::setWorkspaceLayout(WorkspaceLayoutState::Layout layout)
{
    workspaceLayoutState.setLayout(layout);
    if (tunerState.isOpen())
        presentTool(ToolType::tuner, WorkspaceToolState::Presentation::docked);
    if (spectrogramState.isOpen())
        presentTool(ToolType::spectrogram, WorkspaceToolState::Presentation::docked);
    rebuildWorkspaceContainer();
}

void MainComponent::rebuildWorkspaceContainer()
{
    if (workspaceTabs != nullptr)
    {
        while (workspaceTabs->getNumTabs() > 0)
            workspaceTabs->removeTab(0);
        removeChildComponent(workspaceTabs.get());
        workspaceTabs.reset();
    }

    if (workspaceDivider != nullptr)
    {
        removeChildComponent(workspaceDivider.get());
        workspaceDivider.reset();
    }

    for (auto* dock : {tunerDock.get(), spectrogramDock.get()})
    {
        if (dock != nullptr && dock->getParentComponent() == this)
            removeChildComponent(dock);
    }

    const auto tunerIsDocked =
        tunerState.presentation() == WorkspaceToolState::Presentation::docked &&
        tunerDock != nullptr;
    const auto spectrogramIsDocked =
        spectrogramState.presentation() == WorkspaceToolState::Presentation::docked &&
        spectrogramDock != nullptr;
    if (!tunerIsDocked && !spectrogramIsDocked)
    {
        resized();
        return;
    }

    if (tunerIsDocked && spectrogramIsDocked &&
        workspaceLayoutState.layout() == WorkspaceLayoutState::Layout::tabbed)
    {
        workspaceTabs = std::make_unique<juce::TabbedComponent>(juce::TabbedButtonBar::TabsAtTop);
        workspaceTabs->setTabBarDepth(38);
        workspaceTabs->setLookAndFeel(&appLookAndFeel);
        addAndMakeVisible(*workspaceTabs);

        const auto addTab = [this](ToolType tool)
        {
            auto* dock = dockFor(tool).get();
            workspaceTabs->addTab(toolName(tool), appPaletteFor(currentTheme).panel, dock, false);
        };
        const auto first = toolType(workspaceLayoutState.first());
        addTab(first);
        addTab(first == ToolType::tuner ? ToolType::spectrogram : ToolType::tuner);
        workspaceTabs->setCurrentTabIndex(
            workspaceLayoutState.active() == workspaceLayoutState.first() ? 0 : 1);
    }
    else
    {
        if (tunerIsDocked)
            addAndMakeVisible(*tunerDock);
        if (spectrogramIsDocked)
            addAndMakeVisible(*spectrogramDock);

        if (tunerIsDocked && spectrogramIsDocked)
        {
            workspaceLayoutManager.clearAllItems();
            const auto vertical =
                workspaceLayoutState.layout() == WorkspaceLayoutState::Layout::vertical;
            const auto minimumSize = vertical ? minimumVerticalToolSize : minimumHorizontalToolSize;
            workspaceLayoutManager.setItemLayout(0, minimumSize, -1.0, -0.5);
            workspaceLayoutManager.setItemLayout(1, dividerSize, dividerSize, dividerSize);
            workspaceLayoutManager.setItemLayout(2, minimumSize, -1.0, -0.5);
            workspaceDivider = std::make_unique<juce::StretchableLayoutResizerBar>(
                &workspaceLayoutManager, 1, !vertical);
            workspaceDivider->setLookAndFeel(&appLookAndFeel);
            addAndMakeVisible(*workspaceDivider);
        }
    }

    resized();
}

void MainComponent::layoutWorkspace(juce::Rectangle<int> bounds)
{
    if (workspaceTabs != nullptr)
    {
        workspaceTabs->setBounds(bounds);
        return;
    }

    const auto tunerIsDocked =
        tunerState.presentation() == WorkspaceToolState::Presentation::docked &&
        tunerDock != nullptr;
    const auto spectrogramIsDocked =
        spectrogramState.presentation() == WorkspaceToolState::Presentation::docked &&
        spectrogramDock != nullptr;
    if (tunerIsDocked && spectrogramIsDocked && workspaceDivider != nullptr)
    {
        auto* firstDock = dockFor(toolType(workspaceLayoutState.first())).get();
        auto* secondDock =
            dockFor(toolType(WorkspaceLayoutState::otherTool(workspaceLayoutState.first()))).get();
        juce::Component* components[]{firstDock, workspaceDivider.get(), secondDock};
        workspaceLayoutManager.layOutComponents(
            components, 3, bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight(),
            workspaceLayoutState.layout() == WorkspaceLayoutState::Layout::vertical, true);
    }
    else if (tunerIsDocked)
    {
        tunerDock->setBounds(bounds);
    }
    else if (spectrogramIsDocked)
    {
        spectrogramDock->setBounds(bounds);
    }
}

WorkspaceLayoutState::Tool MainComponent::layoutTool(ToolType tool) const
{
    return tool == ToolType::tuner ? WorkspaceLayoutState::Tool::tuner
                                   : WorkspaceLayoutState::Tool::spectrogram;
}

MainComponent::ToolType MainComponent::toolType(WorkspaceLayoutState::Tool tool) const
{
    return tool == WorkspaceLayoutState::Tool::tuner ? ToolType::tuner : ToolType::spectrogram;
}
